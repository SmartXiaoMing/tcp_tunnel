//
// Created by mabaiming on 18-8-15.
//

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <map>

#include "endpoint.h"
#include "endpoint_client.h"
#include "endpoint_client_tunnel.h"
#include "endpoint_server.h"
#include "frame.h"

using namespace std;

void onTunnelChanged(EndpointClient* endpoint, int event, const char* data, int size);
void onTrafficChanged(EndpointClient* endpoint, int event, const char* data, int size);
void onNewClientTraffic(EndpointServer* endpoint, int acfd);

class EndpointClientTraffic: public EndpointClient {
public:
  EndpointClientTraffic(int fd): EndpointClient(fd, onTrafficChanged), firstData(true) {}
  EndpointClientTraffic(const char* ip, int port): EndpointClient(ip, port, onTrafficChanged), firstData(true) {}
  Addr addr;
  bool firstData;
};

class Manager {
public:
  Manager(): tunnel(NULL), firstConnection(true) {}
  void prepare(const char* host, int port, const char* name) {
    while (tunnel == NULL) {
      if (!firstConnection) {
        ERROR("[Manager] prepare and wait 30 seoncds ...");
        sleep(30);
      }
      firstConnection = false;
      char ip[30];
      if (selectIp(host, ip, 29)) {
        INFO("[Manager] success to select tunnel server, host:%s -> ip:%s", host, ip);
        int fd = createClient(ip, port);
        if (fd < 0) {
          WARN("[Manager] failed to connect tunnel server %s:%d", ip, port);
        } else {
          INFO("[Manager] success to connect tunnel server %s:%d", ip, port);
          tunnel = new EndpointClientTunnel(fd, onTunnelChanged);
        }
      } else {
        ERROR("[Manager] failed to select tunnel server, host:%s, ip:%s", host, ip);
      }
      if (tunnel == NULL) {
        ERROR("[Manager] failed to prepare and wait 30 seoncds ...");
        sleep(30);
      } else {
        char data[256];
        int size = sprintf(data, "%s", name);
        tunnel->sendData(STATE_LOGIN, NULL, data, size);
      }
    }
  }
  EndpointServer* createTrafficServer(const char* ip, int port) {
    int fd = createServer(ip, port, 10000);
    if (fd < 0) {
      ERROR("[Manager] failed to create traffic server:%s:%d", ip, port);
      return NULL;
    }
    EndpointServer* trafficServer = new EndpointServer(fd, onNewClientTraffic);
    return trafficServer;
  }
  bool handleSshProto(EndpointClientTraffic* traffic, const char* data, int size) {
    if (tunnel == NULL) {
      return false;
    }
    if (strncmp(data, "SSH-", 4) == 0) {
      string target = "127.0.0.1:22";
      INFO("[traffic %s] guess the proto=SSH, make the target: %s", addrToStr(traffic->addr.b), target.c_str());
      tunnel->sendData(STATE_CONNECT, traffic->addr.b, target.data(), target.size());
      tunnel->sendData(STATE_DATA, traffic->addr.b, data, size);
      traffic->popReadData(size);
      traffic->addReadableSize(size);
      return true;
    }
    return false;
  }

  bool handleHttpProto(EndpointClientTraffic* traffic, const char* data, int size) {
    /*
        > GET http://www.baidu.com/ HTTP/1.1
        > Host: www.baidu.com
        > User-Agent: curl/7.61.1
        > Proxy-Connection: Keep-Alive
        >
    */
    // INFO("guess data:%.*s", size, data);
    if (tunnel == NULL) { // TODO
      return false;
    }
    const char* headerEnd = (const char*) memmem(data, size, "\r\n\r\n", 4);
    if (headerEnd == NULL) {
      INFO("no header end, exit");
      return false;
    }
    int headerSize = headerEnd - data + 4;

    const char* offset = data;
    int leftSize = headerSize - 2; // one \r\n left
    const char* protoEnd = (const char* )memmem(offset, leftSize, "\r\n", 2);
    if (protoEnd == NULL) {
      INFO("no proto end, exit");
      return false;
    }
    int protoSize = protoEnd - data;
    if (memmem(offset, protoSize, "HTTP/", 5) == NULL) {
      INFO("no proto HTTP/, exit");
      return false;
    }
    const char* methodStart = offset;
    const char* methodEnd = (const char* )memmem(offset, protoSize, " ", 1);
    if (methodEnd == NULL) {
      INFO("no proto method, %.*s, offset:%d, protoSize:%d", 20, methodStart, (int)(offset - data), protoSize);
      return false;
    }

    offset = protoEnd + 2;
    leftSize -= offset - data;
    const char* hostStart = (const char* )memmem(offset, leftSize, "Host:", 5);
    if (hostStart == NULL) {
      INFO("no proto host");
      return false;
    }
    hostStart += 5;
    offset = hostStart;
    leftSize -= offset - data;

    const char* hostEnd = (const char* )memmem(offset, leftSize, "\r\n", 2);
    if (hostEnd == NULL) {
      INFO("no proto host, %.*s, offset:%d, protoSize:%d", 20, hostStart, (int)(offset - data), protoSize);
      return false;
    }
    // trim host(hostStart, hostEnd)
    while (hostStart < hostEnd && *hostStart == ' ') {
      ++hostStart;
    }
    while (hostStart < hostEnd && *(hostEnd - 1) == ' ') {
      --hostEnd;
    }
    if (hostStart >= hostEnd) {
      return false;
    }

    string method(methodStart, methodEnd);
    string host(hostStart, hostEnd);
    INFO("[traffic %s] guess the proto=HTTP, parse the method=%s, target=%s",
         addrToStr(traffic->addr.b), method.c_str(), host.c_str());
    tunnel->sendData(STATE_CONNECT, traffic->addr.b, host.data(), host.size());
    if (method == "CONNECT") {
      if (size > headerSize > 0) {
        tunnel->sendData(STATE_DATA, traffic->addr.b, data + headerSize, size - headerSize);
        INFO("[traffic %s] send frame, state=DATA, dataSize=%d", addrToStr(traffic->addr.b), size - headerSize);
      }
      string response = "HTTP/1.1 200 Connection Established\r\n\r\n";
      traffic->writeData(response.data(), response.size());
    } else {
      tunnel->sendData(STATE_DATA, traffic->addr.b, data, size);
    }
    traffic->popReadData(size);
    traffic->addReadableSize(size);
    return true;
  }

  void resetTraffic() {
    map<Addr, EndpointClientTraffic*>::iterator it = trafficMap.begin();
    for (; it != trafficMap.end(); ++it) {
      it->second->writeData(NULL, 0);
    }
    trafficMap.clear();
  }
  EndpointClientTunnel* tunnel;
  string targetAddress;
  map<Addr, EndpointClientTraffic*> trafficMap;
  bool firstConnection;
};

Manager manager;

void onNewClientTraffic(EndpointServer* endpoint, int acfd) {
  EndpointServer* serverTraffic = (EndpointServer*) endpoint;
  EndpointClientTraffic* traffic = new EndpointClientTraffic(acfd);
  traffic->addr = sockFdToAddr(acfd);
  map<Addr, EndpointClientTraffic*>::iterator it = manager.trafficMap.find(traffic->addr);
  if (it != manager.trafficMap.end()) {
    ERROR("!!!!! addr exists %s", addrToStr(traffic->addr.b));
  }
  manager.trafficMap[traffic->addr] = traffic;
}

void onTrafficChanged(EndpointClient* endpoint, int event, const char* data, int size) {
  EndpointClientTraffic* traffic = (EndpointClientTraffic*) endpoint;
  if (event == EVENT_ERROR || event == EVENT_CLOSED) {
    INFO("[traffic %s] error=%d, so", addrToStr(traffic->addr.b), event);
    INFO("[tunnel %s] send frame, state=CLOSE", addrToStr(traffic->addr.b));
    manager.tunnel->sendData(STATE_CLOSE, traffic->addr.b, NULL, 0);
    manager.trafficMap.erase(traffic->addr);
    return;
  }
  if (event == EVENT_READ) {
    if (size > 0) {
      INFO("[traffic %s] read dataSize=%d", addrToStr(traffic->addr.b), size);
      if (traffic->firstData) {
        traffic->firstData = false;
        if (!manager.targetAddress.empty()) {
          manager.tunnel->sendData(STATE_CONNECT, traffic->addr.b,
            manager.targetAddress.data(), manager.targetAddress.size());
          manager.tunnel->sendData(STATE_DATA, traffic->addr.b, data, size);
          INFO("[tunnel %s] send frame, state=DATA, dataSize=%d", addrToStr(traffic->addr.b), size);
          traffic->popReadData(size);
        } else {
          bool success = manager.handleSshProto(traffic, data, size);
          if (!success) {
            success = manager.handleHttpProto(traffic, data, size);
          }
          if (!success) {
            INFO("[traffic %s] guess proto failed, so close it", addrToStr(traffic->addr.b));
            traffic->writeData(NULL, 0);
            manager.trafficMap.erase(traffic->addr);
          }
        }
      } else {
        manager.tunnel->sendData(STATE_DATA, traffic->addr.b, data, size);
        INFO("[tunnel %s] send frame, state=DATA, dataSize=%d", addrToStr(traffic->addr.b), size);
        traffic->popReadData(size);
      }
    } else { // size == 0, read eof
      INFO("[traffic %s] read EOF, so", addrToStr(traffic->addr.b));
      manager.tunnel->sendData(STATE_CLOSE, traffic->addr.b, NULL, 0);
      INFO("[tunnel %s] send frame, state=CLOSE", addrToStr(traffic->addr.b));
    }
    return;
  }
  if (event == EVENT_WRITTEN) {
    char b[20];
    int len = sprintf(b, "%d", size) + 1;
    manager.tunnel->sendData(STATE_ACK, traffic->addr.b, b, len);
    INFO("[traffic %s] write dataSize=%d", addrToStr(traffic->addr.b), size);
    INFO("[tunnel %s] send frame, state=ACK, size=%d", addrToStr(traffic->addr.b), size);
    return;
  }
}

void onTunnelChanged(EndpointClient* endpoint, int event, const char* data, int size) {
  if (event == EVENT_ERROR || event == EVENT_CLOSED) {
    INFO("[Manager] tunnel disconnected");
    manager.resetTraffic();
    manager.tunnel = NULL;
    return;
  }
  EndpointClientTunnel* tunnel = (EndpointClientTunnel*) endpoint;
  if (event == EVENT_READ) {
    Frame frame;
    while (tunnel->parseFrame(frame) > 0) {
      if (frame.state == STATE_RESET) {
        INFO("[tunnel %s] recv frame, state=RESET, peer is broken, so reset all traffic", addrToStr(frame.addr.b));
        manager.resetTraffic();
        continue;
      }
      if (frame.state == STATE_CONNECT) {
        INFO("[tunnel %s] recv frame, state=CONNECT, target=%s", addrToStr(frame.addr.b), frame.message.c_str());
        char host[frame.message.size() + 1];
        int port = 80;
        if (parseIpPort(frame.message, host, &port)) {
          char ip[30];
          if (selectIp(host, ip, 29) == NULL) {
            INFO("[traffic %s] failed to resove host=%s, so", addrToStr(frame.addr.b), host);
            INFO("[tunnel %s] send frame, state=CLOSE", addrToStr(frame.addr.b));
            tunnel->sendData(STATE_CLOSE, frame.addr.b, NULL, 0);
            break;
          }
          INFO("[traffic %s] connect to %s:%d", addrToStr(frame.addr.b), ip, port);
          map<Addr, EndpointClientTraffic*>::iterator it2 = manager.trafficMap.find(frame.addr);
          if (it2 != manager.trafficMap.end()) {
            ERROR("!!!! addr exsists:%s !!!", addrToStr(frame.addr.b));
          }
          EndpointClientTraffic* endpointTraffic = new EndpointClientTraffic(ip, port);
          endpointTraffic->addr = frame.addr;
          endpointTraffic->firstData = false;
          manager.trafficMap[frame.addr] = endpointTraffic;
        } else {
          INFO("[traffic %s] failed to parse host=%s, so", addrToStr(frame.addr.b), frame.message.c_str());
          INFO("[tunnel %s] send frame, state=CLOSE", addrToStr(frame.addr.b));
          tunnel->sendData(STATE_CLOSE, frame.addr.b, NULL, 0);
        }
        continue;
      }
      map<Addr, EndpointClientTraffic*>::iterator it = manager.trafficMap.find(frame.addr);
      if (it == manager.trafficMap.end()) { // must not exist
        if (frame.state == STATE_DATA) {
          INFO("[tunnel %s] recv frame, state=DATA, addr does not exists, so", addrToStr(frame.addr.b));
          INFO("[tunnel %s] send frame, state=CLOSE", addrToStr(frame.addr.b));
          tunnel->sendData(STATE_CLOSE, frame.addr.b, NULL, 0);
        } else {
          INFO("[tunnel %s] recv frame, state=%s, addr does not exists, so skip it",
               addrToStr(frame.addr.b), Frame::stateToStr(frame.state));
        }
        continue;
      }
      EndpointClientTraffic* traffic = it->second;
      if (frame.state == STATE_DATA) {
        INFO("[tunnel %s] recv frame, state=DATA, dataSize=%d, so", addrToStr(frame.addr.b), (int)frame.message.size());
        traffic->writeData(frame.message.data(), frame.message.size());
        INFO("[traffic %s] send data, dataSize=%d", addrToStr(frame.addr.b), (int)frame.message.size());
      } else if (frame.state == STATE_ACK) {
        int size = 0;
        sscanf(frame.message.c_str(), "%d", &size);
        INFO("[tunnel %s] recv frame, state=ACK, size=%d", addrToStr(frame.addr.b), size);
        traffic->addReadableSize(size);
      } else if (frame.state == STATE_CLOSE) {
        INFO("[tunnel %s] recv frame, state=CLOSE, so", addrToStr(frame.addr.b));
        traffic->writeData(NULL, 0);
        INFO("[traffic %s] close", addrToStr(frame.addr.b));
        manager.trafficMap.erase(it);
      }
    }
    return;
  }
  // if (event == EVENT_WRITTEN) { // DO NOTHING
}

int main(int argc, char** argv) {
  const char* dBrokerHost = "127.0.0.1";
  int dBrokerPort = 8120;
  const char* dServerIp = "127.0.0.1";
  int dServerPort = 0;
  const char* dName = "anonymous";
  int dLevel = 0;
  const char* dTargetAddress = "guess";

  const char* brokerHost = dBrokerHost;
  int brokerPort = dBrokerPort;
  const char* serverIp = dServerIp;
  int serverPort = dServerPort;
  const char* name = dName;
  const char* targetAddress = dTargetAddress;
  logLevel = dLevel;

  for (int i = 1; i < argc; i += 2) {
    if (strcmp(argv[i], "--brokerHost") == 0 && i + 1 < argc) {
      brokerHost = argv[i + 1];
    } else if (strcmp(argv[i], "--brokerPort") == 0 && i + 1 < argc) {
      brokerPort = atoi(argv[i + 1]);
    } else if (strcmp(argv[i], "--serverIp") == 0 && i + 1 < argc) {
      serverIp = argv[i + 1];
    } else if (strcmp(argv[i], "--serverPort") == 0 && i + 1 < argc) {
      serverPort = atoi(argv[i + 1]);
    } else if (strcmp(argv[i], "--targetAddress") == 0 && i + 1 < argc) {
      targetAddress = argv[i + 1];
    } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
      name = argv[i + 1];
    } else if (strncmp(argv[i], "--v", 3) == 0) {
      logLevel = atoi(argv[i + 1]);
    } else {
      int ret = strcmp(argv[i], "--help");
      if (ret != 0) {
        printf("\nunknown option: %s\n", argv[i]);
      }
      printf("usage: %s [options]\n\n", argv[0]);
      printf("  --brokerHost domain.com  the server host, default %s\n", dBrokerHost);
      printf("  --brokerPort (0~65535)   the server port, default %d\n", dBrokerPort);
      printf("  --serverIp 0.0.0.0       the server ip, default disabled\n");
      printf("  --serverPort (0~65535)   the server port, default disabled\n");
      printf("  --name name              the name, default %s\n", dName);
      printf("  --targetAddress ip:port  the name, default %s\n", dTargetAddress);
      printf("  --v [0-5]                set log level, 0-5 means OFF, ERROR, WARN, INFO, DEBUG, default %d\n", dLevel);
      printf("  --help                   show the usage then exit\n");
      printf("\n");
      printf("version 1.0, report bugs to SmartXiaoMing(95813422@qq.com)\n");
      exit(ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
    }
  }

  Endpoint::init();
  if (targetAddress != NULL && targetAddress != dTargetAddress) {
    manager.targetAddress = targetAddress;
  } else {
    targetAddress = "";
  }
  if (serverPort > 0 && manager.createTrafficServer(serverIp, serverPort) == NULL) {
    exit(1);
  }
  int startTime = time(0);
  while (true) {
    manager.prepare(brokerHost, brokerPort, name);
    Endpoint::loop();
    int now = time(0);
    if (now - startTime > 120 || now - startTime < 0) {
      manager.tunnel->sendData(STATE_NONE, NULL, NULL, 0);
      startTime = now;
    }
  }
  return 0;
}
