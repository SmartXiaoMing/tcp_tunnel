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

using namespace std;

class TcpClient: public TcpBase {
public:
  TcpClient() {
    isServer = false;
  }
  void init(
    const string& tunnelIp, uint16_t tunnelPort, int retryInterval,
    const string& trafficIp_, uint16_t trafficPort_
  );
  void cleanUpTrafficClient(int fd);
  void cleanUpTrafficServer(int fd);
  bool handleTunnelClient(const struct epoll_event& event);
  bool handleTrafficServer(const struct epoll_event& event);
  void resetTunnelServer();
  void retryConnectTunnelServer();
  void run();
  int prepare(const string& ip, uint16_t port);

private:
  string tunnelBuffer;
  int tunnelRetryInterval;
  TunnelServerInfo tunnelServerInfo;
  string trafficServerIp;
  uint16_t trafficServerPort;
  map<int, int> trafficServerMap; // trafficServer -> trafficClient
  map<int, int> trafficClientMap; // trafficClient -> trafficServer
};

#endif //TCP_TUNNEL_TCP_CLIENT_H
