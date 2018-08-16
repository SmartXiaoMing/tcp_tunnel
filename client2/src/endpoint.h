//
// Created by mabaiming on 18-8-14.
//

#ifndef TCP_TUNNEL_ENDPOINT_H
#define TCP_TUNNEL_ENDPOINT_H

#include <string>
#include <set>
#include <sys/epoll.h>

#include "center.h"
#include "utils.h"

using namespace std;

class Center;

class Endpoint {
public:
  static const int TYPE_TUNNEL = 0;
  static const int TYPE_TRAFFIC = 1;

  static void init();
  static Endpoint* create(int id, int type, const char* ip, int port);
  static void loop();
  static void updateAll();
  static void recycle();
  static void setCenter(Center* center);

  int getId();
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
  static set<Endpoint*> sUpdateSet;
  static set<Endpoint*> sRecycleSet;
  static Center* sCenter;

  int id_;
  int fd_;
  int type_;
  bool eofForWrite_;
  bool broken_;
  struct epoll_event ev_;
  string buffer_;
};


#endif //TCP_TUNNEL_ENDPOINT_H
