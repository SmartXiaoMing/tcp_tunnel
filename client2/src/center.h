//
// Created by mabaiming on 18-8-15.
//

#ifndef TCP_TUNNEL_CENTER_H
#define TCP_TUNNEL_CENTER_H

#include <map>
#include <string>

#include "endpoint_client.h"

using namespace std;

class EndpointClient;

class Center {
public:
  virtual int getRemainBufferSizeFor(EndpointClient* endpoint) = 0;
  virtual void appendDataToBufferFor(EndpointClient* endpoint, const char* data, int size) = 0;
  virtual void notifyWritableFor(EndpointClient* endpoint) = 0;
  virtual void notifyBrokenFor(EndpointClient* endpoint) = 0;
};
#endif //TCP_TUNNEL_CENTER_H
