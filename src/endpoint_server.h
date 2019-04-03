//
// Created by mabaiming on 19-3-28.
//
#pragma once

#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>

#include "endpoint.h"

using namespace std;

class EndpointServer;

typedef void (*EndpointServerCallback)(EndpointServer* endpoint, int acfd);

class EndpointServer: public Endpoint {
public:
  EndpointServer(int fd, EndpointServerCallback callback): callback_(callback) {
    create(fd);
  }
  virtual ~EndpointServer() {}
private:
  void create(int fd);
  EndpointServerCallback callback_;
  void handleEvent(int events);
  void updateEvent();
};