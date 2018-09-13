//
// Created by mabaiming on 18-8-16.
//

#include <unistd.h>
#include <string.h>

#include "center.h"
#include "frame.hpp"
#include "utils.h"

void
Center::prepare(const char* host, int port, const char* group, const char* name) {
  while (trunk_ == NULL) {
    char ip[30];
    if (selectIp(host, ip, 29)) {
      INFO("[%-21s] success to select ip:%s for host:%s\n", "center", ip, host);
      trunk_ = Endpoint::create(0, Endpoint::TYPE_TUNNEL, ip, port);
    } else {
      ERROR("[%-21s] failed to select ip:%s for host:%s\n", "center", ip, host);
    }
    if (trunk_ == NULL) {
      ERROR("[%-21s] failed to prepare and wait 30 seoncds ...\n", "center");
      sleep(30);
    } else {
      char data[256];
      int size = sprintf(data, "group=%s&name=%s", group, name);
      sendDataToTunnel(STATE_LOGIN, NULL, data, size);
    }
  }
}

int
Center::getRemainBufferSizeFor(Endpoint* endpoint) {
  if (trunk_ == NULL) {
    return 0;
  }
  if (endpoint == trunk_) {
    return BufferCapacity - frameBuffer_.size();
  } else {
    return trunk_->getWriteBufferRemainSize() - FrameHeadSize;
  }
}

void
Center::appendDataToBufferFor(Endpoint* endpoint, const char* data, int size) {
  if (trunk_ == NULL) {
    return;
  }
  if (endpoint == trunk_) {
    frameBuffer_.append(data, size);
    handleData();
  } else {
    string buffer;
    Frame::encodeTo(buffer, STATE_DATA, endpoint->getAddr(), data, size);
    trunk_->appendDataToWriteBuffer(buffer.data(), buffer.size());
  }
}

void
Center::notifyWritableFor(Endpoint* endpoint) {
  if (trunk_ == NULL) {
    return;
  }
  if (endpoint == trunk_) {
    map<const uint8_t*, Endpoint*>::iterator it = leaves_.begin();
    while (it != leaves_.end()) {
      Endpoint* leaf = it->second;
      leaf->notifyCenterIsWritable();
      it++;
    }
  } else {
    handleData();
  }
}

void
Center::notifyBrokenFor(Endpoint* endpoint) {
  if (trunk_ == NULL) {
    return;
  }
  if (endpoint == trunk_) {
    reset();
  } else {
    sendDataToTunnel(STATE_CLOSE, endpoint->getAddr(), NULL, 0);
    leaves_.erase(endpoint->getAddr());
  }
}

void
Center::sendDataToTunnel(uint8_t state, const uint8_t* addr, const char* data, int size) {
  if (trunk_ == NULL) {
    return;
  }
  string buffer;
  Frame::encodeTo(buffer, state, addr, data, size);
  trunk_->appendDataToWriteBuffer(buffer.data(), buffer.size());
}

void Center::reset() {
  frame_.state = STATE_NONE;
  memset(frame_.addr, 0, sizeof(frame_.addr));
  frameBuffer_.clear();
  map<const uint8_t*, Endpoint*>::iterator it = leaves_.begin();
  while (it != leaves_.end()) {
    Endpoint* leaf = it->second;
    leaf->setBroken();
    it++;
  }
  leaves_.clear();
  if (trunk_) {
    trunk_->setBroken();
    trunk_ = NULL;
  }
}

void
Center::handleData() {
  while (processFrame()) {
    frame_.reset();
    int parseSize = Frame::parse(frame_, frameBuffer_);
    if (parseSize == 0) {
      return;
    }
    if (parseSize < 0) {
      reset();
      return;
    }
    frameBuffer_.erase(0, parseSize);
  }
}

bool
Center::processFrame() {
  if (frame_.state == STATE_NONE) {
    return true;
  }
  DEBUG("[%-21s] process frame, state:%s, addr:%s, message[%zd]:%-*s\n",
        "center", frame_.stateToStr(), addrToStr(frame_.addr), frame_.message.size(),
        min((int)frame_.message.size(), 21), frame_.message.c_str());
  if (frame_.state == STATE_CONNECT) {;
    map<const uint8_t*, Endpoint*>::iterator it = leaves_.find(frame_.addr);
    if (it == leaves_.end()) { // must not exist
      do {
        int colon = frame_.message.find(':');
        if (colon < 0) {
          ERROR("[%-21s] invalid frame, state:%s, addr:%s, wrong ip port:%s\n",
                "center", frame_.stateToStr(), addrToStr(frame_.addr), frame_.message.c_str());
          break;
        }
        frame_.message[colon] = '\0';
        const char* ip = frame_.message.data();
        int port = 80;
        if (sscanf(frame_.message.data() + colon + 1, "%d", &port) != 1) {
          ERROR("[%-21s] invalid frame, state:%s, addr:%s, wrong ip port:%s\n",
                "center", frame_.stateToStr(), addrToStr(frame_.addr), frame_.message.c_str());
          break;
        }
        Endpoint* leaf = Endpoint::create(frame_.addr, Endpoint::TYPE_TRAFFIC, ip, port);
        if (leaf == NULL) {
          break;
        }
        leaves_[frame_.addr] = leaf;
      } while (false);
    } else {
      ERROR("[%-21s] addr exists\n", addrToStr(frame_.addr));
    }
  } else if (frame_.state == STATE_DATA) {
    map<const uint8_t*, Endpoint*>::iterator it = leaves_.find(frame_.addr);
    if (it != leaves_.end()) {
      if (it->second->getWriteBufferRemainSize() > 0) {
        it->second->appendDataToWriteBuffer(frame_.message.data(), frame_.message.size());
      } else {
        ERROR("[%-21s] the buffer is full\n", addrToStr(frame_.addr));
        return false; // NOTE
      }
    } else {
      ERROR("[%-21s] addr does not exists\n", addrToStr(frame_.addr));
    }
  } else if (frame_.state == STATE_CLOSE) {
    map<const uint8_t*, Endpoint*>::iterator it = leaves_.find(frame_.addr);
    if (it != leaves_.end()) {
      it->second->setWriterBufferEof();
      leaves_.erase(it);
    } else {
      ERROR("[%-21s] addr does not exists\n", addrToStr(frame_.addr));
    }
  }
  // else ignore
  frame_.state = STATE_NONE;
  return true;
}
