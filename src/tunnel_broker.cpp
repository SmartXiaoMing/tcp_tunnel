//
// Created by mabaiming on 18-8-15.
//

#include <stdlib.h>
#include <string.h>
#include <string>
#include <map>

#include "endpoint_client.h"
#include "endpoint_server.h"
#include "endpoint_client_tunnel.h"
#include "frame.h"

using namespace std;

void onNewClientTunnel(EndpointServer* endpoint, int acfd);
void onTunnelChanged(EndpointClient* endpoint, int event, const char* data, int size);

class EndpointClientTunnelPeer: public EndpointClientTunnel {
public:
  EndpointClientTunnelPeer(int fd): EndpointClientTunnel(fd, onTunnelChanged), name(), peer(NULL) {}
  ~EndpointClientTunnelPeer() {}
  string name;
  EndpointClientTunnelPeer* peer;
};

class Manager {
public:
  Manager() {}
  void prepare(const char* ip, int port) {
    int fd = createServer(ip, port, 1000000);
    if (fd < 0) {
      ERROR("failed to create tunnel server:%s:%d", ip, port);
      exit(1);
    }
    EndpointServer* tunnelServer = new EndpointServer(fd, onNewClientTunnel);
  }
  map<string, EndpointClientTunnelPeer*> tunnelPeerMap;
};

Manager manager;

void onNewClientTunnel(EndpointServer* server, int acfd) {
  EndpointClientTunnelPeer* clientTunnel = new EndpointClientTunnelPeer(acfd);
}

void onTunnelChanged(EndpointClient* endpoint, int event, const char* data, int size) {
  EndpointClientTunnelPeer* tunnel = (EndpointClientTunnelPeer*) endpoint;
  if (event == EVENT_ERROR || event == EVENT_CLOSED) {
    if (tunnel->name.empty()) {
      return; // unassigned name
    }
    map<string, EndpointClientTunnelPeer*>::iterator it = manager.tunnelPeerMap.find(tunnel->name);
    if (it == manager.tunnelPeerMap.end()) {
      return;
    }
    it->second = tunnel->peer;
    if (it->second != NULL) {
      it->second->sendData(STATE_RESET, NULL, NULL, 0);
      it->second->peer = NULL;
      tunnel->peer = NULL;
    } else {
      manager.tunnelPeerMap.erase(it);
    }
    return;
  }
  if (event == EVENT_READ) {
    Frame frame;
    while (tunnel->parseFrame(frame) > 0) {
      if (frame.state == STATE_LOGIN) {
        string& name = frame.message;
        map<string, EndpointClientTunnelPeer *>::iterator it = manager.tunnelPeerMap.find(name);
        if (it == manager.tunnelPeerMap.end()) {
          tunnel->name = name;
          manager.tunnelPeerMap[name] = tunnel;
        } else if (it->second->peer == NULL) {
          it->second->peer = tunnel;
          tunnel->peer = it->second;
          tunnel->name = name;
        } else {
          // ERROR
          tunnel->writeData(NULL, 0); // close the tunnel client
          return;
        }
      } if (frame.state == STATE_LOGIN) {
        continue;
      } else {
        if (tunnel->peer == NULL) {
          tunnel->sendData(STATE_CLOSE, frame.addr.b, NULL, 0);
          continue;
        } else {
          frame.addr.b[6] = 1 - frame.addr.b[6]; // NOTE haha, make a reverse
          tunnel->peer->sendData(frame.state, frame.addr.b, frame.message.data(), frame.message.size());
        }
      }
    }
    return;
  }
}

int main(int argc, char** argv) {
  const char* dServerIp = "0.0.0.0";
  int dServerPort = 8120;
  int dLevel = 0;

  const char* serverIp = dServerIp;
  int serverPort = dServerPort;
  logLevel = dLevel;
  for (int i = 1; i < argc; i += 2) {
    if (strcmp(argv[i], "--ip") == 0 && i + 1 < argc) {
      serverIp = argv[i + 1];
    } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
      serverPort = atoi(argv[i + 1]);
    } else if (strncmp(argv[i], "--v", 3) == 0) {
      logLevel = atoi(argv[i + 1]);
    } else {
      int ret = strcmp(argv[i], "--help");
      if (ret != 0) {
        printf("\nunknown option: %s\n", argv[i]);
      }
      printf("usage: %s [options]\n\n", argv[0]);
      printf("  --ip ip                  the server ip, default %s\n", dServerIp);
      printf("  --port (0~65535)         the server port, default %d\n", dServerPort);
      printf("  --v [0-5]                set log level, 0-5 means OFF, ERROR, WARN, INFO, DEBUG, default %d\n", dLevel);
      printf("  --help                   show the usage then exit\n");
      printf("\n");
      printf("version 1.0, report bugs to SmartXiaoMing(95813422@qq.com)\n");
      exit(ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
    }
  }
  Endpoint::init();
  manager.prepare(serverIp, serverPort);
  while (true) {
    Endpoint::loop();
  }
  return 0;
}
