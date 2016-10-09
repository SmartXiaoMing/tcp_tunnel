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
  serverFd = connectServer("127.0.0.1", port);
}

bool
TcpMonitor::handleMonitor(uint32_t events, int eventFd) {
  if (eventFd != serverFd) { // traffic from tunnel
    return false;
  }
  if ((events & EPOLLRDHUP) || (events & EPOLLERR)) {
    exit(EXIT_FAILURE);
  }

  if (events & EPOLLIN) {
		char buf[BUFFER_SIZE];
		int len = recv(eventFd, buf, BUFFER_SIZE, 0);
		if (len > 0) {
		  recvBuffer.append(buf, len);
		  int offset = 0;
		  int totalLength = recvBuffer.length();
		  while (offset < recvBuffer.length()) {
		    TunnelPackage package;
		    int decodeLength
		        = package.decode(recvBuffer.c_str() + offset, totalLength - offset);
		    if (decodeLength < 0) {
		      exit(EXIT_FAILURE);
		      return true;
		    }
		    if (decodeLength == 0) { // wait next time to read
		      break;
		    }
		    offset += decodeLength;
		    log_debug << "recv, " << addrLocal(eventFd)
		        << " <-[fd=" << package.fd << ",state=" << package.getState()
		        << ",length=" << package.message.size() << "]- "
		        <<  addrRemote(eventFd);
		    switch (package.state) {
		      case TunnelPackage::STATE_CLOSE: exit(EXIT_SUCCESS); break;
		      case TunnelPackage::STATE_MONITOR_RESPONSE:
		        cout << package.message;
		        break;
		      default: log_warn << "ignore state: " << (int) package.state;
		    }
		  }
		  if (offset > 0) {
		    recvBuffer.assign(recvBuffer.begin() + offset, recvBuffer.end());
		  }
		} else if (!isGoodCode()) {
      exit(EXIT_FAILURE);
      return true;
    }
  }
  if (events & EPOLLOUT) {
    if (!send(sendBuffer, eventFd)) {
      exit(EXIT_FAILURE);
      return true;
    }
  }
}

void
TcpMonitor::run(const string& cmd) {
  sendTunnelMessage(sendBuffer, serverFd, 0, TunnelPackage::STATE_MONITOR_REQUEST, cmd);
  while(true) {
    struct epoll_event events[MAX_EVENTS];
    int nfds = epoll_wait(epollFd, events, MAX_EVENTS, -1);
    if(nfds == -1) {
      log_error << "failed to epoll_wait";
      exit(EXIT_FAILURE);
    }
    for(int i = 0; i < nfds; i++) {
      int eventFd = events[i].data.fd;
      handleMonitor(events[i].events, eventFd);
    }
  }
}
