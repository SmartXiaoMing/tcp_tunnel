//
// Created by mabaiming on 18-8-14.
//

#include <arpa/inet.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>

#include "endpoint_server.h"
#include "center.h"
#include "center_server.h"

int
EndpointServer::getType() {
  return type_;
}

EndpointClient*
EndpointServer::getTunnel() {
  return tunnel_;
}

bool
EndpointServer::match(const string& group, const string& name, EndpointClient* client) {
  if (group_ == group && name_ == name) {
    tunnel_ = client;
    return true;
  }
  return false;
}

void
EndpointServer::setCenter(CenterServer* center) {
  center_ = center;
}

EndpointServer*
EndpointServer::create(int type, const char* ip, int port,
  const char* group, const char* name, const char* remoteIp, int remotePort) {
  int fd = socket(PF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    WARN("failed to socket, ip:%s, port:%d\n", ip, port);
    return NULL;
  }
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr(ip);
  addr.sin_port = htons(port);
  int v;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(v)) < 0) {
    ERROR("failed to setsockopt SO_REUSEADDR for ip:%s, port:%d\n", ip, port);
    close(fd);
    return NULL;
  }
  if (bind(fd, (struct sockaddr *)&addr, sizeof(sockaddr)) < 0) {
    ERROR("failed to bind for ip:%s, port:%d\n", ip, port);
    return NULL;
  }
  if (listen(fd, 1000000) < 0) {
    ERROR("failed to listen for ip:%s, port:%d\n", ip, port);
    return NULL;
  }
  EndpointServer* leaf = new EndpointServer(type, fd, group, name, remoteIp, remotePort);
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
  epoll_ctl(sEpollFd, EPOLL_CTL_ADD, fd, &leaf->ev_);
  return leaf;
}

void
EndpointServer::handleEvent(int events) {
  DEBUG("handleEvent:%d, for fd:%d\n", events, fd_);
  struct sockaddr_in addr;
  socklen_t sin_size = sizeof(addr);
  int acFd = accept(fd_, (struct sockaddr *) &addr, &sin_size);
  if (acFd < 0) {
    ERROR("failed to accept client");
  } else {
    INFO("success to accept client");
  }
  int id = ++Endpoint::sId;
  EndpointClient* endpoint = new EndpointClient(id, type_, acFd);
  center_->notifyNewClient(endpoint);
}

void
EndpointServer::updateEvent() {
  // do nothing
}
