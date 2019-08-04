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
  EndpointClientTunnelPeer(int fd): EndpointClientTunnel(fd, onTunnelChanged), name(), peerTunnel(NULL) {
    fdToPeerAddr(fd, remoteAddr);
    time(&lastTs);
    readSize = 0;
    writtenSize = 0;
  }
  ~EndpointClientTunnelPeer() {}
  char* getLastTime() {
    strftime(lastTime, sizeof(lastTime), "%Y-%m-%d %H:%M:%S", localtime(&lastTs));
    return lastTime;
  }
  string name;
  string peerName;
  EndpointClientTunnelPeer* peerTunnel;
  map<string, EndpointClientTunnelPeer*> sourceTunnelMap;
  time_t lastTs;
  int readSize;
  int writtenSize;
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
      printf("failed to create tunnel server:%s:%d\n", ip, port);
      exit(1);
    }
    EndpointServer* tunnelServer = new EndpointServer(fd, onNewClientTunnel);
  }

  void broadCastTunnelError(EndpointClientTunnelPeer* tunnel) {
    Frame frame;
    frame.state = STATE_TUNNEL_ERROR;
    frame.from = tunnel->name;
    frame.message = "tunnel error";
    map<string, EndpointClientTunnelPeer*>::iterator it = tunnelPeerMap.begin();
    for (; it != tunnelPeerMap.end(); ++it) {
      ERROR("[tunnel error] %s >> %s, state: tunnel_error", tunnel->name.c_str(), tunnel->peerName.c_str());
      it->second->sendData(frame);
    }
  }

  void cleanTunnel(time_t now) {
    map<string, EndpointClientTunnelPeer*>::iterator it = tunnelPeerMap.begin();
    for (; it != tunnelPeerMap.end(); ) {
      if (now - it->second->lastTs > 3000) {
        EndpointClientTunnelPeer* tunnel = it->second;
        ERROR("[tunnel error] timeout for %s, addr: %s, last time: %s, so send close to %s",
              it->first.c_str(), it->second->remoteAddr, it->second->getLastTime(), it->first.c_str());
        tunnel->writeData(NULL, 0);
        tunnelPeerMap.erase(it++);
        broadCastTunnelError(tunnel);
      } else {
        it++;
      }
    }
  }

  void showStatus(time_t now, int elapse) {
    char nowStr[64];
    strftime(nowStr, sizeof(nowStr), "%Y-%m-%d %H:%M:%S", localtime(&now));
    ERROR("\nTIME\t %s", nowStr);
    ERROR("LISTENING\t%s:%d", serverIp, serverPort);
    ERROR("Rule\t%zd", tunnelPeerMap.size());
    map<string, EndpointClientTunnelPeer*>::iterator it = tunnelPeerMap.begin();
    for (; it != tunnelPeerMap.end(); ++it) {
      EndpointClientTunnelPeer *tunnel = it->second;
      map<string, EndpointClientTunnelPeer*>::iterator it2 = tunnelPeerMap.find(tunnel->peerName);
      if (it2 != tunnelPeerMap.end()) {
        ERROR("%s ---> %s", tunnel->name.c_str(), tunnel->peerName.c_str());
      } else {
        ERROR("%s -x-> %s", tunnel->name.c_str(), tunnel->peerName.c_str());
      }
    }
  }

  string getBetween(const string& message, const string& startStr, const string& endStr) {
    int p1 = message.find(startStr);
    if (p1 < 0) {
      return "";
    }
    p1 += startStr.size();
    int p2 = message.find(endStr, p1);
    if (p2 < 0) {
      p2 = message.size();
    }
    return message.substr(p1, p2 - p1);
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
      // return; // unassigned name
    }
    map<string, EndpointClientTunnelPeer*>::iterator it = manager.tunnelPeerMap.find(tunnel->name);
    if (it == manager.tunnelPeerMap.end()) {
      return;
    }
    ERROR("[tunnel error] error occur on %s, addr: %s, so erase %s",
          it->first.c_str(), it->second->remoteAddr, it->first.c_str());
    manager.broadCastTunnelError(tunnel);
    manager.tunnelPeerMap.erase(it);
    return;
  }
  if (event == EVENT_READ) {
    tunnel->readSize += size;
    time(&tunnel->lastTs);
    Frame frame;
    while (tunnel->parseFrame(frame) > 0) {
      if (frame.state == STATE_TUNNEL_LOGIN) {
        INFO("[tunnel login] addr:%s, message: %s, ts: %s",
             tunnel->remoteAddr, frame.message.c_str(), tunnel->getLastTime());
        string name = manager.getBetween(frame.message, "name=", "&");
        string peerName = manager.getBetween(frame.message, "peerName=", "&");
        map<string, EndpointClientTunnelPeer *>::iterator it = manager.tunnelPeerMap.find(name);
        if (it == manager.tunnelPeerMap.end()) {
          tunnel->name = name;
          tunnel->peerName = peerName;
          manager.tunnelPeerMap[name] = tunnel;
        } else {
          INFO("[tunnel login] failed to login, %s exist already", tunnel->name.c_str());
          frame.state = STATE_TUNNEL_ERROR;
          const string& from = frame.from;
          frame.from = frame.to;
          frame.to = frame.from;
          frame.message = "the tunnel exists with name " + name;
          tunnel->sendData(frame);
          tunnel->writeData(NULL, 0); // close the tunnel client
          return;
        }
      } else {
        map<string, EndpointClientTunnelPeer *>::iterator targetIt = manager.tunnelPeerMap.find(frame.to);
        if (targetIt == manager.tunnelPeerMap.end()) {
          frame.state = STATE_TUNNEL_ERROR;
          const string& from = frame.from;
          frame.from = frame.to;
          frame.to = frame.from;
          frame.message = "target tunnel is not exist";
          INFO("[tunnel traffic] %s > null, state: %s, size: %zd, so send close to %s",
               tunnel->name.c_str(), Frame::stateToStr(frame.state), frame.message.size(), tunnel->name.c_str()); // TOOD
          tunnel->sendData(frame);
        } else {
          targetIt->second->sendData(frame);
          INFO("[tunnel traffic] %s > %s, state: %s, size: %zd",
               tunnel->name.c_str(), tunnel->peerName.c_str(), Frame::stateToStr(frame.state), frame.message.size());
        }
      }
    }
    return;
  } else if (event == EVENT_WRITTEN) {
    tunnel->writtenSize += size;
  }
}

int main(int argc, char** argv) {
  const char* dServerIp = "0.0.0.0";
  int dServerPort = 8120;
  int dLevel = 3;
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
  time_t startTime = time(0);
  while (true) {
    Endpoint::loop();
    time_t now = time(0);
    int elapse = now - startTime;
    if (now - startTime > 20) {
      manager.cleanTunnel(now);
      manager.showStatus(now, elapse);
      startTime = now;
    } else if (elapse < 0) {
      startTime = now;
    }
  }
  return 0;
}
