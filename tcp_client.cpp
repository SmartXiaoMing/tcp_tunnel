//
// Created by mabaiming on 16-8-29.
//
#include "tcp_client.h"

#include "common.h"
#include "logger.h"
#include "tunnel_package.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>

using namespace std;
using namespace Common;

void
TcpClient::init(const vector<Addr>& tunnelAddrList, int tunnelRetryInterval,
    const string& trafficIp_, uint16_t trafficPort_,
    const string& tunnelSecret, int tunnelHeartbeat) {
  if (tunnelAddrList.empty()) {
    log_error << "tunnel addr list is empty!";
    exit(EXIT_FAILURE);
  }
  secret = tunnelSecret;
  if (tunnelHeartbeat <= 0) {
    log_error << "invalid heartbeat, use default " << heartbeat;
  } else {
    heartbeat = tunnelHeartbeat;
  }
  tunnelServerList = tunnelAddrList;
  trafficServerIp = trafficIp_;
  trafficServerPort = trafficPort_;
  tunnelServerFd = -1;
  retryInterval = tunnelRetryInterval;
  retryConnectTunnelServer();
}

void
TcpClient::retryConnectTunnelServer() {
  srand(time(0));
  while (tunnelServerFd < 0) {
    int index = rand() % tunnelServerList.size();
    Addr& addr = tunnelServerList[index];
    int fd = prepare(addr.ip, addr.port);
    if (tunnelServerFd > 0) {
      log_info << "use tunnel server addr(" << index << "/"
          << tunnelServerList.size() << "): " << addr.ip << ":" << addr.port;
      tunnelServerFd = fd;
      return;
    }
    if (retryInterval > 0) {
      log_error << "failed to connect: " << addr.ip << ":" << addr.port
          << ", retry after " << retryInterval << " seconds";
      sleep(retryInterval);
    } else {
      log_error << "failed to connect: " << addr.ip << ":" << addr.port << ", exit now";
      exit(EXIT_FAILURE);
    }
  }
}

void
TcpClient::cleanUpTrafficClient(int fakeFd) {
  log_debug << "clean up trafficClient, fakeFd: " << fakeFd;
  map<int, int>::iterator it = trafficClientMap.find(fakeFd);
  if (it != trafficClientMap.end()) {
    sendTunnelState(tunnelServerFd, fakeFd, TunnelPackage::STATE_CLOSE);
    cleanUpFd(it->second);
    trafficClientMap.erase(fakeFd);
    trafficServerMap.erase(it->second);
  }
}

void
TcpClient::cleanUpTrafficServer(int fd) {
  log_debug << "clean up trafficServer, fd: " << fd;
  map<int, int>::iterator it = trafficServerMap.find(fd);
  if (it != trafficServerMap.end()) {
    cleanUpFd(fd);
    trafficServerMap.erase(fd);
    trafficClientMap.erase(it->second);
  }
}

void
TcpClient::resetTunnelServer() {
  map<int, int>::iterator it = trafficServerMap.begin();
  for (; it != trafficServerMap.end(); ++it) {
    cleanUpFd(it->first);
  }
  trafficServerMap.clear();
  trafficClientMap.clear();
  cleanUpFd(tunnelServerFd);
  tunnelServerFd = -1;
  tunnelBuffer.clear();
  retryConnectTunnelServer();
}

int
TcpClient::prepare(const string& ip, uint16_t port) {
  int fd = socket(PF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    log_error << "failed to socket";
    return fd;
  }
  int v;
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr(ip.c_str());
  addr.sin_port = htons(port);
  int result = connect(fd, (struct sockaddr *)&addr, sizeof(struct sockaddr));
  if(result < 0) {
    log_error << "failed to connect " << ip << ":" << port;
    return result;
  }
  log_info << "new fd: " << fd << " for " << ip << ":" << port;
  registerFd(fd);
  return fd;
}

bool
TcpClient::handleTunnelClient(const struct epoll_event& event) {
  if (event.data.fd != tunnelServerFd) { // traffic from tunnel
    return false;
  }
  if ((event.events & EPOLLRDHUP) || (event.events & EPOLLERR)) {
    resetTunnelServer();
    return true;
  }
  if ((event.events & EPOLLIN) == 0) {
    return true;
  }
  char buf[BUFFER_SIZE];
  int len = recv(event.data.fd, buf, BUFFER_SIZE, 0);
  if (len <= 0) {
    resetTunnelServer();
    return true;
  }
  tunnelBuffer.append(buf, len);
  int offset = 0;
  int totalLength = tunnelBuffer.length();
  while (offset < tunnelBuffer.length()) {
    TunnelPackage package;
    int decodeLength = package.decode(tunnelBuffer.c_str() + offset, totalLength - offset);
    if (decodeLength < 0) {
      resetTunnelServer();
      return true;
    }
    if (decodeLength == 0) { // wait next time to read
      break;
    }
    offset += decodeLength;
    log_debug << "recv, trafficServer <-- *tunnelClient(" << event.data.fd << ") <-[fd=" << package.fd
      << ",state=" << package.getState() << ",length=" << package.message.size()
      << "]- tunnelServer <-- trafficClient";
    switch (package.state) {
      case TunnelPackage::STATE_CHALLENGE_REQUEST:
        sendTunnelState(event.data.fd, 0, TunnelPackage::STATE_CHALLENGE_RESPONSE);
        break;
      case TunnelPackage::STATE_CREATE: {
        int trafficFd = prepare(trafficServerIp, trafficServerPort);
        log_debug << "prepare fd: " << trafficFd << ", from package.fd: " << package.fd;
        if (trafficFd > 0) {
          trafficServerMap[trafficFd] = package.fd;
          trafficClientMap[package.fd] = trafficFd;
          sendTunnelState(event.data.fd, package.fd, TunnelPackage::STATE_CREATE + 10);
        } else {
          sendTunnelState(event.data.fd, package.fd, TunnelPackage::STATE_CREATE_FAILURE);
        }
      } break;
      case TunnelPackage::STATE_CLOSE: cleanUpTrafficClient(package.fd); break;
      case TunnelPackage::STATE_TRAFFIC: {
        map<int, int>::iterator it = trafficClientMap.find(package.fd);
        if (it == trafficClientMap.end()) {
          log_error << "no related fd for client: " << package.fd;
        } else {
          send(it->second, package.message.c_str(), package.message.size(), 0);
          log_debug << "send, trafficServer(" << it->second
                    << ") <-[length=" << package.message.size()
                    << "]- *tunnelClient(" << event.data.fd
                    << ") <-- tunnelServer <-- trafficClient";
        }
      } break;
      default: log_warn << "ignore state: " << (int) package.state;
    }
  }
  if (offset > 0) {
    tunnelBuffer.assign(tunnelBuffer.begin() + offset, tunnelBuffer.end());
  }
  return true;
}

bool
TcpClient::handleTrafficServer(const struct epoll_event& event) {
  log_debug << "test, fd: " << event.data.fd << ", events: " << event.events;
  map<int, int>::iterator it = trafficServerMap.find(event.data.fd);
  if (it == trafficServerMap.end()) {
    return false;
  }
  if ((event.events & EPOLLRDHUP) || (event.events & EPOLLERR)) {
    sendTunnelState(tunnelServerFd, it->second, TunnelPackage::STATE_CLOSE);
    cleanUpTrafficServer(event.data.fd);
    return true;
  }
  if ((event.events & EPOLLIN) == 0) {
    return true;
  }
  char buf[BUFFER_SIZE];
  int len = recv(event.data.fd, buf, BUFFER_SIZE, 0);
  log_debug << "recv, trafficServer -[length=" << len
    << "]-> *tunnelClient(" << event.data.fd << ") --> tunnelServer --> trafficClient";
  if (len <= 0) {
    sendTunnelState(tunnelServerFd, it->second, TunnelPackage::STATE_CLOSE);
    cleanUpTrafficServer(event.data.fd);
    return true;
  }
  sendTunnelTraffic(event.data.fd, tunnelServerFd, it->second, string(buf, len));
  return true;
}

void
TcpClient::run() {
  while(true) {
    struct epoll_event events[MAX_EVENTS];
    int nfds = epoll_wait(epollFd, events, MAX_EVENTS, 60000);
    if (nfds == 0) {
        sendTunnelState(tunnelServerFd, 0, TunnelPackage::STATE_HEARTBEAT);
    } else {
        if(nfds == -1) {
          log_error << "failed to epoll_wait";
          exit(EXIT_FAILURE);
        }
        for(int i = 0; i < nfds; i++) {
          handleTunnelClient(events[i]);
          handleTrafficServer(events[i]);
        }
    }
  }
}
