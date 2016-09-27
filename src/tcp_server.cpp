//
// Created by mabaiming on 16-8-29.
//

#include "tcp_server.h"

#include "common.h"
#include "logger.h"
#include "tunnel_package.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <map>
#include <set>

using namespace std;
using namespace Common;

int
TcpServer::acceptMonitorClient(int serverFd) {
  int clientFd = acceptClient(serverFd);
  monitorClientMap[clientFd] = "";
  return clientFd;
}

int
TcpServer::acceptTrafficClient(int serverFd) {
  int clientFd = acceptClient(serverFd);
  trafficClientMap[clientFd] = TrafficClientInfo(serverFd, -1, -1);
  return assignTunnelClient(clientFd);
}

int
TcpServer::acceptTunnelClient(int serverFd) {
  int clientFd = acceptClient(serverFd);
  if (secret.empty()) {
    tunnelClientMap[clientFd] = TunnelClientInfo(TC_STATE_OK);
  } else {
    tunnelClientMap[clientFd] = TunnelClientInfo(TC_STATE_INVALID);
    if (!sendTunnelState(clientFd, 0, TunnelPackage::STATE_CHALLENGE_REQUEST)) {
      log_error << "send failed, fd: " << clientFd << ", "
        << addrLocal(clientFd) << " -> "
        << addrRemote(clientFd);
      cleanUpTunnelClient(clientFd);
      return -1;
    }
  }
  return clientFd;
}

int
TcpServer::assignTunnelClient(int trafficClientFd) {
  map<int, TrafficClientInfo>::iterator it
      = trafficClientMap.find(trafficClientFd);
  if (it == trafficClientMap.end()) {
    log_error << "invalid trafficClientFd: " << trafficClientFd;
    return -1;
  }
  int tunnelClientFd = chooseTunnelClient(it->second.trafficServerFd);
  if (tunnelClientFd < 0) {
    cleanUpTrafficClient(trafficClientFd, CTRL_ACTIVE);
    return -1;
  }
  if (it->second.connectId > 0) {
    connectIdMap.erase(it->second.connectId);
  }
  it->second.connectId = generateConnectId();
  it->second.tunnelClientFd = tunnelClientFd;
  connectIdMap[it->second.connectId] = trafficClientFd;
  int result = sendTunnelState(
    tunnelClientFd, it->second.connectId, TunnelPackage::STATE_CREATE
  );
  if (result < 0) {
    log_error << "send failed, fd: " << tunnelClientFd << ", "
      << addrLocal(tunnelClientFd) << " -> "
      << addrRemote(tunnelClientFd);
    cleanUpTunnelClient(tunnelClientFd);
    return assignTunnelClient(trafficClientFd);
  }
  return tunnelClientFd;
}

int
TcpServer::chooseTunnelClient(int trafficServerFd) {
  if (tunnelClientMap.empty()) {
    log_warn << "no available tunnelClient";
    return -1;
  }
  map<int, int>::iterator it1 = trafficServerMap.find(trafficServerFd);
  if (it1 != trafficServerMap.end() && it1->second > 0) {
    map<int, TunnelClientInfo>::iterator it2
        = tunnelClientMap.find(it1->second);
    if (it2->second.state == TC_STATE_OK) {
      log_debug << "use assigned fd: " << it1->second;
      return it1->second;
    } else {
      it2->second.count--;
      it1->second = -1;
    }
  }
  int tunnelClientCount = getAvailableTunnelClientCount();
  if (tunnelClientCount <= 0) {
    log_warn << "no available tunnelClient";
    return -1;
  }
  pair<const int, TunnelClientInfo>* bestTunnelClient = NULL;
  map<int, TunnelClientInfo>::iterator it2 = tunnelClientMap.begin();
  for (; it2 != tunnelClientMap.end(); ++it2) {
    if (it2->second.state == TC_STATE_OK) {
      if (bestTunnelClient == NULL
          || bestTunnelClient->second.count > it2->second.count) {
        bestTunnelClient = &(*it2);
      }
    }
  }
  if (bestTunnelClient != NULL) {
    trafficServerMap[trafficServerFd] = bestTunnelClient->first;
    bestTunnelClient->second.count++;
    log_debug << "assign " << addrRemote(bestTunnelClient->first)
        << " to " << addrLocal(trafficServerFd)
        << ", rule:" << bestTunnelClient->second.count;
    return bestTunnelClient->first;
  }
  log_error << "cannot find any tunnelClient, it is impossible!!";
  return -1;
}

void
TcpServer::cleanUpMonitorClient(int fd) {
  map<int, string>::iterator it = monitorClientMap.find(fd);
  if (it != monitorClientMap.end()) {
    monitorClientMap.erase(it);
    log_debug << "clean up monitorClient: " << addrRemote(fd);
  }
  cleanUpFd(fd);
}

void
TcpServer::cleanUpTrafficClient(int fd, int ctrl) {
  map<int, TrafficClientInfo>::iterator it = trafficClientMap.find(fd);
  if (it != trafficClientMap.end()) {
    if (ctrl == CTRL_ACTIVE && it->second.tunnelClientFd > 0) {
      int result = sendTunnelState(
        it->second.tunnelClientFd, fd, TunnelPackage::STATE_CLOSE
      );
      if (result < 0) {
        log_error << "send failed, fd: " << it->second.tunnelClientFd << ", "
          << addrLocal(it->second.tunnelClientFd) << " -> "
          << addrRemote(it->second.tunnelClientFd);
        // TODO bad affected cleanUpTunnelClient(it->second.tunnelClientFd);
      }
    }
    connectIdMap.erase(it->second.connectId);
    trafficClientMap.erase(it);
    log_debug << "clean up trafficClient: " << addrRemote(fd);
  }
  cleanUpFd(fd);
}

void
TcpServer::cleanUpTunnelClient(int fd) {
  map<int, int>::iterator it1 = trafficServerMap.begin();
  for (; it1 != trafficServerMap.end(); ++it1) {
    if (it1->second == fd) {
      it1->second = -1; // keep trafficServer free
    }
  }
  map<int, TrafficClientInfo>::iterator it2 = trafficClientMap.begin();
  for (; it2 != trafficClientMap.end();) {
    if (it2->second.tunnelClientFd == fd) {
      log_debug << "clean up trafficClient, fd: " << addrRemote(it2->first);
      connectIdMap.erase(it2->second.connectId);
      cleanUpFd(it2->first);
      trafficClientMap.erase(it2++);
    } else {
      ++it2;
    }
  }
  map<int, TunnelClientInfo>::iterator it3 = tunnelClientMap.find(fd);
  if (it3 != tunnelClientMap.end()) {
    log_debug << "clean up tunnelClient: " <<  addrRemote(fd);
    tunnelClientMap.erase(it3);
  }
  cleanUpFd(fd);
}

int
TcpServer::getAvailableTunnelClientCount() const {
  int count = 0;
  map<int, TunnelClientInfo>::const_iterator it = tunnelClientMap.begin();
  for (; it != tunnelClientMap.end(); ++it) {
    if (it->second.state == TC_STATE_OK) {
      count += 1;
    }
  }
  return count;
}

bool
TcpServer::handleMonitorClient(uint32_t events, int eventFd) {
  map<int, string>::iterator it = monitorClientMap.find(eventFd);
  if (it == monitorClientMap.end()) {
    return false;
  }
  if ((events & EPOLLRDHUP) || (events & EPOLLERR)) {
    cleanUpMonitorClient(eventFd);
    return true;
  }
  if ((events & EPOLLIN) == 0) {
    return true;
  }
  char buf[BUFFER_SIZE];
  int len = recv(eventFd, buf, BUFFER_SIZE, 0);
  if (len <= 0) {
    cleanUpMonitorClient(eventFd);
    return true;
  }
  it->second.append(buf, len);
  int offset = 0;
  int totalLength = it->second.length();
  while (offset < it->second.size()) {
    TunnelPackage package;
    int decodeLength
        = package.decode(it->second.c_str() + offset, totalLength - offset);
    if (decodeLength == 0) { // wait next time to read
      break;
    }
    offset += decodeLength;
    log_debug << "recv, " << addrLocal(eventFd)
        << " <-[cid=" << package.fd << ",state=" << package.getState()
        << ",length=" << package.message.size() << "]- "
        <<  addrRemote(eventFd);
    switch (package.state) {
      case TunnelPackage::STATE_MONITOR_REQUEST: {
        map<int, TunnelClientInfo>::iterator it2 = tunnelClientMap.begin();
        for (; it2 != tunnelClientMap.end(); ++it2) {
          string line = "tunnelClient\t"
              + addrRemote(it2->first).toAddr().toString()
              + "\t"
              + intToString(it2->second.count)
              + "\t"
              + it2->second.stateString()
              + "\n";
          sendTunnelMessage(
              eventFd, 0, TunnelPackage::STATE_MONITOR_RESPONSE, line
          );
        }
        map<int, int>::iterator it3 = trafficServerMap.begin();
        for (; it3 != trafficServerMap.end(); ++it3) {
          string line = "serverMapTunnelClient\t"
              + addrLocal(it3->first).toAddr().toString()
              + "\t"
              + addrRemote(it3->second).toAddr().toString()
              + "\n";
          sendTunnelMessage(
              eventFd, 0, TunnelPackage::STATE_MONITOR_RESPONSE, line
          );
        }
        map<int, TrafficClientInfo>::iterator it4 = trafficClientMap.begin();
        for (; it4 != trafficClientMap.end(); ++it4) {
          string line = "trafficMapTunnelClient\t"
              + addrRemote(it4->first).toAddr().toString()
              + "\t"
              + addrRemote(it4->second.tunnelClientFd).toAddr().toString()
              + "\n";
          sendTunnelMessage(
              eventFd, 0, TunnelPackage::STATE_MONITOR_RESPONSE, line
          );
        }
        sendTunnelState(eventFd, 0, TunnelPackage::STATE_CLOSE);
        cleanUpMonitorClient(eventFd);
        return true;
      }
      break;
      default: log_warn << "ignore state: " << (int) package.state;
    }
  }
  if (offset > 0) {
    it->second.assign(it->second.begin() + offset, it->second.end());
  }
  return true;
}

bool
TcpServer::handleTunnelClient(uint32_t events, int eventFd) {
  map<int, TunnelClientInfo>::iterator it = tunnelClientMap.find(eventFd);
  if (it == tunnelClientMap.end()) {
    return false;
  }
  if ((events & EPOLLRDHUP) || (events & EPOLLERR)) {
    cleanUpTunnelClient(eventFd);
    return true;
  }
  if ((events & EPOLLIN) == 0) {
    return true;
  }
  char buf[BUFFER_SIZE];
  int len = recv(eventFd, buf, BUFFER_SIZE, 0);
  if (len <= 0) {
    cleanUpTunnelClient(eventFd);
    return true;
  }
  it->second.buffer.append(buf, len);
  int offset = 0;
  int totalLength = it->second.buffer.length();
  while (offset < it->second.buffer.size()) {
    TunnelPackage package;
    int decodeLength = package.decode(
        it->second.buffer.c_str() + offset, totalLength - offset
    );
    if (decodeLength == 0) { // wait next time to read
      break;
    }
    offset += decodeLength;
    log_debug << "recv, " << addrLocal(eventFd)
        << " <-[cid=" << package.fd << ",state=" << package.getState()
        << ",length=" << package.message.size() << "]- "
        <<  addrRemote(eventFd);
    switch (package.state) {
      case TunnelPackage::STATE_HEARTBEAT: break;
      case TunnelPackage::STATE_CHALLENGE_RESPONSE: {
        if (package.message == secret) {
          if (it->second.state == TC_STATE_INVALID) {
            log_debug << "success to challenge: " << addrRemote(eventFd);
            it->second.state = TC_STATE_OK;
          }
        } else {
          log_warn << "failed to challenge" << addrRemote(eventFd)
              << ", secret: " << package.message;
          sendTunnelState(eventFd, 0, TunnelPackage::STATE_CLOSE);
          cleanUpTunnelClient(eventFd);
          return true; // no need to read more
        }
      } break;
      case TunnelPackage::STATE_CREATE_FAILURE: {
          it->second.state = TC_STATE_BROKEN;
          map<int, int>:: iterator it2 = connectIdMap.find(package.fd);
          if (it2 == connectIdMap.end()) {
            log_error << "invalid connectId :" << package.fd << ", from "
                << addrRemote(eventFd);
          } else {
            assignTunnelClient(it2->second);
          }
      } break;
      case TunnelPackage::STATE_CLOSE: {
        map<int, int>:: iterator it2 = connectIdMap.find(package.fd);
        if (it2 == connectIdMap.end()) {
          log_error << "invalid connectId :" << package.fd << ", from "
              << addrRemote(eventFd);
        } else {
          cleanUpTrafficClient(it2->second, CTRL_PASSIVE);
        }
      } break;
      case TunnelPackage::STATE_TRAFFIC: {
        map<int, int>:: iterator it2 = connectIdMap.find(package.fd);
        if (it2 == connectIdMap.end()) {
          log_error << "invalid connectId :" << package.fd << ", from "
              << addrRemote(eventFd);
        } else {
          int result = send(
            it2->second,
            package.message.c_str(),
            package.message.size(),
            MSG_NOSIGNAL
          );
          if (result < 0) {
            log_debug << "send failed, " << it2->second << ", "
                << addrLocal(it2->second)
                << " -[length=" << package.message.size() << "]-> "
                << addrRemote(it2->second);
            cleanUpTrafficClient(it2->second, CTRL_ACTIVE);
          } else {
            log_debug << "send, " << it2->second << ", "
              << addrLocal(it2->second)
              << " -[length=" << package.message.size() << "]-> "
              << addrRemote(it2->second);
          }
        }
      } break;
      default: log_warn << "ignore state: " << (int) package.state;
    };
  }
  if (offset > 0) {
    it->second.buffer.assign(
        it->second.buffer.begin() + offset, it->second.buffer.end()
    );
  }
  return true;
}

bool
TcpServer::handleTrafficClient(uint32_t events, int eventFd) {
  map<int, TrafficClientInfo>::iterator it = trafficClientMap.find(eventFd);
  if (it == trafficClientMap.end()) {
    return false;
  }
  if ((events & EPOLLRDHUP) || (events & EPOLLERR)) {
    cleanUpTrafficClient(eventFd, CTRL_ACTIVE);
    return true;
  }
  if ((events & EPOLLIN) == 0) {
    return true;
  }

  char buf[BUFFER_SIZE];
  int len = recv(eventFd, buf, BUFFER_SIZE, 0);
  log_debug << "recv, " << addrLocal(eventFd)
      << " <-[length=" << len << "]- "
      <<  addrRemote(eventFd);
  if (len <= 0) {
    cleanUpTrafficClient(eventFd, CTRL_ACTIVE);
    return true;
  }
  int result = sendTunnelTraffic(
    it->second.tunnelClientFd, it->second.connectId, string(buf, len)
  );
  if (result < 0) {
    log_error << "send failed, fd: " << it->second.tunnelClientFd << ", "
        << addrLocal(it->second.tunnelClientFd) << " -> "
        << addrRemote(it->second.tunnelClientFd);
    cleanUpTunnelClient(it->second.tunnelClientFd);
  }
  return true;
}

void
TcpServer::init(
    const string& tunnelIp, uint16_t tunnelPort, int tunnelConnection,
    const string& trafficIp, const vector<uint16_t>& trafficPortList,
    int trafficConnection,
    int monitorPort,
    const string& tunnelSecret) {
  secret = tunnelSecret;
  prepareTunnel(tunnelIp, tunnelPort, tunnelConnection);
  log_info << "listen tunnel(" << tunnelConnection << "): "
      << tunnelIp << ":" << tunnelPort;
  if (monitorPort > 0 && monitorPort < 65536) {
    string ip = "127.0.0.1";
    prepareMonitor(ip, monitorPort, 10);
    log_info << "listen monitor: " << ip << ":" << monitorPort;
  }
  for (int i = 0; i < trafficPortList.size(); ++i) {
    prepareTraffic(trafficIp, trafficPortList[i], trafficConnection);
    log_info << "listen traffic(" << trafficConnection << "): "
        << trafficIp << ":" << trafficPortList[i];
  }
}

int
TcpServer::prepareTraffic(const string& ip, uint16_t port, int connection) {
  int fd = prepare(ip, port, connection);
  trafficServerMap[fd] = -1;
  return fd;
}

int
TcpServer::prepareMonitor(const string& ip, uint16_t port, int connection) {
  int fd = prepare(ip, port, connection);
  if (fd <= 0) {
    log_error << "failed to prepareMonitor, addr: " << ip << ":" << port;
  } else {
    monitorServerFd = fd;
  }
  return fd;
}

int
TcpServer::prepareTunnel(const string& ip, uint16_t port, int connection) {
  tunnelServerFd = prepare(ip, port, connection);
  return tunnelServerFd;
}

void
TcpServer::run() {
  while(true) {
    struct epoll_event events[MAX_EVENTS];
    int nfds = epoll_wait(epollFd, events, MAX_EVENTS, -1);
    if(nfds == -1) {
      log_error << "failed to epoll_wait";
      exit(EXIT_FAILURE);
    }
    for(int i = 0; i < nfds; i++) {
      int eventFd = events[i].data.fd;
      if (eventFd == tunnelServerFd) {
        acceptTunnelClient(tunnelServerFd);
        continue;
      }
      if (trafficServerMap.find(eventFd) != trafficServerMap.end()) {
        acceptTrafficClient(eventFd);
        continue;
      }
      if (monitorServerFd > 0 && eventFd == monitorServerFd) {
        acceptMonitorClient(eventFd);
        continue;
      }
      handleTunnelClient(events[i].events, eventFd)
          || handleTrafficClient(events[i].events, eventFd)
          || handleMonitorClient(events[i].events, eventFd);
    }
    recycleFd();
  }
}


