//
// Created by mabaiming on 18-8-14.
//

#ifndef TCP_TUNNEL_ENDPOINT_SERVER_H
#define TCP_TUNNEL_ENDPOINT_SERVER_H

#include "endpoint.h"
#include "center_server.h"

class CenterServer;

using namespace std;

class EndpointServer: public Endpoint {
public:
  static EndpointServer* create(int type, const char* ip, int port, const char* group, const char* name,
    const char* remoteIp, int remotePort);

  EndpointServer(int type, int fd, const char* group, const char* name, const char* remoteIp, int remotePort)
      : type_(type), Endpoint(fd) {
    tunnel_= NULL;
    group_ = group;
    name_ = name;
    remoteIp_ = remoteIp;
    remotePort_ = remotePort;
  }

  void handleEvent(int events);
  bool match(const string& group, const string& name, EndpointClient* client);
  int getId();
  int getType();
  void setCenter(CenterServer* center);
  EndpointClient* getTunnel();

private:
  void updateEvent();
  int type_;
  string group;
  string name;
  string remoteIp;
  int remotePort;
  CenterServer* center_;
  EndpointClient* tunnel_;
};


#endif //TCP_TUNNEL_ENDPOINT_SERVER_H
