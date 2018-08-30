//
// Created by mabaiming on 18-8-14.
//

#ifndef TCP_TUNNEL_ENDPOINT_CLIENT_H
#define TCP_TUNNEL_ENDPOINT_CLIENT_H

#include <string>
#include <set>
#include <sys/epoll.h>

#include "endpoint.h"
#include "center.h"
#include "utils.h"

using namespace std;

class Center;

class EndpointClient: public Endpoint {
public:
  static const int TYPE_TUNNEL = 0;
  static const int TYPE_TRAFFIC = 1;

  static EndpointClient* create(int64_t id, int type, const char* ip, int port);
  static void setCenter(Center* center);

  EndpointClient(int64_t id, int type, int fd): id_(id), type_(type), Endpoint(fd) {}
  int64_t getId();
  int getType();
  void handleEvent(int events);
  int getWriteBufferRemainSize();
  int appendDataToWriteBuffer(const char* data, int size);
  void setWriterBufferEof();
  void setBroken();
  void notifyCenterIsWritable();

private:
  void updateEvent();

  static Center* sCenter;

  int64_t id_;
  int type_;
  bool eofForWrite_;
  bool broken_;
  string buffer_;
};


#endif //TCP_TUNNEL_ENDPOINT_CLIENT_H
