//
// Created by mabaiming on 19-3-28.
//
#pragma once

#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>

#include "endpoint.h"
#include "utils.h"

using namespace std;

class EndpointClient;

typedef void (*EndpointClientCallback)(EndpointClient* endpoint, int event, const char* data, int size);

class EndpointClient: public Endpoint {
public:
  EndpointClient(int fd, EndpointClientCallback callback):
      callback_(callback), readableSize_(MaxBufferSize), eofForWrite_(false) {
    create(fd);
  }
  EndpointClient(EndpointClientCallback callback):
      callback_(callback), readableSize_(MaxBufferSize), eofForWrite_(false) {
  }
  virtual ~EndpointClient() {}
  void writeData(const char* data, int size);
  void popReadData(int size);
  virtual void addReadableSize(int size);
  bool createClient(const char *ip, int port);
protected:
  void create(int fd);
  void handleEvent(int events);
  virtual void updateEvent();
  string bufferRead;
  string bufferWrite;
  int readableSize_;
  bool eofForWrite_;
  EndpointClientCallback callback_;
  void discard();
};