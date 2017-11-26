//
// Created by mabaiming on 16-10-23.
//

#ifndef TCP_TUNNEL_BUFFER_MONITOR_H
#define TCP_TUNNEL_BUFFER_MONITOR_H

#include "frame.h"

#include <set>

using namespace std;

class MonitorBuffer {
public:
  MonitorBuffer(){}
  MonitorBuffer(shared_ptr<Buffer> buffer_): buffer(buffer_) {}
  shared_ptr<Buffer> buffer;
  string sendBuffer;
};
#endif //TCP_TUNNEL_BUFFER_MONITOR_H
