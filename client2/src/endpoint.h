//
// Created by mabaiming on 18-8-14.
//

#ifndef TCP_TUNNEL_ENDPOINT_H
#define TCP_TUNNEL_ENDPOINT_H

#include <string>
#include <set>
#include <sys/epoll.h>
#include <fcntl.h>

#include "utils.h"

using namespace std;

class Endpoint {
public:
  static void init();
  static void loop();
  static void updateAll();
  static void recycle();

  Endpoint(int fd): fd_(fd) {
    fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL, 0) | O_NONBLOCK);
    ev_.data.ptr = this;
    epoll_ctl(sEpollFd, EPOLL_CTL_ADD, fd_, &ev_);
  }
  virtual void handleEvent(int events) = 0;
  virtual void updateEvent() = 0;

protected:
  static const int BufferCapacity = 4096;
  static int sEpollFd;
  static int sId;
  static set<Endpoint*> sUpdateSet;
  static set<Endpoint*> sRecycleSet;

  int fd_;
  struct epoll_event ev_;
};


#endif //TCP_TUNNEL_ENDPOINT_H
