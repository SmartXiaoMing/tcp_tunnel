//
// Created by mabaiming on 19-3-28.
//

#pragma once

#include "endpoint_client.h"
#include "utils.h"
#include "frame.h"

using namespace std;

class EndpointClientTraffic;

class EndpointClientTunnel: public EndpointClient {
public:
  EndpointClientTunnel(int fd, EndpointClientCallback callback): EndpointClient(fd, callback) {}
  ~EndpointClientTunnel() {}
  void sendData(const Frame& frame);
  int parseFrame(Frame& frame);
  void processTrafficFrame(Frame& frame);
};