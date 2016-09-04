//
// Created by mabaiming on 16-8-29.
//

#ifndef TCP_TUNNEL_TCP_SERVER_H
#define TCP_TUNNEL_TCP_SERVER_H

#include "common.h"
#include "logger.h"
#include "tunnel_package.h"
#include "tcp_base.h"

#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <map>
#include <set>

using namespace std;

class TcpServer: public TcpBase {
public:
  TcpServer() {
    isServer = true;
  }

  void init(
    const string& tunnelIp, uint16_t tunnelPort, int tunnelConnection,
    const string& trafficIp, const vector<uint16_t>& trafficPortList, int trafficConnection
  );

  int acceptClient(int serverFd, int type);
  int assignTunnelClient(int trafficServerFd, int trafficClientFd);
  void cleanUpTunnelClient(int fd);
  void cleanUpTrafficClient(int fd);
  bool handleTrafficClient(const struct epoll_event& event);
  bool handleTunnelClient(const struct epoll_event& event);
  int prepare(int serverType, const string& ip, uint16_t port, int connection);
  void run();

private:
    map<int, TunnelClientInfo> tunnelClientMap; // tunnelClient -> ip, port, count, buffer
    int tunnelServerFd;
    map<int, int> trafficClientMap; // trafficClient -> tunnelClient
    map<int, int> trafficServerMap; // trafficServer -> tunnelClient
};

#endif // TCP_TUNNEL_TCP_SERVER_H
