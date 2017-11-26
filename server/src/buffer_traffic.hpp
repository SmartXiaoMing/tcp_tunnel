//
// Created by mabaiming on 16-12-20.
//

#ifndef TCP_TUNNEL_BUFFER_TRAFFIC_H
#define TCP_TUNNEL_BUFFER_TRAFFIC_H

#include "buffer.hpp"

using namespace std;
class TrafficBuffer {
public:
  static const int TRAFFIC_OK = 0;
  static const int TRAFFIC_CREATING = 1;
  static const int TRAFFIC_CLOSING = 2;
  static const int TRAFFIC_CLOSED = 3;
  static const int TRAFFIC_CREATE_FAILURE = 4;

  TrafficBuffer(): state(TRAFFIC_OK) {}
  TrafficBuffer(shared_ptr<Buffer> buffer_):
    buffer(buffer_), tunnelId(-1), state(TRAFFIC_OK) {}

  shared_ptr<Buffer> buffer;
  int tunnelId;
  int state;
};

#endif //TCP_TUNNEL_BUFFER_TRAFFIC_H