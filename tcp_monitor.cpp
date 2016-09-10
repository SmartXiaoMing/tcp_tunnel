//
// Created by mabaiming on 16-8-29.
//
#include "tcp_monitor.h"

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
TcpMonitor::init(uint16_t monitorPort) {
  port = monitorPort;
  serverFd = prepare("127.0.0.1", port);
}

int
TcpMonitor::prepare(const string& ip, uint16_t port) {
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
TcpMonitor::handleMonitor(const struct epoll_event& event) {
  if (event.data.fd != serverFd) { // traffic from tunnel
    return false;
  }
  if ((event.events & EPOLLRDHUP) || (event.events & EPOLLERR)) {
    exit(EXIT_FAILURE);
  }

  if ((event.events & EPOLLIN) == 0) {
    return true;
  }

  char buf[BUFFER_SIZE];
  int len = recv(event.data.fd, buf, BUFFER_SIZE, 0);
  if (len <= 0) {
    exit(EXIT_FAILURE);
    return true;
  }
  buffer.append(buf, len);
  int offset = 0;
  int totalLength = buffer.length();
  while (offset < buffer.length()) {
    TunnelPackage package;
    int decodeLength = package.decode(buffer.c_str() + offset, totalLength - offset);
    if (decodeLength < 0) {
      exit(EXIT_FAILURE);
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
      case TunnelPackage::STATE_CLOSE: exit(EXIT_SUCCESS); break;
      case TunnelPackage::STATE_MONITOR_RESPONSE:
        cout << package.message << endl;
        break;
      default: log_warn << "ignore state: " << (int) package.state;
    }
  }
  if (offset > 0) {
    buffer.assign(buffer.begin() + offset, buffer.end());
  }
}

void
TcpMonitor::run(const string& cmd) {
  while(true) {
    struct epoll_event events[MAX_EVENTS];
    int nfds = epoll_wait(epollFd, events, MAX_EVENTS, -1);
    if(nfds == -1) {
      log_error << "failed to epoll_wait";
      exit(EXIT_FAILURE);
    }
    for(int i = 0; i < nfds; i++) {
      handleMonitor(events[i]);
    }
  }
}
