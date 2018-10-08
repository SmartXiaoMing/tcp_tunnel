//
// Created by mabaiming on 18-9-28.
//
#include <unistd.h>

#include "tunnel_trunk.h"
#include "event_handler.h"
#include "utils.h"
#include "frame.hpp"
#include <map>

using namespace std;

Trunk* Trunk::sSingle = NULL;

void
Trunk::prepare(const char* host, int port, const char* group, const char* name) {
  while (sSingle == NULL) {
    char ip[30];
    if (selectIp(host, ip, 29)) {
      INFO("[trunk] success to select ip:%s for host:%s\n", ip, host);
      int fd = create(ip, port);
      if (fd < 0) {
        WARN("[trunk] failed to connect %s:%d\n", ip, port);
      } else {
        INFO("[trunk] success to connect %s:%d\n", ip, port);
        sSingle = new Trunk(fd);
      }
    } else {
      ERROR("[trunk] failed to select ip:%s for host:%s\n", ip, host);
    }
    if (sSingle == NULL) {
      ERROR("[trunk] failed to prepare and wait 30 seoncds ...\n");
      sleep(30);
    } else {
      char data[256];
      int size = sprintf(data, "group=%s&name=%s&buffer=%d", group, name, 40960); // TODO
      sSingle->sendDataToTunnel(STATE_LOGIN, NULL, data, size);
    }
  }
}

void
Trunk::sendDataToTunnel(uint8_t state, const uint8_t* addr, const char* data, int size) {
  if (broken_) {
    return;
  }
  string buffer;
  Frame::encodeTo(buffer, state, addr, data, size);
  addDataToWrite(buffer.data(), buffer.size());
}

bool
Trunk::processFrame() {
  if (frame_.state == STATE_NONE) {
    return true;
  }
  DEBUG("[trunk] process frame, state:%s, addr:%s, message[%zd]:%-*s\n",
        frame_.stateToStr(), addrToStr(frame_.addr), frame_.message.size(),
        min((int)frame_.message.size(), 21), frame_.message.c_str());
  if (frame_.state == STATE_CONNECT) {;
    map<const uint8_t*, Branch*>::iterator it = branches_.find(frame_.addr);
    if (it == branches_.end()) { // must not exist
      do {
        int colon = frame_.message.find(':');
        if (colon < 0) {
          ERROR("[trunk] invalid frame, state:%s, addr:%s, wrong ip port:%s\n",
                frame_.stateToStr(), addrToStr(frame_.addr), frame_.message.c_str());
          break;
        }
        frame_.message[colon] = '\0';
        const char* ip = frame_.message.data();
        int port = 80;
        if (sscanf(frame_.message.data() + colon + 1, "%d", &port) != 1) {
          ERROR("[trunk] invalid frame, state:%s, addr:%s, wrong ip port:%s\n",
                frame_.stateToStr(), addrToStr(frame_.addr), frame_.message.c_str());
          break;
        }
        int fd = create(ip, port);
        if (fd < 0) {
          break;
        }
        branches_[frame_.addr] = new Branch(fd, frame_.addr);
      } while (false);
    } else {
      ERROR("[%-21s] addr exists\n", addrToStr(frame_.addr));
    }
  } else if (frame_.state == STATE_DATA) {
    map<const uint8_t*, Branch*>::iterator it = branches_.find(frame_.addr);
    if (it != branches_.end()) {
      if (it->second->addDataToWrite(frame_.message.data(), frame_.message.size()) == 0) {
        ERROR("[%-21s] the buffer is full\n", addrToStr(frame_.addr));
        return false; // NOTE
      }
    } else {
      ERROR("[%-21s] addr does not exists\n", addrToStr(frame_.addr));
    }
  } else if (frame_.state == STATE_ACK) {
    map<const uint8_t*, Branch*>::iterator it = branches_.find(frame_.addr);
    if (it != branches_.end()) {
      if (frame_.message.size() < 4) {
        ERROR("[trunk] invalid frame, state:%s, addr:%s, message.size:%zd\n",
              frame_.stateToStr(), addrToStr(frame_.addr), frame_.message.size());
      } else {
        uint32_t ack = bytesToInt(frame_.message.data(), 4);
        it->second->updateAck(ack);
      }
    } else {
      ERROR("[%-21s] addr does not exists\n", addrToStr(frame_.addr));
    }
  } else if (frame_.state == STATE_CLOSE) {
    map<const uint8_t*, Branch*>::iterator it = branches_.find(frame_.addr);
    if (it != branches_.end()) {
      it->second->addDataToWrite(NULL, 0);
      branches_.erase(it);
    } else {
      ERROR("[%-21s] addr does not exists\n", addrToStr(frame_.addr));
    }
  }
  // else ignore
  frame_.state = STATE_NONE;
  return true;
}

void
Trunk::onError() {
  DEBUG("[trunk] error\n");
  map<const uint8_t*, Branch*>::iterator it = branches_.begin();
  while (it != branches_.end()) {
    Branch* branch = it->second;
    branch->setBroken();
    it++;
  }
  branches_.clear();
  sSingle = NULL;
}

int
Trunk::onRead(const char* data, int size) {
  DEBUG("[trunk] onRead, size: %d, %.*s\n", size, min(32, size), data);
  int processSize = 0;
  while (processFrame()) {
    frame_.reset();
    int parseSize = Frame::parse(frame_, data, size);
    if (parseSize == 0) {
      return 0;
    }
    if (parseSize < 0) {
      setBroken();
      return 0;
    }
    processSize += parseSize;
    data += parseSize;
    size -= parseSize;
  }
  return processSize;
}

void
Trunk::onWritten(bool fromFull, int writtenSize) {
  DEBUG("[trunk] onWritten, fromFull:%d, writtenSize: %d\n", fromFull, writtenSize);
  if (fromFull) {
    map<const uint8_t*, Branch*>::iterator it = branches_.begin();
    while (it != branches_.end()) {
      Branch* branch = it->second;
      if (branch->handleReadData() < 0) {
        break;
      }
      it++;
    }
  }
}
