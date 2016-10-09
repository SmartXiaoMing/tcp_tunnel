//
// Created by mabaiming on 16-8-29.
//

#ifndef TCP_TUNNEL_TCP_MONITOR_H
#define TCP_TUNNEL_TCP_MONITOR_H

#include "common.h"
#include "logger.h"
#include "tcp_base.h"
#include "tunnel_package.h"

#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <vector>

using namespace std;
using namespace Common;

class TcpMonitor: public TcpBase {
public:
  TcpMonitor() {}
  void init(uint16_t monitorPort);
  bool handleMonitor(uint32_t events, int eventFd);
  void run(const string& cmd);

private:
  string recvBuffer;
  string sendBuffer;
  uint16_t port;
  int serverFd;
};

#endif //TCP_TUNNEL_TCP_MONITOR_H
