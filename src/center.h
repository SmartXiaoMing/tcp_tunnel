//
// Created by mabaiming on 18-8-16.
//

#ifndef TCP_TUNNEL_CENTER_CLIENT_H
#define TCP_TUNNEL_CENTER_CLIENT_H

#include <map>
#include <string>

#include "endpoint.h"
#include "frame.hpp"

using namespace std;

class Endpoint;

class AddrCompare {
public:
  bool operator() (const uint8_t* a, const uint8_t* b) const {
    if (a == b) {
      return false;
    }
    if (a == NULL) {
      return true;
    }
    if (b == NULL) {
      return false;
    }
    return strncmp((const char*)a, (const char*)b, 6);
  }

};

class Center {
public:
  Center() {
    reset();
  }
  void prepare(const char* host, int port, const char* group, const char* name);
  int getRemainBufferSizeFor(Endpoint* endpoint);
  void appendDataToBufferFor(Endpoint* endpoint, const char* data, int size);
  void notifyWritableFor(Endpoint* endpoint);
  void notifyBrokenFor(Endpoint* endpoint);
  void sendDataToTunnel(uint8_t state, const uint8_t* addr, const char* data, int size);
private:
  void reset();
  void handleData();
  bool processFrame();

  static const int BufferCapacity = 409600;
  string frameBuffer_;
  Frame frame_;
  Endpoint* trunk_;
  map<const uint8_t*, Endpoint*, AddrCompare> leaves_;
};

#endif //TCP_TUNNEL_CENTER_CLIENT_H
