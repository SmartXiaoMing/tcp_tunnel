//
// Created by mabaiming on 18-8-16.
//

#ifndef TCP_TUNNEL_CENTER_SERVER_H
#define TCP_TUNNEL_CENTER_SERVER_H

#include <map>
#include <string>

#include "endpoint.h"
#include "frame.hpp"
#include "utils.h"
#include "center.h"

using namespace std;

class Endpoint;

class ServerCenter: public Center {
public:
  void prepare(const char* host, int port, const char* group, const char* name);
  int getRemainBufferSizeFor(Endpoint* endpoint);
  void appendDataToBufferFor(Endpoint* endpoint, const char* data, int size);
  void notifyWritableFor(Endpoint* endpoint);
  void notifyBrokenFor(Endpoint* endpoint);
  void sendDataToTunnel(uint8_t state, int id, const char* data, int size);
private:
  void reset();
  void handleData();
  bool processFrame();

  static const int BufferCapacity = 40960;
  string frameBuffer_;
  Frame frame_;
  Endpoint* trunk_;
  map<int, Endpoint*> leaves_;
};

#endif //TCP_TUNNEL_CENTER_SERVER_H
