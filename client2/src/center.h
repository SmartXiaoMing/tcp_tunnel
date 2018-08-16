//
// Created by mabaiming on 18-8-15.
//

#ifndef TCP_TUNNEL_CENTER_H
#define TCP_TUNNEL_CENTER_H

#include <map>
#include <string>

#include "endpoint.h"

using namespace std;

class Endpoint;

class Center {
public:
  virtual int getRemainBufferSizeFor(Endpoint* endpoint) = 0;
  virtual void appendDataToBufferFor(Endpoint* endpoint, const char* data, int size) = 0;
  virtual void notifyWritableFor(Endpoint* endpoint) = 0;
  virtual void notifyBrokenFor(Endpoint* endpoint) = 0;
};
#endif //TCP_TUNNEL_CENTER_H
