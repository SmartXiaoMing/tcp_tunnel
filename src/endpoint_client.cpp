//
// Created by mabaiming on 19-3-28.
//

#include "endpoint_client.h"

#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "utils.h"

void
EndpointClient::create(int fd) {
  fd_ = fd;
  fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL, 0) | O_NONBLOCK);
  ev_.data.ptr = this;
  ev_.events = (EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLERR);
  epoll_ctl(Endpoint::epollFd, EPOLL_CTL_ADD, fd_, &ev_);
}

void
EndpointClient::handleEvent(int events) {
  if (markedToRecycle_) {
    return;
  }
  Endpoint::markToUpdate(this);
  if ((events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))) {
    discard();
    callback_(this, EVENT_ERROR, NULL, 0);
    return;
  }
  if ((events & EPOLLIN) && readableSize_ > 0) {
    int bufSize = 2048;
    if (readableSize_ < bufSize) {
      bufSize = readableSize_;
    }
    char buf[bufSize];
    int len = recv(fd_, buf, bufSize, 0);
    if (len == 0) {
      if (bufferRead.size() > 0) {
        callback_(this, EVENT_READ, bufferRead.data(), bufferRead.size());
      }
      discard();
      callback_(this, EVENT_CLOSED, NULL, 0);
      return;
    } else if (len > 0) {
      bufferRead.append(buf, len);
      readableSize_ -= len;
      callback_(this, EVENT_READ, bufferRead.data(), bufferRead.size());
    } else if (len < 0) {
      if (!isGoodCode()) {
        discard();
        callback_(this, EVENT_ERROR, NULL, 0);
        return;
      }
      // ignore for EAGAIN...
    }
  }
  if ((events & EPOLLOUT) && bufferWrite.size() > 0) {
    int len = send(fd_, bufferWrite.data(), bufferWrite.size(), MSG_NOSIGNAL);
    if (len < 0) {
      if  (!isGoodCode()) {
        discard();
        callback_(this, EVENT_ERROR, NULL, 0);
        return;
      }
      // else ignore for EAGAIN
    } else if (len > 0) {
      callback_(this, EVENT_WRITTEN, bufferWrite.data(), len);
      bufferWrite.erase(0, len);
    }
  }
  if (eofForWrite_ && bufferWrite.empty()) {
    discard();
    return;
  }
}

void
EndpointClient::updateEvent() {
  if (markedToRecycle_) {
    return;
  }
  int newEvent = ev_.events;
  if (readableSize_ > 0) {
    newEvent |= EPOLLIN;
  } else {
    newEvent &= ~EPOLLIN;
  }
  if (bufferWrite.size() > 0 || eofForWrite_) {
    newEvent |= EPOLLOUT;
  } else {
    newEvent &= ~EPOLLOUT;
  }
  if (newEvent != ev_.events) {
    ev_.events = newEvent;
    epoll_ctl(Endpoint::epollFd, EPOLL_CTL_MOD, fd_, &ev_);
  }
}

void
EndpointClient::writeData(const char* data, int size) {
  Endpoint::markToUpdate(this);
  if (size == 0) {
    eofForWrite_ = true;
  } else {
    bufferWrite.append(data, size);
  }
}

void
EndpointClient::addReadableSize(int size) {
  Endpoint::markToUpdate(this);
  readableSize_ += size;
  return;
}

void
EndpointClient::popReadData(int size) {
  if (size > 0) {
    Endpoint::markToUpdate(this);
    bufferRead.erase(0, size);
  }
}

void
EndpointClient::discard() {
  if (!markedToRecycle_) {
    Endpoint::markToRecycle(this);
  }
}
