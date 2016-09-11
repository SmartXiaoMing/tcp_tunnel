//
// Created by mabaiming on 16-9-4.
//

#ifndef TCP_TUNNEL_TCP_BASE_H
#define TCP_TUNNEL_TCP_BASE_H

#include "logger.h"
#include "tunnel_package.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

class TcpBase {
public:
  struct TunnelClientInfo {
    int count;
    string buffer;
    bool verified;

    TunnelClientInfo() : count(0), buffer(), verified(false) {}
    TunnelClientInfo(bool verified_)
        : count(0), buffer(), verified(verified_) {}
  };

  TcpBase() {
    epollFd = epoll_create1(0);
    if(epollFd < 0) {
      log_error << "failed to epoll_create1";
      exit(EXIT_FAILURE);
    }
  }

  ~TcpBase() {
    // program exits, and all fds are clean up, so we have to do nothing
  }

  int acceptClient(int serverFd) {
    struct sockaddr_in addr;
    socklen_t sin_size = sizeof(addr);
    int clientFd = accept(serverFd, (struct sockaddr *) &addr, &sin_size);
    if (clientFd < 0) {
      log_error << "failed to accept client, serverFd: " << serverFd;
      exit(EXIT_FAILURE);
    }
    log_info << "accept client, ip: " << inet_ntoa(addr.sin_addr) << ", port: "
        << addr.sin_port;
    registerFd(clientFd);
    return clientFd;
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

  int connectServer(const string& ip, uint16_t port) {
    int fd = socket(PF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
      log_error << "failed to socket";
      return fd;
    }
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
    log_info << "connected for " << ip << ":" << port;
    registerFd(fd);
    return fd;
  }

  int prepare(const string& ip, uint16_t port, int connection) {
    int fd = socket(PF_INET, SOCK_STREAM, 0);
    if(fd < 0) {
      log_error << "failed to create socket!";
      exit(EXIT_FAILURE);
    }
    int v;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(v)) < 0) {
      log_error << "failed to setsockopt: " << SO_REUSEADDR;
      close(fd);
      exit(EXIT_FAILURE);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    addr.sin_port = htons(port);
    int result = bind(fd, (struct sockaddr *)&addr, sizeof(sockaddr));
    if (result < 0) {
      log_error << "failed to bind, port: " << port << "\n";
      exit(EXIT_FAILURE);
    }

    result = listen(fd, connection);
    if (result < 0) {
      log_error << "failed to listen, port: " << port;
      exit(EXIT_FAILURE);
    }

    if (registerFd(fd) < 0) {
      log_error << "failed to registerFd: " <<  fd;
      exit(EXIT_FAILURE);
    }

    return fd;
  }

  int registerFd(int fd) {
    if (fd < 0) {
      log_warn << "invalid fd: " << fd;
      return -1;
    }
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLERR | EPOLLHUP;
    ev.data.fd = fd;
    int result = epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &ev);
    if (result < 0) {
      log_error << "failed epoll_ctl add fd: "
          << fd << ", events: " << ev.events;
    }
    return result;
  }

  void sendTunnelMessage(int tunnelFd, int trafficFd, char state,
      const string& message) {
    TunnelPackage package;
    package.fd = trafficFd;
    package.state = state;
    package.message.assign(message);
    string result;
    package.encode(result);
    log_debug << "send, " << addrLocal(tunnelFd)
        << " -[fd=" << trafficFd << ",state=" << package.getState()
        << ",length=" << package.message.size() << "]-> "
        <<  addrRemote(tunnelFd);
    send(tunnelFd, result.c_str(), result.size(), 0);
  }

  void sendTunnelState(int tunnelFd, int trafficFd, char state) {
    string result;
    sendTunnelMessage(tunnelFd, trafficFd, state, result);
  }

  void sendTunnelTraffic(int tunnelFd, int trafficFd,
      const string& message) {
    sendTunnelMessage(
        tunnelFd, trafficFd, TunnelPackage::STATE_TRAFFIC, message
    );
  }

protected:
  int epollFd;
};


#endif //TCP_TUNNEL_TCP_BASE_H
