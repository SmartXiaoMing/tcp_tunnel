//
// Created by mabaiming on 16-8-29.
//

#ifndef TCP_TUNNEL_TCP_CLIENT_H
#define TCP_TUNNEL_TCP_CLIENT_H

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

class TcpClient: public TcpBase {
public:
  TcpClient(): heartbeat(60) {}
  void init(
      const vector<Addr>& tunnelAddrList, int retryInterval,
      const string& trafficIp_, uint16_t trafficPort_,
      const string& tunnelSecret,
      int heartbeat
  );
  void cleanUpTrafficClient(int fd);
  void cleanUpTrafficServer(int fd);
  bool handleTunnelClient(uint32_t events, int eventFd);
  bool handleTrafficServer(uint32_t events, int eventFd);
  void resetTunnelServer();
  void retryConnectTunnelServer();
  void run();

private:
  int heartbeat;
  string secret;
  string tunnelBuffer;
  int tunnelServerFd;
  vector<Addr> tunnelServerList;
  int retryInterval;
  string trafficServerIp;
  uint16_t trafficServerPort;
  map<int, int> trafficServerMap; // trafficServer -> trafficClient
  map<int, int> trafficClientMap; // trafficClient -> trafficServer
};

#endif //TCP_TUNNEL_TCP_CLIENT_H
