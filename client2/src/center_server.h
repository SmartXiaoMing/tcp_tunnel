//
// Created by mabaiming on 18-8-16.
//

#ifndef TCP_TUNNEL_CENTER_SERVER_H
#define TCP_TUNNEL_CENTER_SERVER_H

#include <map>
#include <string>

#include "endpoint_client.h"
#include "endpoint_server.h"
#include "frame.hpp"
#include "utils.h"
#include "center.h"

using namespace std;

class EndpointServer;

struct TunnelInfo {
  int localPort;
  string group;
  string name;
  string remoteIp;
  int remotePort;
  string buffer;
};

class CenterServer: public Center {
public:
  void prepare(int tunnelPort, int trafficPort);
  int getRemainBufferSizeFor(EndpointClient* endpoint);
  void appendDataToBufferFor(EndpointClient* endpoint, const char* data, int size);
  void notifyWritableFor(EndpointClient* endpoint);
  void notifyBrokenFor(EndpointClient* endpoint);
  void notifyNewClient(EndpointClient* endpoint);
private:

  static const int BufferCapacity = 40960;
  EndpointServer* traffic_;
  set<EndpointServer*> trafficServerSet_;
  EndpointServer* tunnel_;
  map<int, pair<EndpointClient*, string>> trafficClients_;
  map<EndpointClient*, EndpointClient*> tunnelClients_;
};

#endif //TCP_TUNNEL_CENTER_SERVER_H
