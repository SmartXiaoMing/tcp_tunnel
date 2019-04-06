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
  EndpointClientTunnelPeer(int fd): EndpointClientTunnel(fd, onTunnelChanged), name(), peer(NULL) {
    fdToPeerAddr(fd, remoteAddr);
    time(&lastTs);
  }
  ~EndpointClientTunnelPeer() {}
  char* getLastTime() {
    strftime(lastTime, sizeof(lastTime), "%Y-%m-%d %H:%M:%S", localtime(&lastTs));
    return lastTime;
  }
  string name;
  EndpointClientTunnelPeer* peer;
  time_t lastTs;
  char lastTime[64];
  char remoteAddr[30];
};

class Manager {
public:
  Manager() {}
  void prepare(const char* ip, int port) {
    serverIp = ip;
    serverPort = port;
    int fd = createServer(ip, port, 1000000);
    if (fd < 0) {
      printf("failed to create tunnel server:%s:%d", ip, port);
      exit(1);
    }
    EndpointServer* tunnelServer = new EndpointServer(fd, onNewClientTunnel);
  }
  void showStatus(time_t t) {
    time(&t);
    char nowStr[64];
    strftime(nowStr, sizeof(nowStr), "%Y-%m-%d %H:%M:%S", localtime(&t));
    ERROR("\nTIME\t %s", nowStr);
    ERROR("LISTENING\t%s:%d", serverIp, serverPort);
    map<string, EndpointClientTunnelPeer*>::iterator it = tunnelPeerMap.begin();
    for (; it != tunnelPeerMap.end(); ++it) {
      if (it->second->peer == NULL) {
        ERROR("%s\t%s[%s] <- ", it->first.c_str(), it->second->remoteAddr, it->second->getLastTime());
      } else {
        ERROR("%s\t%s[%s] <-> %s[%s]", it->first.c_str(), it->second->remoteAddr, it->second->getLastTime(),
          it->second->peer->remoteAddr, it->second->peer->getLastTime());
      }
    }
  }
  map<string, EndpointClientTunnelPeer*> tunnelPeerMap;
  const char* serverIp;
  int serverPort;
};

Manager manager;

void onNewClientTunnel(EndpointServer* server, int acfd) {
  EndpointClientTunnelPeer* tunnel = new EndpointClientTunnelPeer(acfd);
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
        INFO("[tunnel] recv frame, state=LOGIN, name=%s, addr=%s, time=%s",
             frame.message.c_str(), tunnel->remoteAddr, tunnel->getLastTime());
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
      } else if (frame.state == STATE_NONE) {
        time(&tunnel->lastTs);
        INFO("[tunnel] recv frame, state=NONE, name=%s, addr=%s, time=%s",
             tunnel->name.c_str(), tunnel->remoteAddr, tunnel->getLastTime());
        continue;
      } else {
        INFO("[tunnel %s] recv frame, state=%s", addrToStr(frame.addr.b), Frame::stateToStr(frame.state));
        if (tunnel->peer == NULL) {
          tunnel->sendData(STATE_CLOSE, frame.addr.b, NULL, 0);
          INFO("[tunnel %s] send frame, state=CLOSE", addrToStr(frame.addr.b));
          continue;
        } else {
          frame.addr.b[6] = 1 - frame.addr.b[6]; // NOTE haha, make a reverse
          tunnel->peer->sendData(frame.state, frame.addr.b, frame.message.data(), frame.message.size());
          INFO("[tunnel %s] send frame, state=%s, dataSize=%d",
               addrToStr(frame.addr.b), Frame::stateToStr(frame.state), (int) frame.message.size());
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
  int startTime = time(0);
  while (true) {
    Endpoint::loop();
    int now = time(0);
    if (now - startTime > 60 || now - startTime < 0) {
      manager.showStatus(now);
      startTime = now;
    }
  }
  return 0;
}
