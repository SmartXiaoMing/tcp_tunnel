//
// Created by mabaiming on 16-9-4.
//

#ifndef TCP_TUNNEL_TCP_BASE_H
#define TCP_TUNNEL_TCP_BASE_H

#include "logger.h"
#include "tunnel_package.h"

#include <stdlib.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <sys/socket.h>

class TcpBase {
public:
  static const int SERVER_TUNNEL = 1;
  static const int SERVER_TRAFFIC = 2;
  static const int CLIENT_TUNNEL = 3;
  static const int CLIENT_TRAFFIC = 4;

  struct TunnelServerInfo {
      int fd;
      string ip;
      uint16_t port;

      TunnelServerInfo() : fd(-1), ip(""), port(0) {}

      TunnelServerInfo(int fd_, const char *ip_, uint16_t port_) : fd(fd_), ip(ip_), port(port_) {}
  };

  struct TunnelClientInfo {
      string ip;
      uint16_t port;
      int count;
      string buffer;

      TunnelClientInfo() : ip(""), port(0), count(0), buffer() {}

      TunnelClientInfo(const char *ip_, uint16_t port_) : ip(ip_), port(port_), count(0), buffer() {}
  };

  TcpBase(): isServer(false) {
    epollFd = epoll_create1(0);
    if(epollFd < 0) {
      log_error << "failed to epoll_create1";
      exit(EXIT_FAILURE);
    }
  }

  ~TcpBase() {
    // program exits, and all fds are clean up, so we have to do nothing
  }

  int cleanUpFd(int fd) {
    log_debug << " fd: " << fd;
    if (fd < 0) {
      return -1;
    }
    struct epoll_event ev;
    epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, &ev);
    close(fd);
  }

  int registerFd(int fd) {
    log_debug << " fd: " << fd;
    if (fd < 0) {
      log_warn << "invalid fd: " << fd;
      return -1;
    }
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLERR | EPOLLHUP;
    ev.data.fd = fd;
    int result = epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &ev);
    if (result < 0) {
      log_error << "faild epoll_ctl add fd: " << fd << ", events: " << ev.events;
    }
    return result;
  }

  void sendTunnelState(int tunnelFd, int trafficFd, char state) {
    TunnelPackage package;
    package.fd = trafficFd;
    package.state = state;
    package.message.clear();
    string result;
    package.encode(result);
    int n = send(tunnelFd, result.c_str(), result.size(), 0);
    if (!isServer) {
      log_debug << "send, trafficServer --> *tunnelClient -[fd="
        << trafficFd << ",state=" << package.getState() << ",length=" << "/" << n << package.message.size()
        << "]-> tunnelServer(" << tunnelFd << ") --> trafficClient";
    } else {
      log_debug << "send, trafficServer <-- tunnelClient(" << tunnelFd << ") <-[fd="
        << trafficFd << ",state=" << package.getState() << ",length=" << "/" << n << package.message.size()
        << "]- *tunnelServer <-- trafficClient";
    }
  }

  void sendTunnelTraffic(int eventFd, int tunnelFd, int trafficFd, const string& message) {
    TunnelPackage package;
    package.fd = trafficFd;
    package.state = TunnelPackage::STATE_TRAFFIC;
    package.message.assign(message);
    string result;
    package.encode(result);
    int n = send(tunnelFd, result.c_str(), result.size(), 0);
    if (!isServer) {
      log_debug << "send, trafficServer --> *tunnelClient(" << eventFd << ") -[fd="
        << trafficFd << ",state=" << package.getState() << ",length=" << n << "/" << package.message.size()
        << "]-> tunnelServer(" << tunnelFd << ") --> trafficClient";
    } else {
      log_debug << "send, trafficServer <-- tunnelClient(" << tunnelFd << ") <-[fd="
        << trafficFd << ",state=" << package.getState() << ",length=" << n << "/" << package.message.size()
        << "]- *tunnelServer(" << eventFd << ") <-- trafficClient";
    }
  }

protected:
  int epollFd;
  bool isServer;
};


#endif //TCP_TUNNEL_TCP_BASE_H
