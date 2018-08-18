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
int Endpoint::sEpollFd = 0;
int Endpoint::sId = 0;

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
    INFO("recycle endpoint, fd:%d\n", f->fd_);
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

