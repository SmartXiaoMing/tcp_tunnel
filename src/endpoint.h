//
// Created by mabaiming on 18-8-14.
//

#ifndef TCP_TUNNEL_ENDPOINT_CLIENT_H
#define TCP_TUNNEL_ENDPOINT_CLIENT_H

#include <string>
#include <set>
#include <sys/epoll.h>
#include <fcntl.h>

#include "center.h"
#include "utils.h"

using namespace std;

class Center;

class Endpoint {
public:
  static const int TYPE_TUNNEL = 0;
  static const int TYPE_TRAFFIC = 1;

  static void init();
  static void loop();
  static void updateAll();
  static void recycle();

  static Endpoint* create(const uint8_t* addr, int type, const char* ip, int port);
  static void setCenter(Center* center);

  Endpoint(const uint8_t * addr, int type, int fd): type_(type), fd_(fd) {
    fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL, 0) | O_NONBLOCK);
    ev_.data.ptr = this;
    ev_.events = (EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLERR);
    epoll_ctl(sEpollFd, EPOLL_CTL_ADD, fd_, &ev_);
    if (addr != NULL) {
      memcpy(addr_, addr, sizeof(addr_));
    } else {
      memset(addr_, 0, sizeof(addr_));
    }
    eofForWrite_ = false;
    broken_ = false;
  }
  const uint8_t* getAddr();
  int getType();
  void handleEvent(int events);
  int getWriteBufferRemainSize();
  int appendDataToWriteBuffer(const char* data, int size);
  void setWriterBufferEof();
  void setBroken();
  void notifyCenterIsWritable();

private:
  void updateEvent();

  static const int BufferCapacity = 4096;
  static int sEpollFd;
  static int sId;
  static set<Endpoint*> sUpdateSet;
  static set<Endpoint*> sRecycleSet;
  static Center* sCenter;

  int fd_;
  struct epoll_event ev_;

  int type_;
  bool eofForWrite_;
  bool broken_;
  uint8_t addr_[6];
  string buffer_;
};

#endif //TCP_TUNNEL_ENDPOINT_CLIENT_H
