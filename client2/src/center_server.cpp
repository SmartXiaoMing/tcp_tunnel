//
// Created by mabaiming on 18-8-16.
//

#include "center_server.h"

void
ServerCenter::prepare(int tunnelPort, int trafficPort) {
  // TODO
}

int
ServerCenter::getRemainBufferSizeFor(Endpoint* endpoint) {
  map<int, Endpoint*>* pool = &users_;
  int headSize = 0;
  if (endpoint->getType() == Endpoint::TYPE_TRAFFIC) {
    pool = &clients;
    headSize = FrameHeadSize;
  }
  map<int, Endpoint*>::iterator it = pool->find(endpoint->getId());
  if (it != pool->end()) {
    return it->second->getWriteBufferRemainSize() - headSize;
  }
  return 0;
}

void
ServerCenter::appendDataToBufferFor(Endpoint* endpoint, const char* data, int size) {
  if (endpoint->getType() == Endpoint::TYPE_TRAFFIC) {
    map<int, Endpoint*>::iterator it = clients_.find(endpoint->getId());
    if (it != clients_.end()) {
      string buffer;
      Frame::encodeTo(buffer, STATE_DATA, endpoint->getId(), data, size);
      it->second->appendDataToWriteBuffer(buffer.data(), buffer.size());
    }
  } else {
    map<int, Endpoint*>::iterator it = users_.find(endpoint->getId());
    if (it != users_.end()) {
      it->second->appendDataToWriteBuffer(data, size);
    }
  }
}

void
ServerCenter::notifyWritableFor(Endpoint* endpoint) {
  map<int, Endpoint*>* pool = &users_;
  if (endpoint->getType() == Endpoint::TYPE_TRAFFIC) {
    pool = &clients;
  }
  map<int, Endpoint*>::iterator it = pool->find(endpoint->getId());
  if (it != pool->end()) {
    it->second->notifyCenterIsWritable();
  }
}

void
ServerCenter::notifyBrokenFor(Endpoint* endpoint) {
  if (endpoint->getType() == Endpoint::TYPE_TRAFFIC) {
    map<int, Endpoint*>::iterator it = clients_.find(endpoint->getId());
    if (it != clients_.end()) {
      Frame::encodeTo(buffer, STATE_CLOSE, endpoint->getId(), NULL, 0);
      it->second->appendDataToWriteBuffer(buffer.data(), buffer.size());
    }
  } else {
    map<int, Endpoint*>::iterator it = users_.find(endpoint->getId());
    if (it != users_.end()) {
      it->second->setBroken();
    }
  }
}
