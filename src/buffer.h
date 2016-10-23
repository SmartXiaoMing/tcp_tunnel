//
// Created by mabaiming on 16-10-23.
//

#ifndef TCP_TUNNEL_BUFFER_H
#define TCP_TUNNEL_BUFFER_H

#include "common.h"
#include "logger.h"
#include "tunnel_package.h"

#include <string>

using namespace std;
using namespace Common;

class Buffer {
public:
  static const int MaxSize = 1024 * 1024;

  Buffer(): buffer(), closeOnEmpty(false) {}
  string buffer;
  bool closeOnEmpty;

  bool isFull() {
    return buffer.size() >= MaxSize;
  }

  int send(int fd) {
    if (buffer.empty() && closeOnEmpty) {
      return -1;
    }
    int n = ::send(fd, buffer.c_str(), buffer.size(), MSG_NOSIGNAL);
    if (n > 0) {
      buffer.assign(buffer.begin() + n, buffer.end());
      return n;
    } else if (isGoodCode()) {
      return 0;
    }
    return -1;
  }

  int send(int fd, const char* data, int n) {
    buffer.append(data, n);
    return send(fd);
  }

  int send(int fd, const string& data) {
    buffer.append(data);
    return send(fd);
  }

  bool sendTunnelMessage(int fd, int cid, char state, const string& message) {
    TunnelPackage package;
    package.fd = cid;
    package.state = state;
    package.message.assign(message);
    string result;
    package.encode(result);
    log_debug << "send, " << addrLocal(fd)
              << " -[cid=" << cid << ",state=" << package.getState()
              << ",length=" << package.message.size() << "]-> "
              <<  addrRemote(fd);
    buffer.append(result);
    return send(fd);
  }

  bool sendTunnelState(int fd, int cid, char state) {
    string result;
    return sendTunnelMessage(fd, cid, state, result);
  }

  bool sendTunnelTraffic(int fd, int cid, const string& message) {
    return sendTunnelMessage(fd, cid, TunnelPackage::STATE_TRAFFIC, message);
  }

  int recv(int fd) {
    if (isFull()) {
      return 0;
    }
    char buf[BUFFER_SIZE];
    int len = ::recv(fd, buf, BUFFER_SIZE, 0);
    if (len <= 0) {
      return isGoodCode() ? 0 : -1;
    } else {
      buffer.append(buf, len);
      return len;
    }
  }

  int recvToFull(int fd) {
    int n = 0;
    while (!isFull()) {
      char buf[BUFFER_SIZE];
      int len = ::recv(fd, buf, BUFFER_SIZE, 0);
      if (len <= 0) {
        return isGoodCode() ? n : -1;
      } else {
        buffer.append(buf, len);
        n += len;
      }
    }
    return n;
  }

  int recvAsFrame(int fd, int cid) {
    if (isFull()) {
      return 0;
    }
    char buf[BUFFER_SIZE];
    int len = ::recv(fd, buf, BUFFER_SIZE, 0);
    if (len <= 0) {
      return isGoodCode() ? 0 : -1;
    } else {
      log_debug << "recv, " << addrLocal(fd)
        << " <-[length=" << len << "]- "
        <<  addrRemote(fd);
      string encodeResult;
      TunnelPackage::encode(encodeResult, cid, TunnelPackage::STATE_TRAFFIC, string(buf, len));
      buffer.append(encodeResult);
      return len;
    }
  }

  bool needClosed() {
    return closeOnEmpty && buffer.empty();
  }

  void push(const char* data, int size) {
    buffer.append(data, size);
  }

  void push(const string& data) {
    buffer.append(data);
  }
};
#endif //TCP_TUNNEL_BUFFER_H
