//
// Created by mabaiming on 18-9-27.
//

#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <string>

#include "utils.h"
#include "event_manager.h"
#include "event_handler.h"

using namespace std;

EventHandler::EventHandler(int fd, int maxBufferSize):
    fd_(fd), maxBufferSize_(maxBufferSize), broken_(false), eofForWrite_(false) {
  fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL, 0) | O_NONBLOCK);
  ev_.data.ptr = this;
  ev_.events = (EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLERR);
  epoll_ctl(EventManager::sSingle->epollFd, EPOLL_CTL_ADD, fd_, &ev_);
}

void
EventHandler::handleEvent(int events) {
  if (broken_) {
    return;
  }
  if ((events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))) {
    setBroken();
    return;
  }
  int state = 0;
  if (events & EPOLLIN) {
    while (readBuffer_.size() < maxBufferSize_) {
      int remain = maxBufferSize_ - readBuffer_.size();
      if (remain <= 0) {
        break;
      }
      char buf[remain];
      int len = recv(fd_, buf, remain, 0);
      if (len == 0) {
        setBroken();
        return;
      } else if (len > 0) {
        readBuffer_.append(buf, len);
        handleReadData();
      } else if (len < 0) {
        if (!isGoodCode()) {
          setBroken();
          return;
        }
        break; // ignore for EAGAIN...
      }
    }
  }
  if (events & EPOLLOUT) {
    int size = writeBuffer_.size();
    if (size > 0) {
      int len = send(fd_, writeBuffer_.data(), size, MSG_NOSIGNAL);
      if (len < 0) {
        if  (!isGoodCode()) {
          setBroken();
        }
        // else ignore for EAGAIN
      } else if (len > 0) {
        onWritten(size >= maxBufferSize_, len);
        writeBuffer_.erase(0, len);
      }
    }
    if (eofForWrite_ && writeBuffer_.empty()) {
      setBroken();
      return;
    }
  }
}

void
EventHandler::updateEvent() {
  if (broken_) {
    return;
  }
  int newEvent = ev_.events;
  if (maxBufferSize_ - readBuffer_.size() > 0) {
    newEvent |= EPOLLIN;
  } else {
    newEvent &= ~EPOLLIN;
  }
  if (writeBuffer_.size() > 0 || eofForWrite_) {
    newEvent |= EPOLLOUT;
  } else {
    newEvent &= ~EPOLLOUT;
  }
  if (newEvent != ev_.events) {
    ev_.events = newEvent;
    epoll_ctl(EventManager::sSingle->epollFd, EPOLL_CTL_MOD, fd_, &ev_);
  }
  addedToUpdate_ = false;
}

int
EventHandler::addDataToWrite(const char* data, int size) {
  if (eofForWrite_ || broken_) {
    return 0;
  }
  EventManager::sSingle->addHandler(this);
  if (size == 0) {
    eofForWrite_ = true;
    return 0;
  } else {
    if (size <= maxBufferSize_ - writeBuffer_.size()) {
      writeBuffer_.append(data, size);
      return size;
    } else {
      return 0;
    }
  }
}

int
EventHandler::handleReadData() {
  if (!broken_) {
    return 0;
  }
  if (readBuffer_.empty()) {
    return 0;
  }
  int n = onRead(readBuffer_.data(), readBuffer_.size());
  if (n > 0) {
    readBuffer_.erase(0, n);
    return 0;
  }
  return -1; // is full
}

void
EventHandler::setBroken() {
  if (!broken_) {
    broken_ = true;
    EventManager::sSingle->addHandler(this);
    onError();
  }
}