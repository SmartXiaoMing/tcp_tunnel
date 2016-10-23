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
TcpClient::cleanUpTrafficClient(int fakeFd, int ctrl) {
  log_debug << "clean up trafficClient, fakeFd: " << fakeFd;
  map<int, int>::iterator it = trafficClientMap.find(fakeFd);
  if (it != trafficClientMap.end()) {
    if (ctrl == CTRL_ACTIVE) {
      tunnelSendBuffer.sendTunnelState(tunnelServerFd, fakeFd, TunnelPackage::STATE_CLOSE);
    }
    cleanUpFd(it->second);
    trafficServerMap.erase(it->second);
    trafficClientMap.erase(fakeFd);
  }
}

void
TcpClient::cleanUpTrafficServer(int fd) {
  map<int, TrafficServerInfo>::iterator it = trafficServerMap.find(fd);
  if (it != trafficServerMap.end()) {
    log_debug << "clean up trafficServer: " << addrRemote(fd);
    tunnelSendBuffer.sendTunnelState(tunnelServerFd, it->second.connectId, TunnelPackage::STATE_CLOSE);
    cleanUpFd(fd);
    trafficClientMap.erase(it->second.connectId);
    trafficServerMap.erase(fd);
  }
}

void
TcpClient::resetTunnelServer() {
  map<int, TrafficServerInfo>::iterator it = trafficServerMap.begin();
  for (; it != trafficServerMap.end(); ++it) {
    cleanUpFd(it->first);
  }
  trafficServerMap.clear();
  trafficClientMap.clear();
  cleanUpFd(tunnelServerFd);
  tunnelServerFd = -1;
  tunnelRecvBuffer.buffer.clear();
  tunnelSendBuffer.buffer.clear();
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
  if (events & EPOLLIN) {
		int len = tunnelRecvBuffer.recv(eventFd);
		if (len < 0) {
      log_error << "events: " << events << ", len: " << len;
      resetTunnelServer();
      return true;
    }
    TunnelPackage package;
    while (tunnelRecvBuffer.popPackage(package)) {
      log_debug << "recv, " << addrLocal(tunnelServerFd)
          << " <-[fd=" << package.fd << ",state=" << package.getState()
          << ",length=" << package.message.size() << "]- "
          <<  addrRemote(tunnelServerFd);
      switch (package.state) {
        case TunnelPackage::STATE_CHALLENGE_REQUEST:
          tunnelSendBuffer.sendTunnelMessage(
            tunnelServerFd, 0, TunnelPackage::STATE_CHALLENGE_RESPONSE, secret
          );
          break;
        case TunnelPackage::STATE_CREATE: {
          int trafficFd = connectServer(trafficServerIp, trafficServerPort);
          if (trafficFd > 0) {
            trafficServerMap[trafficFd] = TrafficServerInfo(package.fd);
            trafficClientMap[package.fd] = trafficFd;
            log_debug << "create trafficFd: " << trafficFd << " for package.fd" << package.fd
              << ", " << addrLocal(trafficFd) << " -> " << addrRemote(trafficFd);
          } else {
            tunnelSendBuffer.sendTunnelState(
              tunnelServerFd, package.fd, TunnelPackage::STATE_CREATE_FAILURE
            );
          }
        } break;
        case TunnelPackage::STATE_CLOSE:
          cleanUpTrafficClient(package.fd, CTRL_PASSIVE);
          break;
        case TunnelPackage::STATE_TRAFFIC: {
          map<int, int>::iterator it = trafficClientMap.find(package.fd);
          if (it == trafficClientMap.end()) {
            log_error << "no related fd for client: " << package.fd;
          } else {
            map<int, TrafficServerInfo>::iterator it2 = trafficServerMap.find(it->second);
            int result = it2->second.sendBuffer.send(it2->first, package.message);
            if (result < 0) {
              log_debug << "send, fd:" << it->second << ", "
                  << addrLocal(it->second)
                  << " --> "
                  << addrRemote(it->second);
            } else {
              log_debug << "send failed, fd:" << it->second << ", "
                  << addrLocal(it->second)
                  << " --> "
                  << addrRemote(it->second);
            }
          }
        } break;
        default: log_warn << "ignore state: " << (int) package.state;
      }
    }
	}
  if (events & EPOLLOUT) {
    if (tunnelSendBuffer.send(eventFd) < 0) {
      resetTunnelServer();
      return true;
    }
  }
  return true;
}

bool
TcpClient::handleTrafficServer(uint32_t events, int eventFd) { // TODO
  map<int, TrafficServerInfo>::iterator it = trafficServerMap.find(eventFd);
  if (it == trafficServerMap.end()) {
    return false;
  }
  if ((events & EPOLLRDHUP) || (events & EPOLLERR)) {
    cleanUpTrafficServer(eventFd);
    return true;
  }
  if (events & EPOLLIN && !tunnelSendBuffer.isFull()) {
    char buf[BUFFER_SIZE];
    log_debug << "ready to recv data, for " << addrLocal(it->first) << " <- " << addrRemote(it->first);
    int len = recv(eventFd, buf, BUFFER_SIZE, 0);
    if (len > 0) { // TODO
      tunnelSendBuffer.sendTunnelTraffic(tunnelServerFd, it->second.connectId, string(buf, len));
      log_debug << "recv, " << addrLocal(it->first)
          << " <-[length=" << len << "]- "
          << addrRemote(it->first);
    } else if (!isGoodCode()) {
      cleanUpTrafficServer(eventFd);
      return true;
    }
  }
  if (events & EPOLLOUT) {
    if (it->second.sendBuffer.send(eventFd) < 0) {
      cleanUpTrafficServer(eventFd);
      return true;
    }
  }
  return true;
}

void
TcpClient::run() {
  int heartbeatMs = heartbeat * 1000;
  while(true) {
    struct epoll_event events[MAX_EVENTS];
    log_debug << "wait events... ";
    int nfds = epoll_wait(epollFd, events, MAX_EVENTS, heartbeatMs);
    if (nfds == 0) {
      tunnelSendBuffer.sendTunnelState(tunnelServerFd, 0, TunnelPackage::STATE_HEARTBEAT);
    } else {
      if(nfds == -1) {
        log_error << "failed to epoll_wait";
        exit(EXIT_FAILURE);
      }
      log_debug << "nfds.size = " << nfds;
      for(int i = 0; i < nfds; i++) {
        int eventFd = events[i].data.fd;
        int count =0;
        if (handleTunnelClient(events[i].events, eventFd)) {
          count +=1;
        }
        if (handleTrafficServer(events[i].events, eventFd)) {
          count +=1;
        }
        log_debug << "handle count: " << count << ", eventFd: " << eventFd << ", " << addrLocal(eventFd) << ", " << addrRemote(eventFd);
      }
      recycleFd();
    }
  }
}
