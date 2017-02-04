//
// Created by mabaiming on 16-12-20.
//

#ifndef TCP_TUNNEL_EVENT_MANAGER_H
#define TCP_TUNNEL_EVENT_MANAGER_H

#include "buffer.hpp"
#include "logger.h"
#include "frame.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <map>
#include <memory>

using namespace std;

class EventManager {
public:
  struct ListenInfo {
      string ip;
      int port;
      int type;
      ListenInfo() {}
      ListenInfo(const string& ip_, int port_, int type_)
          : ip(ip_), port(port_), type(type_) {}
  };

  EventManager() {
    epollFd = epoll_create1(0);
    if(epollFd < 0) {
      log_error << "failed to epoll_create1";
      exit(EXIT_FAILURE);
    }
  }
 ~EventManager() {
    // program exits, and all fds are clean up, so we have to do nothing
  }
  virtual void onBufferCreated(
    shared_ptr<Buffer> buffer, const ListenInfo& listenInfo) = 0;
  virtual bool exchangeData() = 0;
  virtual int idle() = 0;

  static const int FD_TYPE_TUNNEL = 1;
  static const int FD_TYPE_TRAFFIC = 2;
  static const int FD_TYPE_MONITOR = 3;

  map<int, shared_ptr<Buffer>> bufferMap;
  map<int, ListenInfo> acceptFdMap;

  void listen(const string& ip, int port, int connectionCount, int type) {
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
    int result = ::bind(fd, (struct sockaddr *)&addr, sizeof(sockaddr));
    if (result < 0) {
      log_error << "failed to bind, port: " << port;
      exit(EXIT_FAILURE);
    }
    result = ::listen(fd, connectionCount);
    if (result < 0) {
      log_error << "failed to listen, port: " << port;
      exit(EXIT_FAILURE);
    }

    if (registerFd(fd) < 0) {
      log_error << "failed to registerFd: " <<  fd;
      exit(EXIT_FAILURE);
    }
    acceptFdMap[fd] = ListenInfo(ip, port, type);
  }

  bool accept(int eventFd, int events) {
    map<int, ListenInfo>::iterator it = acceptFdMap.find(eventFd);
    if (it == acceptFdMap.end()) {
      return false;
    }
    struct sockaddr_in addr;
    socklen_t sin_size = sizeof(addr);
    int clientFd = ::accept(eventFd, (struct sockaddr *) &addr, &sin_size);
    if (clientFd < 0) {
      log_error << "failed to accept client: " << FdToAddr(clientFd, false);
      exit(EXIT_FAILURE);
    } else {
      log_info << "success accept client: " << FdToAddr(clientFd, false);
    }
    registerFd(clientFd);
    shared_ptr<Buffer> buffer(new Buffer(clientFd));
    bufferMap[clientFd] = buffer;
    shared_ptr<Buffer> buffer2(new Buffer(buffer->reverse()));
    onBufferCreated(buffer2, it->second);
    return true;
  }

  shared_ptr<Buffer> connect(const string& ip, int port) {
    int fd = socket(PF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
      log_error << "failed to socket";
      return shared_ptr<Buffer>();
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip.c_str());
    addr.sin_port = htons(port);
    int result = ::connect(fd, (struct sockaddr *)&addr, sizeof(struct sockaddr));
    shared_ptr<Buffer> buffer(new Buffer(fd));
    if(result < 0) {
      log_error << "failed to connect " << ip << ":" << port;
      buffer->close();
    } else {
      registerFd(fd);
      bufferMap[fd] = buffer;
    }
    return shared_ptr<Buffer>(new Buffer(buffer->reverse()));
  }

  bool handleEvent(int eventFd, int events) {
    map<int, shared_ptr<Buffer>>::iterator it = bufferMap.find(eventFd);
    if (it == bufferMap.end()) {
      return false;
    }
    shared_ptr<Buffer> buffer = it->second;
    if ((events & EPOLLRDHUP) || (events & EPOLLERR)) {
      log_debug << "event " << events << " occurs, close "
        << buffer->getName() << ", fd: " << eventFd << ", error: " << errno;
      buffer->close();
      return true;
    }
    if (events & EPOLLIN) {
      int maxSize = buffer->writableSize();
      if (maxSize > 0) {
        char buf[maxSize];
        int len = recv(eventFd, buf, maxSize, 0);
        if (len > 0) {
          buffer->write(buf, len);
        } else if (len == 0) {
          log_debug << "read EOF, close " << buffer->getName() << ", fd: " << eventFd;
          buffer->close();
          return true;
        } else if (!isGoodCode()) {
          log_debug << "read error, close " << buffer->getName() << ", fd: " << eventFd;
          buffer->close();
          return true;
        }
      }
    }
    if (events & EPOLLOUT) {
      int maxSize = buffer->readableSize();
      if (maxSize > 0) {
        int n = ::send(eventFd, buffer->getReadData(), maxSize, MSG_NOSIGNAL);
          if (n > 0) {
            log_debug << "write bytes: " << n << "/" << maxSize << "/"
              << buffer->getReadBufferSize() << ", for " << buffer->getName();
            buffer->popRead(n);
          } else if (n < 0 && !isGoodCode()) {
            log_debug << "write error, close " << buffer->getName()
              << ", errno: " << errno;
            buffer->close();
            return true;
          }
          log_debug << "write bytes: " << n << "/" << maxSize << "/"
            << buffer->getReadBufferSize() << ", for "
            << buffer->getName();
      }
    }
    return true;
  }

  void run() {
    int idleCount = 0;
    while(true) {
      struct epoll_event events[MAX_EVENTS];
      int nfds = epoll_wait(epollFd, events, MAX_EVENTS, 2);
      if(nfds == -1) {
        log_error << "failed to epoll_wait";
        exit(EXIT_FAILURE);
        return;
      }
      for(int i = 0; i < nfds; i++) {
        accept(events[i].data.fd, events[i].events)
        || handleEvent(events[i].data.fd, events[i].events);
      }
      if (!exchangeData()) {
        usleep(100000);
        idleCount += 1;
        if (idleCount >= 600) {
          idle();
          idleCount = 0;
        }
      } else {
        idleCount = 0;
      }
      recycle();
    }
  }

  void recycle() {
    map<int, shared_ptr<Buffer>>::iterator it = bufferMap.begin();
    while (it != bufferMap.end()) {
      shared_ptr<Buffer>& buffer = it->second;
      bool toClean = false;
      if (buffer->isClosed()) {
        int fd = it->first;
        it = bufferMap.erase(it);
        cleanUpFd(fd);
      } else {
        it++;
      }
    }
  }

  int cleanUpFd(int fd) {
    if (fd < 0) {
      return -1;
    }
    struct epoll_event ev;
    epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, &ev);
    close(fd);
  }

  int registerFd(int fd) {
    if (fd < 0) {
      return -1;
    }
    setNonblock(fd);
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLRDHUP;
    ev.data.fd = fd;
    int result = epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &ev);
    if (result < 0) {
      log_error << "failed epoll_ctl add fd: "
        << fd << ", events: " << ev.events;
    }
    return result;
  }

  int setNonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    int result = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (result < 0) {
      log_error << "failed to set NONBLOCK, fd: " << fd;
    }
    return result;
  }
protected:
  int epollFd;
};
#endif //TCP_TUNNEL_EVENT_MANAGER_H