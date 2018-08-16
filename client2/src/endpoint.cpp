//
// Created by mabaiming on 18-8-14.
//

#include <arpa/inet.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>

#include "endpoint.h"
#include "center.h"

set<Endpoint*> Endpoint::sUpdateSet;
set<Endpoint*> Endpoint::sRecycleSet;
Center* Endpoint::sCenter = NULL;
int Endpoint::sEpollFd = 0;

void
Endpoint::init() {
  sEpollFd = epoll_create1(0);
  if(sEpollFd < 0) {
    WARN("failed to epoll_create1\n");
    exit(EXIT_FAILURE);
  }
  fcntl(sEpollFd, F_SETFL, fcntl(sEpollFd, F_GETFL, 0) | O_NONBLOCK);
}

void
Endpoint::updateAll() {
  set<Endpoint*>::iterator it = sUpdateSet.begin();
  while (it != sUpdateSet.end()) {
    Endpoint* f = *it;
    f->updateEvent();
    ++it;
  }
  sUpdateSet.clear();
}

void
Endpoint::recycle() {
  set<Endpoint*>::iterator it = sRecycleSet.begin();
  while (it != sRecycleSet.end()) {
    Endpoint* f = *it;
    INFO("recycle endpoint, id:%d\n", f->id_);
    close(f->fd_);
    delete f;
    ++it;
  }
  sRecycleSet.clear();
}

Endpoint*
Endpoint::create(int id, const char* ip, int port) {
  int fd = socket(PF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    WARN("failed to socket, ip:%s, port:%d\n", ip, port);
    return NULL;
  }
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr(ip);
  addr.sin_port = htons(port);
  if (connect(fd, (struct sockaddr *) &addr, sizeof(struct sockaddr)) < 0) {
    WARN("failed to connect, ip:%s, port:%d\n", ip, port);
    return NULL;
  } else {
    INFO("success to connect,ip:%s, port:%d\n", ip, port);
    Endpoint* leaf = new Endpoint;
    leaf->id_ = id;
    leaf->fd_ = fd;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    epoll_ctl(sEpollFd, EPOLL_CTL_ADD, fd, &leaf->ev_);
    return leaf;
  }
}

void
Endpoint::loop() {
  const int MAX_EVENTS = 100;
  int epollWaitTime = 50;
  struct epoll_event events[MAX_EVENTS];
  int n = epoll_wait(sEpollFd, events, MAX_EVENTS, epollWaitTime);
  if (n == -1) {
    ERROR("failed to epoll_wait: %d\n", n);
    exit(EXIT_FAILURE);
    return;
  }
  for (int i = 0; i < n; i++) {
    Endpoint* e = (Endpoint*) events[i].data.ptr;
    e->handleEvent(events[i].events);
  }
  Endpoint::updateAll();
  Endpoint::recycle();
}

void
Endpoint::setCenter(Center* center) {
  sCenter = center;
}

int
Endpoint::getId() {
  return id_;
}

void
Endpoint::handleEvent(int events) {
  DEBUG("handleEvent:%d, for id:%d\n", events, id_);
  sUpdateSet.insert(this);
  if (broken_) {
    return;
  }
  if ((events & EPOLLERR) || (events & EPOLLHUP)) {
    broken_ = true;
    sCenter->notifyBrokenFor(this);
    return;
  }
  int state = 0;
  if (events & EPOLLIN) {
    while (true) {
      int remain = sCenter->getRemainBufferSizeFor(this);
      DEBUG("readableSize:%d, for id:%d\n", remain, id_);
      if (remain <= 0) {
        break;
      }
      char buf[remain];
      int len = recv(fd_, buf, remain, 0);
      DEBUG("readSize:%d, for id:%d\n", len, id_);
      if (len == 0) {
        broken_ = true;
        sCenter->notifyBrokenFor(this);
        return;
      } else if (len > 0) {
        sCenter->appendDataToBufferFor(this, buf, len);
      } else if (len < 0) {
        if (!isGoodCode()) {
          DEBUG("read error, for id:%d\n", id_);
          broken_ = true;
          sCenter->notifyBrokenFor(this);
          return;
        }
        break; // ignore for EAGAIN...
      }
    }
  }
  if (events & EPOLLOUT) {
    int size = buffer_.size();
    DEBUG("writabeSize:%d, for id:%d\n", size, id_);
    if (size > 0) {
      int len = send(fd_, buffer_.data(), size, MSG_NOSIGNAL);
      if (len < 0) {
        if  (!isGoodCode()) {
          DEBUG("write error, for id:%d, fd:%d\n", id_, fd_);
          broken_ = true;
          sCenter->notifyBrokenFor(this);
        }
        // else ignore for EAGAIN
      } else if (len > 0) {
        buffer_.erase(0, len);
        sCenter->notifyWritableFor(this);
      }
    }
    if (eofForWrite_ && buffer_.empty()) {
      DEBUG("write finished, for id:%d, fd:%d\n", id_, fd_);
      broken_ = true;
      sCenter->notifyBrokenFor(this);
      return;
    }
  }
}

int
Endpoint::getWriteBufferRemainSize() {
  return BufferCapacity - buffer_.size();
}

int
Endpoint::appendDataToWriteBuffer(const char* data, int size) {
  buffer_.append(data, size);
  sUpdateSet.insert(this);
  return size;
}

void
Endpoint::setWriterBufferEof() {
  eofForWrite_ = true;
  sUpdateSet.insert(this);
}

void
Endpoint::setBroken() {
  broken_ = true;
  sUpdateSet.insert(this);
}

void
Endpoint::notifyCenterIsWritable() {
  sUpdateSet.insert(this);
}

void
Endpoint::updateEvent() {
  if (broken_) {
    sRecycleSet.insert(this);
    return;
  }
  int newEvent = ev_.events;
  if (sCenter->getRemainBufferSizeFor(this) > 0) {
    newEvent |= EPOLLIN;
  } else {
    newEvent &= ~EPOLLIN;
  }
  if (buffer_.size() > 0) {
    newEvent |= EPOLLOUT;
  } else {
    newEvent &= ~EPOLLOUT;
  }
  if (newEvent != ev_.events) {
    DEBUG("update event:%d, for id:%d, fd:%d\n", newEvent, id_, fd_);
    ev_.events = newEvent;
    epoll_ctl(sEpollFd, EPOLL_CTL_MOD, fd_, &ev_);
  }
}
