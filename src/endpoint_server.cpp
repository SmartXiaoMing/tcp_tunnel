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
  char str[30];
  INFO("[tcp] %s --- * listen", fdToLocalAddr(fd_, str));
}

void
EndpointServer::handleEvent(int events) {
  if (events & EPOLLIN) {
    struct sockaddr_in addr;
    socklen_t len = sizeof(sockaddr_in);
    int acfd = accept(fd_, (struct sockaddr*) &addr, &len);
    if (acfd < 0) {
      return;
    }
    char s1[30], s2[30];
    INFO("[tcp] %s <--- %s connected", fdToLocalAddr(acfd, s1), fdToPeerAddr(acfd, s2));
    callback_(this, acfd);
  }
}

void
EndpointServer::updateEvent() {}