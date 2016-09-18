//
// Created by mabaiming on 16-8-29.
//

#ifndef TCP_TUNNEL_TCP_SERVER_H
#define TCP_TUNNEL_TCP_SERVER_H

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
#include <map>
#include <set>

using namespace std;

class TcpServer: public TcpBase {
public:

  TcpServer(): monitorServerFd(-1) {}

  void init(
    const string& tunnelIp, uint16_t tunnelPort, int tunnelConnection,
    const string& trafficIp, const vector<uint16_t>& trafficPortList,
    int trafficConnection,
    int monitorPort,
    const string& tunnelSecret
  );

  int acceptMonitorClient(int serverFd);
  int acceptTrafficClient(int serverFd);
  int acceptTunnelClient(int serverFd);
  int assignTunnelClient(int trafficServerFd, int trafficClientFd);
  int chooseTunnelClient(int trafficServerFd);
  void cleanUpMonitorClient(int fd);
  void cleanUpTunnelClient(int fd);
  void cleanUpTrafficClient(int fd);
  int getAvailableTunnelClientCount() const;
  bool handleMonitorClient(uint32_t events, int eventFd);
  bool handleTrafficClient(uint32_t events, int eventFd);
  bool handleTunnelClient(uint32_t events, int eventFd);
  int prepareMonitor(const string& ip, uint16_t port, int connection);
  int prepareTraffic(const string& ip, uint16_t port, int connection);
  int prepareTunnel(const string& ip, uint16_t port, int connection);
  void run();

private:
    int monitorServerFd;
    map<int, string> monitorClientMap;
    string secret;
    map<int, TunnelClientInfo> tunnelClientMap; // count, buffer
    int tunnelServerFd;
    map<int, TrafficClientInfo> trafficClientMap; // trafficClient -> (trafficServer, tunnelClient)
    map<int, int> trafficServerMap; // trafficServer -> tunnelClient
};

#endif // TCP_TUNNEL_TCP_SERVER_H
