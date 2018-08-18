//
// Created by mabaiming on 18-8-14.
//

#include <arpa/inet.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "endpoint_client.h"
#include "center.h"

Center* EndpointClient::sCenter = NULL;

EndpointClient*
EndpointClient::create(int id, int type, const char* ip, int port) {
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
  if (connect(fd, (struct sockaddr *) &addr, sizeof(struct sockaddr)) < 0) {
    WARN("failed to connect, ip:%s, port:%d\n", ip, port);
    return NULL;
  } else {
    INFO("success to connect,ip:%s, port:%d\n", ip, port);
  }
  EndpointClient *leaf = new EndpointClient(id, type, fd);
  return leaf;
}

void
EndpointClient::setCenter(Center* center) {
  sCenter = center;
}

int
EndpointClient::getId() {
  return id_;
}

int
EndpointClient::getType() {
  return type_;
}

void
EndpointClient::handleEvent(int events) {
  DEBUG("handleEvent:%d, for id:%d\n", events, id_);
  sUpdateSet.insert(this);
  if (broken_) {
    return;
  }
  if ((events & EPOLLERR) || (events & EPOLLHUP)) {
    broken_ = true;
    sCenter->notifyBrokenFor(this);
    return;
  }
  int state = 0;
  if (events & EPOLLIN) {
    while (true) {
      int remain = sCenter->getRemainBufferSizeFor(this);
      DEBUG("readableSize:%d, for id:%d\n", remain, id_);
      if (remain <= 0) {
        break;
      }
      char buf[remain];
      int len = recv(fd_, buf, remain, 0);
      DEBUG("readSize:%d, for id:%d\n", len, id_);
      if (len == 0) {
        broken_ = true;
        sCenter->notifyBrokenFor(this);
        return;
      } else if (len > 0) {
        sCenter->appendDataToBufferFor(this, buf, len);
      } else if (len < 0) {
        if (!isGoodCode()) {
          DEBUG("read error, for id:%d\n", id_);
          broken_ = true;
          sCenter->notifyBrokenFor(this);
          return;
        }
        break; // ignore for EAGAIN...
      }
    }
  }
  if (events & EPOLLOUT) {
    int size = buffer_.size();
    DEBUG("writabeSize:%d, for id:%d\n", size, id_);
    if (size > 0) {
      int len = send(fd_, buffer_.data(), size, MSG_NOSIGNAL);
      if (len < 0) {
        if  (!isGoodCode()) {
          DEBUG("write error, for id:%d, fd:%d\n", id_, fd_);
          broken_ = true;
          sCenter->notifyBrokenFor(this);
        }
        // else ignore for EAGAIN
      } else if (len > 0) {
        buffer_.erase(0, len);
        sCenter->notifyWritableFor(this);
      }
    }
    if (eofForWrite_ && buffer_.empty()) {
      DEBUG("write finished, for id:%d, fd:%d\n", id_, fd_);
      broken_ = true;
      sCenter->notifyBrokenFor(this);
      return;
    }
  }
}

int
EndpointClient::getWriteBufferRemainSize() {
  return BufferCapacity - buffer_.size();
}

int
EndpointClient::appendDataToWriteBuffer(const char* data, int size) {
  buffer_.append(data, size);
  sUpdateSet.insert(this);
  return size;
}

void
EndpointClient::setWriterBufferEof() {
  eofForWrite_ = true;
  sUpdateSet.insert(this);
}

void
EndpointClient::setBroken() {
  broken_ = true;
  sUpdateSet.insert(this);
}

void
EndpointClient::notifyCenterIsWritable() {
  sUpdateSet.insert(this);
}

void
EndpointClient::updateEvent() {
  if (broken_) {
    sRecycleSet.insert(this);
    return;
  }
  int newEvent = ev_.events;
  if (sCenter->getRemainBufferSizeFor(this) > 0) {
    newEvent |= EPOLLIN;
  } else {
    newEvent &= ~EPOLLIN;
  }
  if (buffer_.size() > 0) {
    newEvent |= EPOLLOUT;
  } else {
    newEvent &= ~EPOLLOUT;
  }
  if (newEvent != ev_.events) {
    DEBUG("update event:%d, for id:%d, fd:%d\n", newEvent, id_, fd_);
    ev_.events = newEvent;
    epoll_ctl(sEpollFd, EPOLL_CTL_MOD, fd_, &ev_);
  }
}
