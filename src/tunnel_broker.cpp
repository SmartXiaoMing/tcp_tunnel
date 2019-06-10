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

int gTunnelId = 1;
void onNewClientTunnel(EndpointServer* endpoint, int acfd);
void onTunnelChanged(EndpointClient* endpoint, int event, const char* data, int size);

class EndpointClientTunnelPeer: public EndpointClientTunnel {
public:
  EndpointClientTunnelPeer(int fd): EndpointClientTunnel(fd, onTunnelChanged), name(), peerTunnel(NULL) {
    fdToPeerAddr(fd, remoteAddr);
    time(&lastTs);
    id = gTunnelId;
    ++gTunnelId;
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
  map<int, EndpointClientTunnelPeer*> sourceTunnelMap;
  time_t lastTs;
  int readSize;
  int writtenSize;
  char lastTime[64];
  char remoteAddr[30];
  int id;
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

  void breakTunnel(EndpointClientTunnelPeer* tunnel) {
    Addr tunnelAddr(tunnel->id);
    if (tunnel->peerTunnel != NULL) {
      tunnel->peerTunnel->sendData(STATE_RESET, &tunnelAddr, NULL, 0);
      tunnel->peerTunnel->sourceTunnelMap.erase(tunnel->id);
      tunnel->peerTunnel = NULL;
    }
    if (!tunnel->sourceTunnelMap.empty()) {
      map<int, EndpointClientTunnelPeer*>::iterator it2 = tunnel->sourceTunnelMap.begin();
      for (; it2 != tunnel->sourceTunnelMap.end(); ++it2) {
        it2->second->sendData(STATE_RESET, &tunnelAddr, NULL, 0);
        it2->second->peerTunnel = NULL;
      }
      tunnel->sourceTunnelMap.clear();
    }
  }

  void cleanTunnel(time_t now) {
    map<string, EndpointClientTunnelPeer*>::iterator it = tunnelPeerMap.begin();
    for (; it != tunnelPeerMap.end(); ) {
      if (now - it->second->lastTs > 300) {
        EndpointClientTunnelPeer* tunnel = it->second;
        ERROR("[tunnel] clean %s\t%s[%s] <- ", it->first.c_str(), it->second->remoteAddr, it->second->getLastTime());
        tunnel->writeData(NULL, 0);
        breakTunnel(tunnel);
        tunnelPeerMap.erase(it++);
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
      if (tunnel->peerTunnel != NULL) {
        ERROR("%s ---> %s", tunnel->name.c_str(), tunnel->peerName.c_str());
      } else {
        ERROR("%s -x-> %s", tunnel->name.c_str(), tunnel->peerName.c_str());
      }
    }
    ERROR("Follower");
    it = tunnelPeerMap.begin();
    for (; it != tunnelPeerMap.end(); ++it) {
      EndpointClientTunnelPeer* tunnel = it->second;
      ERROR("%s[%zd]: %s %s elapse:%d, read:%d, write:%d",
            tunnel->name.c_str(), tunnel->sourceTunnelMap.size(), tunnel->remoteAddr, tunnel->getLastTime(),
            elapse, tunnel->readSize, tunnel->writtenSize);
      tunnel->readSize = 0;
      tunnel->writtenSize = 0;
      map<int, EndpointClientTunnelPeer*>::iterator srcIt = tunnel->sourceTunnelMap.begin();
      for (int i = 0; srcIt != tunnel->sourceTunnelMap.end(); ++i, ++srcIt) {
        const char* lineStart = i < tunnel->sourceTunnelMap.size() - 1 ? "├─" : "└─";
        EndpointClientTunnelPeer* srcTunnel = srcIt->second;
        ERROR(" %s %s: %s %s", lineStart, srcTunnel->name.c_str(), srcTunnel->remoteAddr, srcTunnel->getLastTime());
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
      return; // unassigned name
    }
    map<string, EndpointClientTunnelPeer*>::iterator it = manager.tunnelPeerMap.find(tunnel->name);
    if (it == manager.tunnelPeerMap.end()) {
      return;
    }
    manager.breakTunnel(tunnel);
    manager.tunnelPeerMap.erase(it);
    return;
  }
  if (event == EVENT_READ) {
    tunnel->readSize += size;
    time(&tunnel->lastTs);
    Frame frame;
    while (tunnel->parseFrame(frame) > 0) {
      if (frame.state == STATE_LOGIN) {
        INFO("[tunnel] recv frame, state=LOGIN, message=%s, addr=%s, time=%s",
             frame.message.c_str(), tunnel->remoteAddr, tunnel->getLastTime());
        string name = manager.getBetween(frame.message, "name=", "&");
        string peerName = manager.getBetween(frame.message, "peerName=", "&");
        INFO("[tunnel] parse message, name:%s, peerName:%s", name.c_str(), peerName.c_str());
        map<string, EndpointClientTunnelPeer *>::iterator it = manager.tunnelPeerMap.find(name);
        if (it == manager.tunnelPeerMap.end()) {
          tunnel->name = name;
          tunnel->peerName = peerName;
          map<string, EndpointClientTunnelPeer *>::iterator targetIt = manager.tunnelPeerMap.find(peerName);
          if (targetIt != manager.tunnelPeerMap.end()) {
            tunnel->peerTunnel = targetIt->second;
            targetIt->second->sourceTunnelMap[tunnel->id] = tunnel;
            INFO("[tunnel %s] --> %s", name.c_str(), tunnel->peerTunnel->name.c_str());
          }
          map<string, EndpointClientTunnelPeer*>::iterator sourceIt = manager.tunnelPeerMap.begin();
          for (; sourceIt != manager.tunnelPeerMap.end(); ++sourceIt) {
            EndpointClientTunnelPeer* sourceTunnel = sourceIt->second;
            if (sourceTunnel->peerTunnel == NULL && sourceTunnel->peerName == name) {
              sourceTunnel->peerTunnel = tunnel;
              tunnel->sourceTunnelMap[sourceTunnel->id] = sourceTunnel;
              INFO("[tunnel %s] --> %s", sourceTunnel->name.c_str(), tunnel->name.c_str());
            }
          }
          manager.tunnelPeerMap[name] = tunnel;
        } else {
          // ERROR
          tunnel->writeData(NULL, 0); // close the tunnel client
          manager.tunnelPeerMap.erase(it);
          return;
        }
      } else if (frame.state == STATE_NONE) {
        INFO("[tunnel %s] recv frame, state=NONE, addr=%s, time=%s",
             tunnel->name.c_str(), tunnel->remoteAddr, tunnel->getLastTime());
        continue;
      } else {
        INFO("[tunnel %s %s] recv frame, state=%s, dataSize=%zd, tid:%d, peerTunnel:%p",
             tunnel->name.c_str(), addrToStr(frame.addr.b), Frame::stateToStr(frame.state), frame.message.size(), frame.addr.tid, tunnel->peerTunnel);
        if (frame.addr.tid == 0) { // link started from that
          if (tunnel->peerTunnel == NULL) {
            tunnel->sendData(STATE_CLOSE, &frame.addr, NULL, 0);
            INFO("[tunnel %s %s] send frame, state=CLOSE", tunnel->name.c_str(), addrToStr(frame.addr.b));
          } else {
            Addr addr = frame.addr;
            addr.tid = tunnel->id;
            tunnel->peerTunnel->sendData(frame.state, &addr, frame.message.data(), frame.message.size());
            INFO("[tunnel %s %s] send frame, state=%s, dataSize=%d", tunnel->peerTunnel->name.c_str(),
                 addrToStr(frame.addr.b), Frame::stateToStr(frame.state), (int) frame.message.size());
          }
        } else {
          map<int, EndpointClientTunnelPeer*>::iterator tunnelIt = tunnel->sourceTunnelMap.find(frame.addr.tid);
          if (tunnelIt == tunnel->sourceTunnelMap.end()) {
            tunnel->sendData(STATE_CLOSE, &frame.addr, NULL, 0);
            INFO("[tunnel %s %s] send frame, state=CLOSE", tunnel->name.c_str(), addrToStr(frame.addr.b));
          } else {
            Addr addr = frame.addr;
            addr.tid = 0;
            EndpointClientTunnelPeer* tunnelPeer = tunnelIt->second;
            tunnelPeer->sendData(frame.state, &addr, frame.message.data(), frame.message.size());
            INFO("[tunnel %s %s] send frame, state=%s, dataSize=%d", tunnelPeer->name.c_str(),
                 addrToStr(frame.addr.b), Frame::stateToStr(frame.state), (int) frame.message.size());
          }
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
    if (now - startTime > 60) {
      manager.cleanTunnel(now);
      manager.showStatus(now, elapse);
      startTime = now;
    } else if (elapse < 0) {
      startTime = now;
    }
  }
  return 0;
}
