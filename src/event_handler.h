//
// Created by mabaiming on 18-9-27.
//

#ifndef TCP_TUNNEL_EVENT_HANDLER_H
#define TCP_TUNNEL_EVENT_HANDLER_H

#include <sys/epoll.h>

#include <string>

#include "utils.h"

using namespace std;

class EventHandler {
public:
  virtual void onError() = 0;
  virtual int onRead(const char* data, int size) = 0;
  virtual void onWritten(bool fromFull, int writtenSize) = 0;

  EventHandler(int fd, int maxBufferSize);
  void handleEvent(int events);
  void updateEvent();
  int addDataToWrite(const char* data, int size);
  int handleReadData();
  void setBroken();

  int fd_;
  struct epoll_event ev_;
  bool addedToUpdate_; // used by manager
  bool eofForWrite_;
  bool broken_;
  int maxBufferSize_;
  string readBuffer_;
  string writeBuffer_;
};

#endif //TCP_TUNNEL_EVENT_HANDLER_H
