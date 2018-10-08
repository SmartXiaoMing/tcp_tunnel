//
// Created by mabaiming on 18-9-27.
//
#include "event_manager.h"

#include <unistd.h>

#include "event_handler.h"

EventManager* EventManager::sSingle = new EventManager;

void
EventManager::addHandler(EventHandler* handler) {
  if (!handler->broken_ && !handler->addedToUpdate_) {
    handler->addedToUpdate_ = true;
    sUpdateSet.insert(handler);
  }
}

void
EventManager::init() {
  epollFd = epoll_create1(0);
  if(epollFd < 0) {
    WARN("[manager] failed to epoll_create1\n");
    exit(EXIT_FAILURE);
  }
  fcntl(epollFd, F_SETFL, fcntl(epollFd, F_GETFL, 0) | O_NONBLOCK);
}

void
EventManager::loop() {
  update();
  recycle();
  const int MAX_EVENTS = 100;
  int epollWaitTime = 50;
  struct epoll_event events[MAX_EVENTS];
  int n = epoll_wait(epollFd, events, MAX_EVENTS, epollWaitTime);
  if (n == -1) {
    ERROR("[manager] wait failed, then exit\n");
    exit(EXIT_FAILURE);
    return;
  }
  for (int i = 0; i < n; i++) {
    EventHandler* handler = (EventHandler*) events[i].data.ptr;
    handler->handleEvent(events[i].events);
    addHandler(handler);
  }
}

void
EventManager::update() {
  set<EventHandler*>::iterator it = sUpdateSet.begin();
  while (it != sUpdateSet.end()) {
    EventHandler* h = *it;
    h->updateEvent();
    h->addedToUpdate_ = false;
    ++it;
  }
  sUpdateSet.clear();
}

void
EventManager::recycle() {
  set<EventHandler*>::iterator it = sRecycleSet.begin();
  while (it != sRecycleSet.end()) {
    EventHandler* f = *it;
    close(f->fd_);
    delete f;
    ++it;
  }
  sRecycleSet.clear();
}
