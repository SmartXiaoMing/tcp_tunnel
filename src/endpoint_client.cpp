//
// Created by mabaiming on 19-3-28.
//

#include "endpoint_client.h"

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "utils.h"

bool
EndpointClient::createClient(const char *ip, int port) {
  fd_ = socket(PF_INET, SOCK_STREAM, 0);
  if (fd_ < 0) {
    discard();
    return false;
  }
  fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL, 0) | O_NONBLOCK);
  struct sockaddr_in saddr;
  memset(&saddr, 0, sizeof(saddr));
  saddr.sin_family = AF_INET;
  saddr.sin_addr.s_addr = inet_addr(ip);
  saddr.sin_port = htons(port);
  ev_.data.ptr = this;
  ev_.events = (EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLERR);
  epoll_ctl(Endpoint::epollFd, EPOLL_CTL_ADD, fd_, &ev_);
  int res = connect(fd_, (struct sockaddr *) &saddr, sizeof(struct sockaddr));
  if (res < 0 && errno != EINPROGRESS) {
    INFO("failed to connect %s:%d", ip, port);
    discard();
    return false;
  }
  return true;
}

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
  INFO("handleEvent recv event:%d", events); 
  if ((events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))) {
    discard();
    ERROR("recv event:%d", events);
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
        INFO("read eof:%d", len);
        callback_(this, EVENT_READ, bufferRead.data(), bufferRead.size());
      }
      discard();
      ERROR("read eof, len:%d, bufSize:%d, readableSize_:%d", len, bufSize, readableSize_);
      callback_(this, EVENT_CLOSED, NULL, 0);
      return;
    } else if (len > 0) {
      bufferRead.append(buf, len);
      readableSize_ -= len;      
      INFO("bufferRead.append:%d", len);
      callback_(this, EVENT_READ, bufferRead.data(), bufferRead.size());
    } else if (len < 0) {
      if (!isGoodCode()) {
        discard();
        ERROR("read error len:%d, not good code", len);
        callback_(this, EVENT_ERROR, NULL, 0);
        return;
      }
      // ignore for EAGAIN...
    }
  }
  if ((events & EPOLLOUT) && bufferWrite.size() > 0) {
    int len = send(fd_, bufferWrite.data(), bufferWrite.size(), MSG_NOSIGNAL);
    INFO("try to send data:%zd, in fact:%d", bufferWrite.size(), len);
    if (len < 0) {
      if  (!isGoodCode()) {
        discard();
        ERROR("write error len:%d, not good code", len);
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
    INFO("readableSize_:%d, close the EPOLLIN", readableSize_)
    newEvent &= ~EPOLLIN;
  }
  if (bufferWrite.size() > 0 || eofForWrite_) {
    newEvent |= EPOLLOUT;
  } else {
    newEvent &= ~EPOLLOUT;
  }
  //INFO("fd:%d, old:%s -> new:%s, bufferRead.size:%d, readableSize_:%d, bufferWrite.size:%d, eof:%d", fd_,
  //     eventToStr(ev_.events), eventToStr(newEvent),
  //     (int)bufferRead.size(), readableSize_, (int)bufferWrite.size(), eofForWrite_);
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
