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
TcpClient::init(const string& addrStr, int tunnelRetryInterval,
    const string& trafficIp_, uint16_t trafficPort_,
    const string& tunnelSecret, int tunnelHeartbeat) {
  if (addrStr.empty()) {
    log_error << "tunnel addr list is empty!";
    exit(EXIT_FAILURE);
  }
  addrListStr = addrStr;
  if (!refreshAddrList()) {
    exit(EXIT_FAILURE);
  }
  secret = tunnelSecret;
  if (tunnelHeartbeat <= 0) {
    log_error << "invalid heartbeat, use default " << heartbeat;
  } else {
    heartbeat = tunnelHeartbeat;
  }
  trafficServerIp = trafficIp_;
  trafficServerPort = trafficPort_;
  tunnelServerFd = -1;
  retryInterval = tunnelRetryInterval;
  retryConnectTunnelServer();
}

bool
TcpClient::refreshAddrList() {
  if (!parseAddressList(tunnelServerList, addrListStr)) {
    log_error << "invalid addrList: " << addrListStr;
    return false;
  }
  for (int i = 0; i < tunnelServerList.size(); ++i) {
    log_info << "tunnelServer[" << i << "]: "
        << tunnelServerList[i].toString() << endl;
  }
  return true;
}

void
TcpClient::retryConnectTunnelServer() {
  static bool firstConnect = true;
  srand(time(0));
  int retryTime = 0;
  while (tunnelServerFd < 0) {
    ++retryTime;
    if (retryTime > 10) {
      refreshAddrList();
      retryTime = 0;
    }
    int index = rand() % tunnelServerList.size();
    Addr& addr = tunnelServerList[index];
    if (!firstConnect) {
      sleep(retryInterval);
    }
    firstConnect = false;
    int fd = connectServer(addr.ip, addr.port);
    if (fd > 0) {
      log_info << "use tunnel server addr(" << index << "/"
          << tunnelServerList.size() << "): " << addr.ip << ":" << addr.port;
      tunnelServerFd = fd;
      return;
    }
    if (retryInterval > 0) {
      log_error << "retry " << addr.ip << ":" << addr.port
          << " after " << retryInterval << " seconds";
      sleep(retryInterval);
    } else {
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
  map<int, int>::iterator it = trafficServerMap.find(fd);
  if (it != trafficServerMap.end()) {
    log_debug << "clean up trafficServer: " << addrRemote(fd);
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

bool
TcpClient::handleTunnelClient(uint32_t events, int eventFd) {
  if (eventFd != tunnelServerFd) { // traffic from tunnel
    return false;
  }
  if ((events & EPOLLRDHUP) || (events & EPOLLERR)) {
    log_error << "events: " << events << ", fd: " << eventFd;
    resetTunnelServer();
    return true;
  }
  if ((events & EPOLLIN) == 0) {
    return true;
  }
  char buf[BUFFER_SIZE];
  int len = recv(eventFd, buf, BUFFER_SIZE, 0);
  if (len <= 0) {
    log_error << "events: " << events << ", len: " << len;
    resetTunnelServer();
    return true;
  }
  tunnelBuffer.append(buf, len);
  int offset = 0;
  int totalLength = tunnelBuffer.length();
  while (offset < tunnelBuffer.length()) {
    TunnelPackage package;
    int decodeLength
        = package.decode(tunnelBuffer.c_str() + offset, totalLength - offset);
    if (decodeLength < 0) {
      log_error << "events: " << events << ", decodeLength: " << decodeLength;
      resetTunnelServer();
      return true;
    }
    if (decodeLength == 0) { // wait next time to read
      break;
    }
    offset += decodeLength;
    log_debug << "recv, " << addrLocal(tunnelServerFd)
        << " <-[fd=" << package.fd << ",state=" << package.getState()
        << ",length=" << package.message.size() << "]- "
        <<  addrRemote(tunnelServerFd);
    switch (package.state) {
      case TunnelPackage::STATE_CHALLENGE_REQUEST:
        sendTunnelMessage(
            tunnelServerFd, 0, TunnelPackage::STATE_CHALLENGE_RESPONSE, secret
        );
        break;
      case TunnelPackage::STATE_CREATE: {
        int trafficFd = connectServer(trafficServerIp, trafficServerPort);
        if (trafficFd > 0) {
          trafficServerMap[trafficFd] = package.fd;
          trafficClientMap[package.fd] = trafficFd;
        } else {
          sendTunnelState(
              tunnelServerFd, package.fd, TunnelPackage::STATE_CREATE_FAILURE
          );
        }
      } break;
      case TunnelPackage::STATE_CLOSE: cleanUpTrafficClient(package.fd); break;
      case TunnelPackage::STATE_TRAFFIC: {
        map<int, int>::iterator it = trafficClientMap.find(package.fd);
        if (it == trafficClientMap.end()) {
          log_error << "no related fd for client: " << package.fd;
        } else {
          int n = send(
              it->second, package.message.c_str(), package.message.size(), 0
          );
          log_debug << "send, " << addrLocal(it->second)
              << " -[length=" << n << "]-> "
              <<  addrRemote(it->second);
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
TcpClient::handleTrafficServer(uint32_t events, int eventFd) {
  map<int, int>::iterator it = trafficServerMap.find(eventFd);
  if (it == trafficServerMap.end()) {
    return false;
  }
  if ((events & EPOLLRDHUP) || (events & EPOLLERR)) {
    sendTunnelState(tunnelServerFd, it->second, TunnelPackage::STATE_CLOSE);
    cleanUpTrafficServer(eventFd);
    return true;
  }
  if ((events & EPOLLIN) == 0) {
    return true;
  }
  char buf[BUFFER_SIZE];
  log_debug << "ready to recv data, for " << addrLocal(it->first) << " <- " << addrRemote(it->first);
  int len = recv(eventFd, buf, BUFFER_SIZE, 0);
  log_debug << "recv, " << addrLocal(it->first)
      << " <-[length=" << len << "]- "
      << addrRemote(it->first);
  if (len <= 0) {
    sendTunnelState(tunnelServerFd, it->second, TunnelPackage::STATE_CLOSE);
    cleanUpTrafficServer(eventFd);
    return true;
  }
  sendTunnelTraffic(tunnelServerFd, it->second, string(buf, len));
  return true;
}

void
TcpClient::run() {
  int heartbeatMs = heartbeat * 1000;
  while(true) {
    struct epoll_event events[MAX_EVENTS];
    int nfds = epoll_wait(epollFd, events, MAX_EVENTS, heartbeatMs);
    if (nfds == 0) {
        sendTunnelState(tunnelServerFd, 0, TunnelPackage::STATE_HEARTBEAT);
    } else {
        if(nfds == -1) {
          log_error << "failed to epoll_wait";
          exit(EXIT_FAILURE);
        }
        for(int i = 0; i < nfds; i++) {
          int eventFd = events[i].data.fd;
          handleTunnelClient(events[i].events, eventFd);
          handleTrafficServer(events[i].events, eventFd);
        }
    }
  }
}
