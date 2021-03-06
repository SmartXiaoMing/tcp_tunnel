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
      return d < 0;
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

  EndpointClientTraffic(const string& name, int fd, const string& peerName): EndpointClient(fd, onTrafficChanged),
      packageNumber(0), proto(ProtoUnknown), owner(Frame::OwnerMe) {
    gSession++;
    addr.session = gSession;
    addr.name = name;
    this->name = name;
    frameSent = 0;
    frameRecv = 0;
    INFO("[%s#%d > %s] create new traffic come from local", addr.name.c_str(), addr.session, peerName.c_str());
  }
  EndpointClientTraffic(const string& name, const Addr& addr, const string& peerName): EndpointClient(onTrafficChanged),
      packageNumber(0), proto(ProtoUnknown), owner(Frame::OwnerPeer) {
    this->addr = addr;
    this->name = name;
    frameSent = 0;
    frameRecv = 0;
    INFO("[%s#%d <- %s] create new traffic come from remote", addr.name.c_str(), addr.session, name.c_str());
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
  int owner;
  string name;
  int frameSent;
  int frameRecv;
};

class Manager {
public:
  static string name;
  static string password;
  static string peerName;
  static string peerPassword;
  static string targetAddress;
  static string serverIp;
  static int serverPort;
  static string brokerHost;
  static int brokerPort;

  Manager(): tunnel(NULL), firstConnection(true) {}
  void prepare() {
    while (tunnel == NULL) {
      if (!firstConnection) {
        ERROR("[Manager] prepare and wait 30 seoncds ...");
        sleep(30);
      }
      firstConnection = false;
      char ip[30];
      if (selectIp(brokerHost.c_str(), ip, 29)) {
        INFO("[Manager] success to select tunnel server, host:%s -> ip:%s", brokerHost.c_str(), ip);
        int fd = createClient(ip, brokerPort);
        if (fd < 0) {
          WARN("[Manager] failed to connect tunnel server %s:%d", ip, brokerPort);
        } else {
          INFO("[Manager] success to connect tunnel server %s:%d", ip, brokerPort);
          tunnel = new EndpointClientTunnel(fd, onTunnelChanged);
        }
      } else {
        ERROR("[Manager] failed to select tunnel server, host:%s, ip:%s", brokerHost.c_str(), ip);
      }
      if (tunnel == NULL) {
        ERROR("[Manager] failed to prepare and wait 30 seoncds ...");
        sleep(30);
      } else {
        char data[256];
        int size = sprintf(data, "name=%s&peerName=%s", name.c_str(), peerName.c_str());
        string message(data, size);
        INFO("[%s -> %s] send login, message[%zd]: %s",
             name.c_str(), peerName.c_str(), message.size(), message.c_str());
        Frame frame = makeFrame(NULL, STATE_TUNNEL_LOGIN, message);
        sendFrame(NULL, frame);
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
    Frame frame = makeFrame(traffic, STATE_TRAFFIC_CONNECT, target);
    INFO("[%s#%d -> %s] guess proto = SSH, send connect, message[%zd]: %s",
         frame.from.c_str(), frame.session, frame.to.c_str(), frame.message.size(), frame.message.c_str());
    sendFrame(traffic, frame);
    frame = makeFrame(traffic, STATE_TRAFFIC_DATA, string(data, size));
    sendFrame(traffic, frame);
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
    INFO("[%s#%d -> %s] guess proto = https, send connect, message[%zd]: %s",
         frame.from.c_str(), frame.session, frame.to.c_str(), frame.message.size(), frame.message.c_str());
    sendFrame(traffic, frame);
    int leftSize = size - headerEndPos - headerEnd.size;
    if (leftSize > 0) {
      frame = makeFrame(traffic, STATE_TRAFFIC_DATA, string(data + size - leftSize, leftSize));
      INFO("[%s#%d -> %s] send data, message[%zd]: %s",
           frame.from.c_str(), frame.session, frame.to.c_str(), frame.message.size(), frame.message.c_str());
      sendFrame(traffic, frame);
    }
    string response = "HTTP/1.1 200 Connection Established\r\n\r\n";
    sendTraffic(traffic, response.data(), response.size());
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
    INFO("[%s#%d -> %s] guess proto = http, send connect, message[%zd]: %s",
         frame.from.c_str(), frame.session, frame.to.c_str(), frame.message.size(), frame.message.c_str());
    sendFrame(traffic, frame);
    frame = makeFrame(traffic, STATE_TRAFFIC_DATA, string(data, size));
    INFO("[%s#%d -> %s] send data, message[%zd]: %s",
         frame.from.c_str(), frame.session, frame.to.c_str(), frame.message.size(), frame.message.c_str());
    sendFrame(traffic, frame);
    traffic->popReadData(size);
    traffic->addReadableSize(size);
    return true;
  }

  void handleTunnelPeerError(Frame& frame) {
    ERROR("[tunnel] tunnel %s error, message:%s, reset all relation connections",
          frame.from.c_str(), frame.message.c_str());
    resetResponseTraffic(frame.from);
  }

  void handleTunnelMeError() {
    INFO("[tunnel] tunnel error local, reset all connections");
    resetAllTraffic();
    tunnel = NULL;
  }

  void handleOwnerPeerConnect(Frame& frame) {
    INFO("[%s#%d > %s] recv frame#0, %s, message[%zd]: %s", frame.from.c_str(), frame.session, frame.to.c_str(),
         Frame::stateToStr(frame.state), frame.message.size(), frame.message.c_str());
    char host[frame.message.size() + 1];
    int port = 80;
    if (parseIpPort(frame.message, host, &port)) {
      char ip[30];
      if (selectIp(host, ip, 29) == NULL) {
        INFO("[%s#%d > %s] failed to resolve host:%s", frame.from.c_str(), frame.session, frame.to.c_str(), host);
        string message("failed to resolve host");
        frame.setReply(STATE_TRAFFIC_CLOSE, message);
        sendFrame(NULL, frame);
        return;
      } else {
        INFO("[%s#%d > %s] success to resolve host:%s, ip:%s, port:%d", frame.from.c_str(),
             frame.session, frame.to.c_str(), host, ip, port);
      }
      Addr addr(frame.from, frame.session);
      map<Addr, EndpointClientTraffic*>::iterator it2 = responseTrafficMap.find(addr);
      if (it2 != responseTrafficMap.end()) {
        INFO("[%s#%d > %s] error, the session exists already!!!", frame.from.c_str(), frame.session, frame.to.c_str());
      }
      EndpointClientTraffic* traffic = new EndpointClientTraffic(name, addr, frame.from);
      if (traffic->createClient(ip, port)) {
        traffic->packageNumber = 1;
        responseTrafficMap[addr] = traffic;
        INFO("[%s#%d > %s] success to make a connection to host:%s, ip:%s, port:%d", frame.from.c_str(),
             frame.session, frame.to.c_str(), host, ip, port);
        traffic->frameRecv++;
      } else {
        string message("failed to conenct");
        frame.setReply(STATE_TRAFFIC_CLOSE, "failed to connect");
        sendFrame(traffic, frame);
      }
    } else {
      string message("invalid config");
      frame.setReply(STATE_TRAFFIC_CLOSE, "invalid config");
      sendFrame(NULL, frame);
    }
}
  void handleOwnerMeFrame(EndpointClientTraffic* traffic, Frame& frame) {
    if (traffic == NULL) {
      if (frame.state == STATE_TRAFFIC_CLOSE) {
        INFO("[%s#%d > %s] traffic not exists, do nothing", frame.from.c_str(), frame.session, frame.to.c_str());
      } else {
        frame.setReply(STATE_TRAFFIC_CLOSE, "failed to handle data, traffic not exists");
        sendFrame(NULL, frame);
      }
      return;
    }
    if (frame.state == STATE_TRAFFIC_DATA) {
      sendTraffic(traffic, frame.message.data(), frame.message.size());
    } else if (frame.state == STATE_TRAFFIC_ACK) {
      int size = 0;
      sscanf(frame.message.c_str(), "%d", &size);
      traffic->addReadableSize(size);
    } else if (frame.state == STATE_TRAFFIC_CLOSE) {
      INFO("[%s#%d] try to erase the local traffic", traffic->addr.name.c_str(), traffic->addr.session);
      sendTraffic(traffic, NULL, 0);
      requestTrafficMap.erase(traffic->addr.session);
    }
  }

  void handleOwnerPeerFrame(EndpointClientTraffic* traffic, Frame& frame) {
    if (traffic == NULL) {
      if (frame.state == STATE_TRAFFIC_CLOSE) {
        INFO("[%s#%d > %s] traffic not exists, do nothing", frame.from.c_str(), frame.session, frame.to.c_str());
      } else {
        frame.setReply(STATE_TRAFFIC_CLOSE, "failed to handle data, traffic not exists");
        sendFrame(NULL, frame);
      }
      return;
    }
    if (frame.state == STATE_TRAFFIC_DATA) {
      sendTraffic(traffic, frame.message.data(), frame.message.size());
    } else if (frame.state == STATE_TRAFFIC_ACK) {
      int size = 0;
      sscanf(frame.message.c_str(), "%d", &size);
      traffic->addReadableSize(size);
    } else if (frame.state == STATE_TRAFFIC_CLOSE) {
      INFO("[%s#%d] try to erase the local traffic", traffic->addr.name.c_str(), traffic->addr.session);
      sendTraffic(traffic, NULL, 0);
      responseTrafficMap.erase(traffic->addr);
    }
  }

  void handleOwnerMeTrafficRead(EndpointClientTraffic* traffic, const char* data, int size) {
    if (size > 0) {
      if (traffic->packageNumber == 0) {
        traffic->packageNumber = 1;
        if (!targetAddress.empty() && targetAddress != "guess") {
          Frame frame = makeFrame(traffic, STATE_TRAFFIC_CONNECT, targetAddress);
          sendFrame(traffic, frame);
          frame = makeFrame(traffic, STATE_TRAFFIC_DATA, string(data, size));
          sendFrame(traffic, frame);
          traffic->popReadData(size);
        } else {
          int proto = EndpointClientTraffic::guessProto(data, size);
          INFO("[%s#%d -> %s] guess proto:%d for data[%d]: %.*s",
               traffic->addr.name.c_str(), traffic->addr.session, peerName.c_str(),
               proto, size, size, data);
          bool success = false;
          if (proto == EndpointClientTraffic::ProtoSsh) {
            success = handleProtoSsh(traffic, data, size);
          } else if (proto == EndpointClientTraffic::ProtoHttpTunnel) {
            success = handleProtoHttpTunnel(traffic, data, size);
          } else if (proto == EndpointClientTraffic::ProtoHttp) {
            success = handleProtoHttp(traffic, data, size);
          }
          if (!success) {
            INFO("[%s#%d -> %s] failed to guess proto, close local traffic",
                 traffic->addr.name.c_str(), traffic->addr.session, peerName.c_str());
            sendTraffic(traffic, NULL, 0);
            requestTrafficMap.erase(traffic->addr.session);
          }
        }
      } else {
        Frame frame = makeFrame(traffic, STATE_TRAFFIC_DATA, string(data, size));
        sendFrame(traffic, frame);
        traffic->popReadData(size);
      }
    } else { // size == 0, read eof
      Frame frame = makeFrame(traffic, STATE_TRAFFIC_CLOSE, "read EOF");
      INFO("[%s#%d -> %s] read EOF, send data, message[%zd]: %s", frame.from.c_str(),
           frame.session, frame.to.c_str(), frame.message.size(), frame.message.c_str());
      sendFrame(traffic, frame);
    }
  }

  void handleOwnerPeerTrafficRead(EndpointClientTraffic* traffic, const char* data, int size) {
    if (size > 0) {
      Frame frame = makeFrame(traffic, STATE_TRAFFIC_DATA, string(data, size));
      sendFrame(traffic, frame);
      traffic->popReadData(size);
    } else { // size == 0, read eof
      Frame frame = makeFrame(traffic, STATE_TRAFFIC_CLOSE, "read EOF");
      sendFrame(traffic, frame);
    }
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
      frame.from = traffic->name;
      frame.owner = traffic->owner;
      frame.session = traffic->addr.session;
      if (frame.owner == Frame::OwnerMe) {
        frame.from = name;
        frame.to = peerName;
      } else {
        frame.from = traffic->addr.name;
        frame.to = name;
      }
    } else {
      frame.from = "";
      frame.to = "";
      frame.owner = Frame::OwnerMe;
      frame.session = 0;
    }
    frame.state = state;
    frame.message = message;
    return frame;
  }

  void sendFrame(EndpointClientTraffic* traffic, Frame& frame) {
    if (traffic != NULL) {
      if (frame.state == FrameState::STATE_TRAFFIC_ACK ||
          frame.state == FrameState::STATE_TRAFFIC_CLOSE ||
          frame.state == FrameState::STATE_TRAFFIC_CONNECT) {
        INFO("[%s#%d > %s] send frame#%d, %s, message[%zd]: %s", frame.from.c_str(), frame.session, frame.to.c_str(),
             traffic->frameSent, Frame::stateToStr(frame.state), frame.message.size(), frame.message.c_str());
      } else {
        INFO("[%s#%d > %s] send frame#%d, %s, message[%zd]", frame.from.c_str(), frame.session, frame.to.c_str(),
             traffic->frameSent, Frame::stateToStr(frame.state), frame.message.size());
      }
      traffic->frameSent++;
    } else {
      if (frame.state == FrameState::STATE_TRAFFIC_ACK ||
          frame.state == FrameState::STATE_TRAFFIC_CLOSE ||
          frame.state == FrameState::STATE_TRAFFIC_CONNECT) {
        INFO("[%s#%d > %s] send frame#-1, %s, message[%zd]: %s", frame.from.c_str(), frame.session, frame.to.c_str(),
             Frame::stateToStr(frame.state), frame.message.size(), frame.message.c_str());
      } else {
        INFO("[%s#%d > %s] send frame#-1, %s, message[%zd]", frame.from.c_str(), frame.session, frame.to.c_str(),
             Frame::stateToStr(frame.state), frame.message.size());
      }
    }
    tunnel->sendData(frame);
  }

  void sendTraffic(EndpointClientTraffic* traffic, const char* data, int size) {
    INFO("[%s#%d] send traffic: %d", traffic->addr.name.c_str(), traffic->addr.session, size);
    traffic->writeData(data, size);
  }

  EndpointClientTunnel* tunnel;
  map<uint32_t, EndpointClientTraffic*> requestTrafficMap;
  map<Addr, EndpointClientTraffic*> responseTrafficMap;
  bool firstConnection;
};

Manager manager;

void onNewClientTraffic(EndpointServer* endpoint, int acfd) {
  EndpointServer* serverTraffic = (EndpointServer*) endpoint;
  EndpointClientTraffic* traffic = new EndpointClientTraffic(manager.name, acfd, manager.peerName);
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
    INFO("[%s#%d] traffic local close", traffic->addr.name.c_str(), traffic->addr.session);
    if (traffic->owner == Frame::OwnerMe) {
      manager.requestTrafficMap.erase(traffic->addr.session);
    } else {
      manager.responseTrafficMap.erase(traffic->addr);
    }
    manager.sendFrame(traffic, frame);
    return;
  }
  if (event == EVENT_READ) {
    if (traffic->owner == Frame::OwnerMe) {
      manager.handleOwnerMeTrafficRead(traffic, data, size);
    } else {
      manager.handleOwnerPeerTrafficRead(traffic, data, size);
    }
    return;
  } else if (event == EVENT_WRITTEN && size > 0) {
    char b[20];
    int len = sprintf(b, "%d", size) + 1;
    Frame frame = manager.makeFrame(traffic, STATE_TRAFFIC_ACK, b);
    manager.sendFrame(traffic, frame);
    return;
  }
}

void onTunnelChanged(EndpointClient* endpoint, int event, const char* data, int size) {
  if (event == EVENT_ERROR || event == EVENT_CLOSED) {
    manager.handleTunnelMeError();
    return;
  }
  EndpointClientTunnel* tunnel = (EndpointClientTunnel*) endpoint;
  if (event == EVENT_READ) {
    Frame frame;
    while (tunnel->parseFrame(frame) > 0) {
      if (frame.state == STATE_TUNNEL_ERROR) {
        manager.handleTunnelPeerError(frame);
      } else if (frame.state == STATE_TRAFFIC_CONNECT) {
        manager.handleOwnerPeerConnect(frame);
      } else if (frame.owner == Frame::OwnerMe) {
        map<uint32_t, EndpointClientTraffic*>::iterator it = manager.requestTrafficMap.find(frame.session);
        EndpointClientTraffic* traffic = NULL;
        if (it != manager.requestTrafficMap.end()) {
          traffic = it->second;
          if (frame.state == FrameState::STATE_TRAFFIC_ACK ||
              frame.state == FrameState::STATE_TRAFFIC_CLOSE ||
              frame.state == FrameState::STATE_TRAFFIC_CONNECT) {
            INFO("[%s#%d > %s] recv frame#%d, %s, message[%zd]: %s", frame.from.c_str(), frame.session, frame.to.c_str(),
                 traffic->frameRecv, Frame::stateToStr(frame.state), frame.message.size(), frame.message.c_str());
          } else {
            INFO("[%s#%d > %s] recv frame#%d, %s, message[%zd]", frame.from.c_str(), frame.session, frame.to.c_str(),
                 traffic->frameRecv, Frame::stateToStr(frame.state), frame.message.size());
          }
          traffic->frameRecv++;
        }
        manager.handleOwnerMeFrame(traffic, frame);
      } else {
        Addr addr(frame.from, frame.session);
        map<Addr, EndpointClientTraffic*>::iterator it = manager.responseTrafficMap.find(addr);
        EndpointClientTraffic* traffic = NULL;
        if (it != manager.responseTrafficMap.end()) {
          traffic = it->second;
          if (frame.state == FrameState::STATE_TRAFFIC_ACK ||
              frame.state == FrameState::STATE_TRAFFIC_CLOSE ||
              frame.state == FrameState::STATE_TRAFFIC_CONNECT) {
            INFO("[%s#%d > %s] recv frame#%d, %s, message[%zd]: %s", frame.from.c_str(), frame.session, frame.to.c_str(),
                 traffic->frameRecv, Frame::stateToStr(frame.state), frame.message.size(), frame.message.c_str());
          } else {
            INFO("[%s#%d > %s] recv frame#%d, %s, message[%zd]", frame.from.c_str(), frame.session, frame.to.c_str(),
                 traffic->frameRecv, Frame::stateToStr(frame.state), frame.message.size());
          }
          traffic->frameRecv++;
        }
        manager.handleOwnerPeerFrame(traffic, frame);
      }
    }
    return;
  }
  // if (event == EVENT_WRITTEN) { // DO NOTHING
}
string Manager::name = "alice";
string Manager::password = "142857";
string Manager::peerName = "bob";
string Manager::peerPassword = "142857";
string Manager::targetAddress = "guess";
string Manager::brokerHost = "127.0.0.1";
int Manager::brokerPort = 8122;
string Manager::serverIp = "127.0.0.1";
int Manager::serverPort = 9001;

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
      Manager::brokerHost = argv[i + 1];
    } else if (strcmp(argv[i], "--brokerPort") == 0 && i + 1 < argc) {
      Manager::brokerPort = atoi(argv[i + 1]);
    } else if (strcmp(argv[i], "--serverIp") == 0 && i + 1 < argc) {
      Manager::serverIp = argv[i + 1];
    } else if (strcmp(argv[i], "--serverPort") == 0 && i + 1 < argc) {
      Manager::serverPort = atoi(argv[i + 1]);
    } else if (strcmp(argv[i], "--targetAddress") == 0 && i + 1 < argc) {
      Manager::targetAddress = argv[i + 1];
    } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
      Manager::name = argv[i + 1];
    } else if (strcmp(argv[i], "--password") == 0 && i + 1 < argc) {
      Manager::password = argv[i + 1];
    } else if (strcmp(argv[i], "--peerName") == 0 && i + 1 < argc) {
      Manager::peerName = argv[i + 1];
    } else if (strcmp(argv[i], "--peerPassword") == 0 && i + 1 < argc) {
      Manager::peerPassword = argv[i + 1];
    } else if (strncmp(argv[i], "--v", 3) == 0) {
      logLevel = atoi(argv[i + 1]);
    } else {
      int ret = strcmp(argv[i], "--help");
      if (ret != 0) {
        printf("\nunknown option: %s\n", argv[i]);
      }
      printf("usage: %s [options]\n\n", argv[0]);
      printf("  --brokerHost domain.com  the server host, default %s\n", Manager::brokerHost.c_str());
      printf("  --brokerPort (0~65535)   the server port, default %d\n", Manager::brokerPort);
      printf("  --serverIp 0.0.0.0       the server ip, default %s\n", Manager::serverIp.c_str());
      printf("  --serverPort (0~65535)   the server port, default %d\n", Manager::serverPort);
      printf("  --name name              the name, default %s\n", Manager::name.c_str());
      printf("  --password password      the password, default %s\n", Manager::password.c_str());
      printf("  --peerName peerName      the peerName, default %s\n", Manager::peerName.c_str());
      printf("  --password password      the peerPassword, default %s\n", Manager::peerPassword.c_str());
      printf("  --targetAddress ip:port  the targetAddress, default %s\n", Manager::targetAddress.c_str());
      printf("  --v [0-5]                set log level, 0-5 means OFF, ERROR, WARN, INFO, DEBUG, default %d\n", dLevel);
      printf("  --help                   show the usage then exit\n");
      printf("\n");
      printf("version 1.0, report bugs to SmartXiaoMing(95813422@qq.com)\n");
      exit(ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
    }
  }

  Endpoint::init();
  if (Manager::serverPort > 0 && manager.createTrafficServer(Manager::serverIp.c_str(), Manager::serverPort) == NULL) {
    ERROR("%s:%d is in use already", Manager::serverIp.c_str(), Manager::serverPort);
    exit(1);
  }
  time_t startTime = time(0);
  while (true) {
    manager.prepare();
    Endpoint::loop();
    time_t now = time(0);
    int elapse = now - startTime;
    if (elapse > 120 || elapse < 0) {
      Frame frame = manager.makeFrame(NULL, STATE_NONE, "");
      manager.sendFrame(NULL, frame);
      startTime = now;
    }
  }
  return 0;
}
