//
// Created by mabaiming on 16-8-29.
//

#ifndef TCP_TUNNEL_TCP_CLIENT_H
#define TCP_TUNNEL_TCP_CLIENT_H

#include "buffer.h"
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
  struct TrafficServerInfo {
    Buffer sendBuffer;
    int connectId;
    TrafficServerInfo(): connectId(-1) {}
    TrafficServerInfo(int cid): connectId(cid) {}
  };

  TcpClient(): heartbeat(60) {}
  void init(
      const string& tunnelAddrList, int retryInterval,
      const string& trafficIp_, uint16_t trafficPort_,
      const string& tunnelSecret,
      int heartbeat
  );
  void cleanUpTrafficClient(int fd, int ctrl);
  void cleanUpTrafficServer(int fd);
  bool handleTunnelClient(uint32_t events, int eventFd);
  bool handleTrafficServer(uint32_t events, int eventFd);
  void resetTunnelServer();
  void retryConnectTunnelServer();
  bool refreshAddrList();
  void run();

private:
  string addrListStr;
  int heartbeat;
  string secret;
  Buffer tunnelRecvBuffer;
  Buffer tunnelSendBuffer;
  int tunnelServerFd;
  vector<Addr> tunnelServerList;
  int retryInterval;
  string trafficServerIp;
  uint16_t trafficServerPort;
  map<int, TrafficServerInfo> trafficServerMap; // trafficServer -> trafficClient
  map<int, int> trafficClientMap; // trafficClient -> trafficServer
};

#endif //TCP_TUNNEL_TCP_CLIENT_H
