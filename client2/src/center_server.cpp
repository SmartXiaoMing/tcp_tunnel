//
// Created by mabaiming on 18-8-16.
//

#include "center_server.h"

int
ServerCenter::getRemainBufferSizeFor(Endpoint* endpoint) {
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
ServerCenter::appendDataToBufferFor(Endpoint* endpoint, const char* data, int size) {
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
ServerCenter::notifyWritableFor(Endpoint* endpoint) {
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
ServerCenter::notifyBrokenFor(Endpoint* endpoint) {
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
