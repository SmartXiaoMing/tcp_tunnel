//
// Created by mabaiming on 18-8-15.
//

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <map>

#include "constr.h"
#include "endpoint.h"
#include "endpoint_client.h"
#include "endpoint_client_tunnel.h"
#include "endpoint_server.h"
#include "frame.h"

using namespace std;

void onTunnelChanged(EndpointClient* endpoint, int event, const char* data, int size);
void onTrafficChanged(EndpointClient* endpoint, int event, const char* data, int size);
void onNewClientTraffic(EndpointServer* endpoint, int acfd);

uint32_t gSession = 0;

struct Addr {
  string name;
  uint32_t session;
  Addr() {}
  Addr(const string& name_, uint32_t s): name(name_), session(s) {}
  bool operator < (const Addr& addr) const {
    int d = session - addr.session;
    if (d != 0) {
      return d;
    }
    return name < addr.name;
  }
  bool operator == (const Addr& addr) const {
    return session == addr.session && name == addr.name;
  }
};

class EndpointClientTraffic: public EndpointClient {
public:
  static const int ProtoUnknown = 0;
  static const int ProtoSsh = 1;
  static const int ProtoHttp = 2;
  static const int ProtoHttpTunnel = 3; // CONNECT home.netscape.com:443 HTTP/1.0 see https://tools.ietf.org/html/draft-luotonen-web-proxy-tunneling-01

  EndpointClientTraffic(const string& name, int fd): EndpointClient(fd, onTrafficChanged), packageNumber(0), proto(ProtoUnknown), stream(Frame::StreamRequest) {
    gSession++;
    addr.session = gSession;
    addr.name = name;
    this->name = name;
    INFO("[traffic create] traffic %s#%d come from local", addr.name.c_str(), addr.session);
  }
  EndpointClientTraffic(const string& name, const Addr& addr): EndpointClient(onTrafficChanged), packageNumber(0), proto(ProtoUnknown), stream(Frame::StreamResponse) {
    this->addr = addr;
    this->name = name;
    INFO("[traffic create] traffic %s#%d come from tunnel", addr.name.c_str(), addr.session);
  }
  static int guessProto(const char* data, int size) {
    Constr content(data, size);
    if (content.startsWith("CONNECT ")) {
      return ProtoHttpTunnel;
    }
    if (content.startsWith("SSH-")) {
      return ProtoSsh;
    }
    if (content.startsWith("GET ")
        || content.startsWith("POST ")
        || content.startsWith("PUT ")
        || content.startsWith("DELETE ")
        || content.startsWith("HEAD ")) {
      return ProtoHttp;
    }
    return ProtoUnknown;
  }
  Addr addr;
  bool packageNumber;
  int proto;
  int stream;
  string name;
};

class Manager {
public:
  Manager(): tunnel(NULL), firstConnection(true) {}
  void prepare(const char* host, int port, const char* name, const char* peerName) {
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
        int size = sprintf(data, "name=%s&peerName=%s", name, peerName);
        string message(data, size);
        INFO("[tunnel login] %s > %s, message: %s", name, peerName, message.c_str());
        Frame frame = makeFrame(NULL, STATE_TUNNEL_LOGIN, message);
        tunnel->sendData(frame);
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

  bool handleProtoSsh(EndpointClientTraffic* traffic, const char* data, int size) {
    if (tunnel == NULL) {
      return false;
    }
    string target = "127.0.0.1:22";
    INFO("[traffic proto] %s guess the proto=SSH, make the target: %s", traffic->addr.name.c_str(), target.c_str());

    Frame frame = makeFrame(traffic, STATE_TRAFFIC_CONNECT, target);
    INFO("[traffic connect] %s#%d > %s, dataSize: %zd",
         frame.from.c_str(), frame.session, frame.to.c_str(), frame.message.size());
    tunnel->sendData(frame);
    frame = makeFrame(traffic, STATE_TRAFFIC_DATA, string(data, size));
    INFO("[traffic data] %s#%d > %s, dataSize: %zd~~~",
         frame.from.c_str(), frame.session, frame.to.c_str(), frame.message.size());
    tunnel->sendData(frame);
    traffic->popReadData(size);
    traffic->addReadableSize(size);
    return true;
  }

  bool handleProtoHttpTunnel(EndpointClientTraffic* traffic, const char* data, int size) {
    /*
     * see https://tools.ietf.org/html/draft-luotonen-web-proxy-tunneling-01
         CONNECT home.netscape.com:443 HTTP/1.0
         User-agent: Mozilla/4.0
         Proxy-authorization: basic dGVzdDp0ZXN0
    */
    // INFO("guess data:%.*s", size, data);
    if (tunnel == NULL) { // TODO
      return false;
    }
    static Constr headerEnd("\r\n\r\n");
    Constr content(data, size);
    int headerEndPos = content.indexOf(headerEnd);
    if (headerEndPos < 0) {
      ERROR("invalid http tunnel proto:%.*s, exit", content.size, content.data);
      return false;
    }
    Constr requestLine;
    content.readUtil("\r\n", requestLine);
    if (requestLine.size == 0) {
      ERROR("invalid http tunnel proto:%.*s, no requestLine, exit", content.size, content.data);
      return false;
    }
    INFO("parse proto:%.*s", requestLine.size, requestLine.data);
    Constr three[3];
    if (requestLine.split(" ", three, 3) < 3) {
      INFO("invalid proto:%.*s, exit", requestLine.size, requestLine.data);
      return false;
    }
    Constr& method = three[0];
    Constr& host = three[1];
    Constr& version = three[2];
    if (!version.startsWith("HTTP/")) {
      INFO("invalid http tunnel proto, httpVersion:%.*s, exit", version.size, version.data);
      return false;
    }
    Frame frame = makeFrame(traffic, STATE_TRAFFIC_CONNECT, string(host.data, host.size));
    INFO("[traffic connect] %s#%d > %s, dataSize: %zd~~~",
         frame.from.c_str(), frame.session, frame.to.c_str(), frame.message.size());
    tunnel->sendData(frame);
    int leftSize = size - headerEndPos - headerEnd.size;
    if (leftSize > 0) {
      frame = makeFrame(traffic, STATE_TRAFFIC_DATA, string(data + size - leftSize, leftSize));
      INFO("[traffic data] %s#%d > %s, dataSize: %zd~~~",
           frame.from.c_str(), frame.session, frame.to.c_str(), frame.message.size());
      tunnel->sendData(frame);
    }
    string response = "HTTP/1.1 200 Connection Established\r\n\r\n";
    traffic->writeData(response.data(), response.size());
    traffic->popReadData(size);
    traffic->addReadableSize(size);
    return true;
  }

  bool handleProtoHttp(EndpointClientTraffic* traffic, const char* data, int size) {
    /* see https://tools.ietf.org/html/rfc2616#page-46
     * The absoluteURI form is REQUIRED when the request is being made to a
     * proxy. The proxy is requested to forward the request or service it
     * from a valid cache, and return the response. Note that the proxy MAY
     * forward the request on to another proxy or directly to the server
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
    Constr content(data, size);
    static Constr lineEnd("\r\n");
    Constr requestLine;
    content.readUtil(lineEnd, requestLine);
    if (requestLine.size == 0) {
      INFO("not http proto, exit");
      return false;
    }
    INFO("parse proto:%.*s", requestLine.size, requestLine.data);
    Constr three[3];
    if (requestLine.split(" ", three, 3) < 3) {
      INFO("invalid proto:%.*s, exit", requestLine.size, requestLine.data);
      return false;
    }
    Constr& method = three[0];
    Constr& url = three[1];
    Constr& version = three[2];
    if (!version.startsWith("HTTP/")) {
      INFO("invalid httpVersion:%.*s, exit", version.size, version.data);
      return false;
    }
    Constr httpsSchema("https://");
    Constr schema("://");
    int schemaPos = url.indexOf(schema);
    if (schemaPos >= 0) {
      url.popFront(schemaPos + schema.size);
    }
    int slashPos = url.indexOf("/");
    if (slashPos < 0) {
      INFO("invalid proto:%.*s, exit", url.size, url.data);
      return false;
    }
    Constr host(url.data, slashPos);;
    Frame frame = makeFrame(traffic, STATE_TRAFFIC_CONNECT, string(host.data, host.size));
    INFO("[traffic connect] %s#%d > %s, dataSize: %zd~~~",
         frame.from.c_str(), frame.session, frame.to.c_str(), frame.message.size());
    tunnel->sendData(frame);
    frame = makeFrame(traffic, STATE_TRAFFIC_DATA, string(data, size));
    INFO("[traffic data] %s#%d > %s, dataSize: %zd~~~",
         frame.from.c_str(), frame.session, frame.to.c_str(), frame.message.size());
    tunnel->sendData(frame);
    traffic->popReadData(size);
    traffic->addReadableSize(size);
    return true;
  }

  void resetResponseTraffic(const string& name) {
    if (name == peerName) {
      resetAllTraffic();
    } else {
      map<Addr, EndpointClientTraffic *>::iterator it = responseTrafficMap.begin();
      for (; it != responseTrafficMap.end();) {
        if (it->first.name == name) {
          it->second->writeData(NULL, 0);
          it = responseTrafficMap.erase(it);
        } else {
          ++it;
        }
      }
    }
  }

  void resetAllTraffic() {
    responseTrafficMap.clear();
    map<uint32_t, EndpointClientTraffic*>::iterator it = requestTrafficMap.begin();
    for (; it != requestTrafficMap.end(); ++it) {
        it->second->writeData(NULL, 0);
    }
    requestTrafficMap.clear();
  }

  Frame makeFrame(EndpointClientTraffic* traffic, uint8_t state, const string& message) {
    Frame frame;
    if (traffic != NULL) {
      frame.from = traffic->addr.name;
      frame.to = peerName;
      frame.stream = traffic->stream;
      frame.session = traffic->addr.session;
    } else {
      frame.from = "";
      frame.to = "";
      frame.stream = Frame::StreamRequest;
      frame.session = 0;
    }
    frame.state = state;
    frame.message = message;
    return frame;
  }

  EndpointClientTunnel* tunnel;
  string targetAddress;
  string name;
  string peerName; // TODO
  map<uint32_t, EndpointClientTraffic*> requestTrafficMap;
  map<Addr, EndpointClientTraffic*> responseTrafficMap;
  bool firstConnection;
  int peerTunnelId;
  int tunnelId;
  const char* password;
};

Manager manager;

void onNewClientTraffic(EndpointServer* endpoint, int acfd) {
  EndpointServer* serverTraffic = (EndpointServer*) endpoint;
  EndpointClientTraffic* traffic = new EndpointClientTraffic(manager.name, acfd);
  map<uint32_t, EndpointClientTraffic*>::iterator it = manager.requestTrafficMap.find(traffic->addr.session);
  if (it != manager.requestTrafficMap.end()) {
    ERROR("!!!!! addr exists %s, session:%d", traffic->addr.name.c_str(), traffic->addr.session);
  }
  manager.requestTrafficMap[traffic->addr.session] = traffic;
  INFO("[local accept] accept %s#%d", traffic->addr.name.c_str(), traffic->addr.session);
}

void onTrafficChanged(EndpointClient* endpoint, int event, const char* data, int size) {
  EndpointClientTraffic* traffic = (EndpointClientTraffic*) endpoint;
  if (event == EVENT_ERROR || event == EVENT_CLOSED) {
    Frame frame = manager.makeFrame(traffic, STATE_TRAFFIC_CLOSE, "traffic error or closed");
    manager.tunnel->sendData(frame);
    INFO("[traffic local close] %s#%d > %s, message: %s",
      frame.from.c_str(), frame.session, frame.to.c_str(), frame.message.c_str());
    manager.requestTrafficMap.erase(traffic->addr.session);
    return;
  }
  if (event == EVENT_READ) {
    if (size > 0) {
      if (traffic->packageNumber == 0) {
        traffic->packageNumber = 1;
        if (!manager.targetAddress.empty() && manager.targetAddress != "guess") {
          Frame frame = manager.makeFrame(traffic, STATE_TRAFFIC_CONNECT, manager.targetAddress);
          manager.tunnel->sendData(frame);
          INFO("[traffic local connect] %s#%d > %s, dataSize: %zd",
               frame.from.c_str(), frame.session, frame.to.c_str(), frame.message.size());
          frame = manager.makeFrame(traffic, STATE_TRAFFIC_DATA, string(data, size));
          manager.tunnel->sendData(frame);
          INFO("[traffic local data] %s#%d > %s, dataSize: %d",
               frame.from.c_str(), frame.session, frame.to.c_str(), size);
          traffic->popReadData(size);
        } else {
          int proto = EndpointClientTraffic::guessProto(data, size);
          INFO("guess proto:%d, for dataSize=%d, data=%.*s", proto, size, size, data);
          bool success = false;
          if (proto == EndpointClientTraffic::ProtoSsh) {
            success = manager.handleProtoSsh(traffic, data, size);
          } else if (proto == EndpointClientTraffic::ProtoHttpTunnel) {
            success = manager.handleProtoHttpTunnel(traffic, data, size);
          } else if (proto == EndpointClientTraffic::ProtoHttp) {
            success = manager.handleProtoHttp(traffic, data, size);
          }
          if (!success) {
            INFO("[traffic local parse] failed to guess proto");
            traffic->writeData(NULL, 0);
            manager.requestTrafficMap.erase(traffic->addr.session);
          }
        }
      } else {
        Frame frame = manager.makeFrame(traffic, STATE_TRAFFIC_DATA, string(data, size));
        INFO("[traffic local data] %s#%d > %s, dataSize: %d",
            frame.from.c_str(), frame.session, frame.to.c_str(), size);
        manager.tunnel->sendData(frame);
        traffic->popReadData(size);
      }
    } else { // size == 0, read eof
      Frame frame = manager.makeFrame(traffic, STATE_TRAFFIC_CLOSE, "read EOF");
      INFO("[traffic local eof] %s#%d > %s, read EOF",
          frame.from.c_str(), frame.session, frame.to.c_str());
      manager.tunnel->sendData(frame);
    }
    return;
  }
  if (event == EVENT_WRITTEN) {
    char b[20];
    int len = sprintf(b, "%d", size) + 1;
    Frame frame = manager.makeFrame(traffic, STATE_TRAFFIC_ACK, b);
    manager.tunnel->sendData(frame);
    INFO("[traffic local ack] %s#%d > %s, dataSize: %d",
         frame.from.c_str(), frame.session, frame.to.c_str(), size);
    return;
  }
}

void onTunnelChanged(EndpointClient* endpoint, int event, const char* data, int size) {
  if (event == EVENT_ERROR || event == EVENT_CLOSED) {
    INFO("[tunnel local error] tunnel disconnected");
    manager.resetAllTraffic();
    manager.tunnel = NULL;
    return;
  }
  EndpointClientTunnel* tunnel = (EndpointClientTunnel*) endpoint;
  if (event == EVENT_READ) {
    Frame frame;
    while (tunnel->parseFrame(frame) > 0) {
      if (frame.state == STATE_TUNNEL_ERROR) {
        ERROR("[tunnel remote error] %s#%d > %s, dataSize: %d",
             frame.from.c_str(), frame.session, frame.to.c_str(), size);
        manager.resetResponseTraffic(frame.from);
        continue;
      }
      if (frame.state == STATE_TRAFFIC_CONNECT) {
        INFO("[traffic remote connect] %s#%d > %s, message: %s",
             frame.from.c_str(), frame.session, frame.to.c_str(), frame.message.c_str());
        char host[frame.message.size() + 1];
        int port = 80;
        if (parseIpPort(frame.message, host, &port)) {
          char ip[30];
          if (selectIp(host, ip, 29) == NULL) {
            frame.setReply(STATE_TRAFFIC_CLOSE, "failed to resolve host");
            ERROR("[traffic remote connect] %s#%d > %s, failed to resolve host:%s",
                 frame.from.c_str(), frame.session, frame.to.c_str(), host);
            tunnel->sendData(frame);
            break;
          } else {
            INFO("[traffic remote connect] %s#%d > %s, success to resolve host:%s, ip:%s, port:%d",
                 frame.from.c_str(), frame.session, frame.to.c_str(), host, ip, port);
          }
          Addr addr(frame.from, frame.session);
          map<Addr, EndpointClientTraffic*>::iterator it2 = manager.responseTrafficMap.find(addr);
          if (it2 != manager.responseTrafficMap.end()) {
            INFO("[traffic error] %s > %s#%d, error, the session exists already!!!",
                 frame.from.c_str(), frame.to.c_str(), frame.session);
          }
          EndpointClientTraffic* endpointTraffic = new EndpointClientTraffic(manager.name, addr);
          if (endpointTraffic->createClient(ip, port)) {
            endpointTraffic->addr = addr; // TODO
            endpointTraffic->packageNumber = 1;
            manager.responseTrafficMap[addr] = endpointTraffic;
            INFO("[traffic connect] %s > %s, session: %d, success to make a connection host:%s, ip:%s, port:%d",
                 frame.from.c_str(), frame.to.c_str(), frame.session, host, ip, port);
          } else {
            frame.setReply(STATE_TRAFFIC_CLOSE, "failed to connect");
            INFO("[traffic connect] %s >> %s, session: %d, failed to connect host:%s, ip:%s, port:%d",
                 frame.from.c_str(), frame.to.c_str(), frame.session, host, ip, port);
            tunnel->sendData(frame);
          }
        } else {
          frame.setReply(STATE_TRAFFIC_CLOSE, "invalid config");
          INFO("[traffic connect] %s >> %s, session: %d, invalid config: %s",
               frame.from.c_str(), frame.to.c_str(), frame.session, frame.message.c_str());
          tunnel->sendData(frame);
        }
        continue;
      }
      if (frame.stream == Frame::StreamRequest) {
        Addr addr(frame.from, frame.session);
        if (frame.state == STATE_TRAFFIC_DATA) {
          map<Addr, EndpointClientTraffic*>::iterator it = manager.responseTrafficMap.find(addr);
          INFO("[traffic data] %s#%d > %s, dataSize: %zd",
               frame.from.c_str(), frame.session, frame.to.c_str(), frame.message.size());
          if (it != manager.responseTrafficMap.end()) {
            it->second->writeData(frame.message.data(), frame.message.size());
          } else {
            frame.setReply(STATE_TRAFFIC_CLOSE, "failed to handle data, traffic not exists");
            INFO("[traffic error] %s >> %s, session: %d, dataSize: %zd, failed to handle data, traffic not exists",
                 frame.from.c_str(), frame.to.c_str(), frame.session, frame.message.size());
            tunnel->sendData(frame);
          }
        } else if (frame.state == STATE_TRAFFIC_ACK) {
          map<Addr, EndpointClientTraffic*>::iterator it = manager.responseTrafficMap.find(addr);
          int size = 0;
          sscanf(frame.message.c_str(), "%d", &size);
          INFO("[traffic ack] %s > %s, session: %d, ack: %d",
               frame.from.c_str(), frame.to.c_str(), frame.session, size);
          if (it != manager.responseTrafficMap.end()) {
            it->second->addReadableSize(size);
          } else {
            frame.setReply(STATE_TRAFFIC_CLOSE, "failed to handle ack, traffic not exists");
            INFO("[traffic error] %s >> %s, session: %d, ack: %d, failed to handle ack, traffic not exists",
                 frame.from.c_str(), frame.to.c_str(), frame.session, size);
            tunnel->sendData(frame);
          }
        } else if (frame.state == STATE_TRAFFIC_CLOSE) {
          map<Addr, EndpointClientTraffic*>::iterator it = manager.responseTrafficMap.find(addr);
          int size = 0;
          sscanf(frame.message.c_str(), "%d", &size);
          if (it != manager.responseTrafficMap.end()) {
            INFO("[traffic close] %s > %s, session: %d", frame.from.c_str(), frame.to.c_str(), frame.session);
            it->second->writeData(NULL, 0);
            manager.responseTrafficMap.erase(it);
          } else {
            INFO("[traffic close] %s > %s, session: %d, but traffic not exists, skip.",
                 frame.from.c_str(), frame.to.c_str(), frame.session);
          }
        }
      } else { // StreamResponse
        if (frame.state == STATE_TRAFFIC_DATA) {
          map<uint32_t, EndpointClientTraffic*>::iterator it = manager.requestTrafficMap.find(frame.session);
          INFO("[traffic data] %s >> %s, session: %d, dataSize: %zd",
               frame.from.c_str(), frame.to.c_str(), frame.session, frame.message.size());
          if (it != manager.requestTrafficMap.end()) {
            it->second->writeData(frame.message.data(), frame.message.size());
          } else {
            frame.setReply(STATE_TRAFFIC_CLOSE, "failed to handle data, traffic not exists");
            INFO("[traffic error] %s > %s, session: %d, dataSize: %zd, failed to handle data, traffic not exists",
                 frame.from.c_str(), frame.to.c_str(), frame.session, frame.message.size());
            tunnel->sendData(frame);
          }
        } else if (frame.state == STATE_TRAFFIC_ACK) {
          map<uint32_t, EndpointClientTraffic*>::iterator it = manager.requestTrafficMap.find(frame.session);
          int size = 0;
          sscanf(frame.message.c_str(), "%d", &size);
          INFO("[traffic ack] %s >> %s, session: %d, ack: %d",
               frame.from.c_str(), frame.to.c_str(), frame.session, size);
          if (it != manager.requestTrafficMap.end()) {
            it->second->addReadableSize(size);
          } else {
            frame.setReply(STATE_TRAFFIC_CLOSE, "failed to handle ack, traffic not exists");
            INFO("[traffic error] %s > %s, session: %d, ack: %d, failed to handle ack, traffic not exists",
                 frame.from.c_str(), frame.to.c_str(), frame.session, size);
            tunnel->sendData(frame);
          }
        } else if (frame.state == STATE_TRAFFIC_CLOSE) {
          map<uint32_t, EndpointClientTraffic*>::iterator it = manager.requestTrafficMap.find(frame.session);
          int size = 0;
          sscanf(frame.message.c_str(), "%d", &size);
          if (it != manager.requestTrafficMap.end()) {
            INFO("[traffic close] %s >> %s, session: %d", frame.from.c_str(), frame.to.c_str(), frame.session);
            it->second->writeData(NULL, 0);
            manager.requestTrafficMap.erase(it);
          } else {
            INFO("[traffic close] %s >> %s, session: %d, but traffic not exists, skip.",
                 frame.from.c_str(), frame.to.c_str(), frame.session);
          }
        }
      }
    }
    return;
  }
  // if (event == EVENT_WRITTEN) { // DO NOTHING
}

int main(int argc, char** argv) {
  const char* dBrokerHost = "127.0.0.1";
  int dBrokerPort = 8120;
  const char* dServerIp = "0.0.0.0";
  int dServerPort = 0;
  const char* dName = "anonymous";
  const char* dPeerName = "";
  int dLevel = 0;
  const char* dTargetAddress = "guess";
  const char* dPassword = "helloworld";

  const char* brokerHost = dBrokerHost;
  int brokerPort = dBrokerPort;
  const char* serverIp = dServerIp;
  int serverPort = dServerPort;
  const char* name = dName;
  const char* peerName = dPeerName;
  const char* targetAddress = dTargetAddress;
  const char* password = password;
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
    } else if (strcmp(argv[i], "--peerName") == 0 && i + 1 < argc) {
      peerName = argv[i + 1];
    } else if (strcmp(argv[i], "--password") == 0 && i + 1 < argc) {
      password = argv[i + 1];
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
      printf("  --serverIp 0.0.0.0       the server ip, default %s\n", dServerIp);
      printf("  --serverPort (0~65535)   the server port, default disabled\n");
      printf("  --name name              the name, default %s\n", dName);
      printf("  --peerName peerName      the name, default %s\n", dPeerName);
      printf("  --targetAddress ip:port  the name, default %s\n", dTargetAddress);
      printf("  --password xxx           the password to encrypt data, default %s\n", dPassword);
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
  manager.password = password;
  manager.peerName = peerName;
  manager.name = name;
  time_t startTime = time(0);
  while (true) {
    manager.prepare(brokerHost, brokerPort, name, peerName);
    Endpoint::loop();
    time_t now = time(0);
    int elapse = now - startTime;
    if (elapse > 1000 || elapse < 0) {
      Frame frame = manager.makeFrame(NULL, STATE_NONE, "");
      manager.tunnel->sendData(frame);
      startTime = now;
    }
  }
  return 0;
}
