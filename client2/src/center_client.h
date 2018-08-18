//
// Created by mabaiming on 18-8-16.
//

#ifndef TCP_TUNNEL_CENTER_CLIENT_H
#define TCP_TUNNEL_CENTER_CLIENT_H

#include <map>
#include <string>

#include "endpoint_client.h"
#include "frame.hpp"
#include "center.h"

using namespace std;

class EndpointClient;

class CenterClient: public Center {
public:
  CenterClient() {
    reset();
  }
  void prepare(const char* host, int port, const char* group, const char* name);
  int getRemainBufferSizeFor(EndpointClient* endpoint);
  void appendDataToBufferFor(EndpointClient* endpoint, const char* data, int size);
  void notifyWritableFor(EndpointClient* endpoint);
  void notifyBrokenFor(EndpointClient* endpoint);
  void sendDataToTunnel(uint8_t state, int id, const char* data, int size);
private:
  void reset();
  void handleData();
  bool processFrame();

  static const int BufferCapacity = 40960;
  string frameBuffer_;
  Frame frame_;
  EndpointClient* trunk_;
  map<int, EndpointClient*> leaves_;
};

#endif //TCP_TUNNEL_CENTER_CLIENT_H
