//
// Created by mabaiming on 18-9-28.
//

#include "tunnel_branch.h"


#include "event_manager.h"
#include "event_handler.h"
#include "utils.h"
#include "frame.hpp"

#include "tunnel_trunk.h"

void
Branch::onError() {
  Trunk::sSingle->sendDataToTunnel(STATE_CLOSE, addr_, NULL, 0);
}

int
Branch::onRead(const char* data, int size) {
  int rsize = min(readableSize_, size);
  if (rsize > 0) {
    int rsize = min(readableSize_, size);
    Trunk::sSingle->sendDataToTunnel(STATE_DATA, addr_, data, rsize);
    readableSize_ -= rsize;
  }
  return rsize;
}

void
Branch::onWritten(bool fromFull, int writtenSize) {
  DEBUG("[%-21s] fromFull:%d, writtenSize %d\n", addrToStr(addr_), fromFull, writtenSize);
  writtenSize_ += writtenSize;
  if (writtenSize_ > halfBufferSize_) {
    char b4[4];
    Trunk::sSingle->sendDataToTunnel(STATE_DATA, addr_, intToBytes(writtenSize_, b4, 4), 4);
    writtenSize_ = 0;
  }
  if (fromFull) {
    Trunk::sSingle->handleReadData();
  }
}

void
Branch::updateAck(int size) {
  readableSize_ += size;
}