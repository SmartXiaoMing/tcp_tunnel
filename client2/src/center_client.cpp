//
// Created by mabaiming on 18-8-16.
//

#include <unistd.h>

#include "center_client.h"
#include "frame.hpp"
#include "utils.h"

void
ClientCenter::prepare(const char* host, int port, const char* group, const char* name) {
  while (trunk_ == NULL) {
    char ip[30];
    if (selectIp(host, ip, 29)) {
      INFO("success select ip:%s for host:%s\n", ip, host);
      trunk_ = Endpoint::create(0, ip, port);
    } else {
      ERROR("failed to select ip for host:%s\n", host);
    }
    if (trunk_) {
      ERROR("failed to prepare and wait 30 seoncds ...\n");
      sleep(30);
    }
  }
  char data[256];
  int size = sprintf(data, "group=%s&name=%s", group, name);
  sendDataToTunnel(STATE_LOGIN, 0, data, size);
}

int
ClientCenter::getRemainBufferSizeFor(Endpoint* endpoint) {
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
ClientCenter::appendDataToBufferFor(Endpoint* endpoint, const char* data, int size) {
  if (trunk_ == NULL) {
    return;
  }
  if (endpoint == trunk_) {
    frameBuffer_.append(data, size);
    handleData();
  } else {
    trunk_->appendDataToWriteBuffer(data, size);
  }
}

void
ClientCenter::notifyWritableFor(Endpoint* endpoint) {
  if (trunk_ == NULL) {
    return;
  }
  if (endpoint == trunk_) {
    map<int, Endpoint*>::iterator it = leaves_.begin();
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
ClientCenter::notifyBrokenFor(Endpoint* endpoint) {
  if (trunk_ == NULL) {
    return;
  }
  if (endpoint == trunk_) {
    reset();
  } else {
    sendDataToTunnel(STATE_CLOSE, endpoint->getId(), NULL, 0);
    leaves_.erase(endpoint->getId());
  }
}

void
ClientCenter::sendDataToTunnel(uint8_t state, int id, const char* data, int size) {
  if (trunk_ == NULL) {
    return;
  }
  string buffer;
  Frame::encodeTo(buffer, state, id, data, size);
  trunk_->appendDataToWriteBuffer(buffer.data(), buffer.size());
}

void ClientCenter::reset() {
  frame_.state = STATE_NONE;
  frame_.id = 0;
  frameBuffer_.clear();
  map<int, Endpoint*>::iterator it = leaves_.begin();
  while (it != leaves_.end()) {
    Endpoint* leaf = it->second;
    leaf->setBroken();
    it++;
  }
  leaves_.clear();
  trunk_->setBroken();
  trunk_ = NULL;
}

void
ClientCenter::handleData() {
  while (processFrame()) {
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
ClientCenter::processFrame() {
  if (frame_.state == STATE_NONE) {
    return true;
  }
  DEBUG("process frame, state:%d, id:%d, message.size:%zd\n", frame_.state, frame_.id, frame_.message.size());
  if (frame_.id == 0) {
    frame_.state = STATE_NONE;
    return true;
  }
  if (frame_.state == STATE_CONNECT) {
    DEBUG("process frame, state:CONNECT, id:%d, message:%s\n", frame_.id, frame_.message.c_str());
    map<int, Endpoint*>::iterator it = leaves_.find(frame_.id);
    if (it == leaves_.end()) { // must not exist
      char ip[30];
      int port;
      do {
        if (sscanf(frame_.message.c_str(), "%s:%d", ip, &port) != 2) {
          ERROR("invalid frame with state:CONNECT, message:%s\n", frame_.message.c_str());
          break;
        }
        Endpoint* leaf = Endpoint::create(frame_.id, ip, port);
        if (leaf == NULL) {
          ERROR("failed to create ip:%s, port:%d\n", ip, port);
          break;
        }
        leaves_[frame_.id] = leaf;
      } while (false);
    }
  } else if (frame_.state == STATE_DATA) {
    map<int, Endpoint*>::iterator it = leaves_.find(frame_.id);
    DEBUG("process frame, state:DATA, id:%d, message.size:%zd\n", frame_.id, frame_.message.size());
    if (it != leaves_.end()) {
      if (it->second->getWriteBufferRemainSize() > 0) {
        it->second->appendDataToWriteBuffer(frame_.message.data(), frame_.message.size());
      } else {
        DEBUG("process frame, but the buffer is full\n");
        return false; // NOTE
      }
    }
  } else if (frame_.state == STATE_CLOSE) {
    DEBUG("process frame, state:CLOSE, id:%d, message.size:%zd\n", frame_.id, frame_.message.size());
    map<int, Endpoint*>::iterator it = leaves_.find(frame_.id);
    if (it != leaves_.end()) {
      it->second->setWriterBufferEof();
      leaves_.erase(it);
    }
  }
  // else ignore
  frame_.state = STATE_NONE;
  return true;
}
