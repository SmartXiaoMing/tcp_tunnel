//
// Created by mabaiming on 19-3-28.
//

#include "endpoint.h"

#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>

#include "utils.h"

int Endpoint::epollFd = -1;
set<Endpoint*> Endpoint::updateSet;
set<Endpoint*> Endpoint::recycleSet;

void Endpoint::init() {
  epollFd = epoll_create1(0);
  if(epollFd < 0) {
    ERROR("failed to epoll_create1\n");
    exit(EXIT_FAILURE);
  }
  fcntl(epollFd, F_SETFL, fcntl(epollFd, F_GETFL, 0) | O_NONBLOCK);
}

void Endpoint::loop() {
  update();
  recycle();
  const int MAX_EVENTS = 100;
  int epollWaitTime = 50;
  struct epoll_event events[MAX_EVENTS];
  int n = epoll_wait(Endpoint::epollFd, events, MAX_EVENTS, epollWaitTime);
  if (n == -1) {
    ERROR("failed to wait, then exit\n");
    exit(EXIT_FAILURE);
    return;
  }
  for (int i = 0; i < n; i++) {
    Endpoint* endpoint = (Endpoint*) events[i].data.ptr;
    endpoint->handleEvent(events[i].events);
    markToUpdate(endpoint);
  }
}

void Endpoint::update() {
  set<Endpoint*>::iterator it = updateSet.begin();
  while (it != updateSet.end()) {
    Endpoint* h = *it;
    h->updateEvent();
    h->markedToUpdate_ = false;
    ++it;
  }
  updateSet.clear();
}

void Endpoint::recycle() {
  set<Endpoint*>::iterator it = recycleSet.begin();
  while (it != recycleSet.end()) {
    Endpoint* f = *it;
    close(f->fd_);
    delete f;
    ++it;
  }
  recycleSet.clear();
}

void Endpoint::markToUpdate(Endpoint* endpoint) {
  if (!endpoint->markedToUpdate_) {
    updateSet.insert(endpoint);
    endpoint->markedToUpdate_ = true;
  }
}
void Endpoint::markToRecycle(Endpoint* endpoint) {
  if (!endpoint->markedToRecycle_) {
    recycleSet.insert(endpoint);
    endpoint->markedToRecycle_ = true;
  }
}

Endpoint::Endpoint(): fd_(-1), markedToUpdate_(false), markedToRecycle_(false) {}
Endpoint::~Endpoint() {}