//
// Created by mabaiming on 19-3-28.
//
#pragma once

#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <set>

using namespace std;

const int EVENT_ERROR = 0;
const int EVENT_READ = 1;
const int EVENT_WRITTEN = 2;
const int EVENT_CLOSED = 3;

class Endpoint {
public:
  static const int MaxBufferSize = 40960;
  static int epollFd;
  static set<Endpoint*> updateSet;
  static set<Endpoint*> recycleSet;
  static void init();
  static void loop();

  Endpoint();
  virtual ~Endpoint();
  virtual void handleEvent(int events) = 0;
  virtual void updateEvent() = 0;

protected:
  static void update();
  static void recycle();
  static void markToUpdate(Endpoint* endpoint);
  static void markToRecycle(Endpoint* endpoint);

  int fd_;
  bool markedToUpdate_;
  bool markedToRecycle_;
  struct epoll_event ev_;
};