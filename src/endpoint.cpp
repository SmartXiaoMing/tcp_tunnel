//
// Created by mabaiming on 18-8-14.
//

#include <arpa/inet.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "endpoint.h"

set<Endpoint*> Endpoint::sUpdateSet;
set<Endpoint*> Endpoint::sRecycleSet;
int Endpoint::sEpollFd = 0;
int Endpoint::sId = 0;

Center* Endpoint::sCenter = NULL;

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
    INFO("[%-21s] recyle\n", addrToStr(f->getAddr()));
    close(f->fd_);
    delete f;
    ++it;
  }
  sRecycleSet.clear();
}

void
Endpoint::loop() {
  const int MAX_EVENTS = 100;
  int epollWaitTime = 50;
  struct epoll_event events[MAX_EVENTS];
  int n = epoll_wait(sEpollFd, events, MAX_EVENTS, epollWaitTime);
  if (n == -1) {
    ERROR("[LOOP] wait failed, then exit\n");
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


Endpoint*
Endpoint::create(const uint8_t* addr, int type, const char* ip, int port) {
  int fd = socket(PF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    WARN("[%-21s] failed to socket\n", addrToStr(addr));
    return NULL;
  }
  struct sockaddr_in saddr;
  memset(&saddr, 0, sizeof(saddr));
  saddr.sin_family = AF_INET;
  saddr.sin_addr.s_addr = inet_addr(ip);
  saddr.sin_port = htons(port);
  if (connect(fd, (struct sockaddr *) &saddr, sizeof(struct sockaddr)) < 0) {
    WARN("[%-21s] failed to connect %s:%d\n", addrToStr(addr), ip, port);
    return NULL;
  } else {
    INFO("[%-21s] success to connect %s:%d\n", addrToStr(addr), ip, port);
  }
  return new Endpoint(addr, type, fd);
}

void
Endpoint::setCenter(Center* center) {
  sCenter = center;
}

const uint8_t*
Endpoint::getAddr() {
  return addr_;
}

int
Endpoint::getType() {
  return type_;
}

void
Endpoint::handleEvent(int events) {
  DEBUG("[%-21s] handle event: %s\n", addrToStr(addr_), eventToStr(events));
  sUpdateSet.insert(this);
  if (broken_) {
    return;
  }
  if ((events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))) {
    DEBUG("[%-21s] error\n", addrToStr(addr_));
    setBroken();
    sCenter->notifyBrokenFor(this);
    return;
  }
  int state = 0;
  if (events & EPOLLIN) {
    while (true) {
      int remain = sCenter->getRemainBufferSizeFor(this);
      if (remain <= 0) {
        break;
      }
      char buf[remain];
      int len = recv(fd_, buf, remain, 0);
      if (len == 0) {
        DEBUG("[%-21s] read close\n", addrToStr(addr_));
        setBroken();
        sCenter->notifyBrokenFor(this);
        return;
      } else if (len > 0) {
        DEBUG("[%-21s] read data: %d\n", addrToStr(addr_), len);
        sCenter->appendDataToBufferFor(this, buf, len);
      } else if (len < 0) {
        if (!isGoodCode()) {
          DEBUG("[%-21s] read error\n", addrToStr(addr_));
          setBroken();
          sCenter->notifyBrokenFor(this);
          return;
        }
        break; // ignore for EAGAIN...
      }
    }
  }
  if (events & EPOLLOUT) {
    int size = buffer_.size();
    if (size > 0) {
      int len = send(fd_, buffer_.data(), size, MSG_NOSIGNAL);
      if (len < 0) {
        if  (!isGoodCode()) {
          DEBUG("[%-21s] write error\n", addrToStr(addr_));
          setBroken();
          sCenter->notifyBrokenFor(this);
        }
        // else ignore for EAGAIN
      } else if (len > 0) {
        DEBUG("[%-21s] write data: %d\n", addrToStr(addr_), len);
        buffer_.erase(0, len);
        sCenter->notifyWritableFor(this);
      }
    }
    if (eofForWrite_ && buffer_.empty()) {
      DEBUG("[%-21s] write eof\n", addrToStr(addr_));
      setBroken();
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
  DEBUG("[%-21s] set eof\n", addrToStr(addr_));
}

void
Endpoint::setBroken() {
  broken_ = true;
  sUpdateSet.insert(this);
  sRecycleSet.insert(this);
  DEBUG("[%-21s] set broken eof\n", addrToStr(addr_));
}

void
Endpoint::notifyCenterIsWritable() {
  sUpdateSet.insert(this);
}

void
Endpoint::updateEvent() {
  if (broken_) {
    return;
  }
  int newEvent = ev_.events;
  if (sCenter->getRemainBufferSizeFor(this) > 0) {
    newEvent |= EPOLLIN;
  } else {
    newEvent &= ~EPOLLIN;
  }
  if (buffer_.size() > 0 || eofForWrite_) {
    newEvent |= EPOLLOUT;
  } else {
    newEvent &= ~EPOLLOUT;
  }
  if (newEvent != ev_.events) {
    DEBUG("[%-21s] update event:%s\n", addrToStr(addr_), eventToStr(newEvent));
    ev_.events = newEvent;
    epoll_ctl(sEpollFd, EPOLL_CTL_MOD, fd_, &ev_);
  }
}
