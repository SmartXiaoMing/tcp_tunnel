//
// Created by mabaiming on 19-3-28.
//

#include "endpoint_server.h"

#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>

#include "utils.h"

void
EndpointServer::create(int fd) {
  fd_ = fd;
  fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL, 0) | O_NONBLOCK);
  ev_.data.ptr = this;
  ev_.events = (EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLERR);
  epoll_ctl(Endpoint::epollFd, EPOLL_CTL_ADD, fd_, &ev_);
}

void
EndpointServer::handleEvent(int events) {

  INFO("enter onNewClientTraffic, events:%d", events);
  if (events & EPOLLIN) {
    struct sockaddr_in addr;
    socklen_t len = sizeof(sockaddr_in);
    int acfd = accept(fd_, (struct sockaddr*) &addr, &len);
    if (acfd < 0) {
      return;
    }
    callback_(this, acfd);
  }
}

void
EndpointServer::updateEvent() {}
