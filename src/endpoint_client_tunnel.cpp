//
// Created by mabaiming on 19-3-28.
//

#include "endpoint_client_tunnel.h"

#include "frame.h"

void
EndpointClientTunnel::sendData(const Frame& frame) {
  string buffer;
  Frame::encodeTo(buffer, frame);
  writeData(buffer.data(), buffer.size());
}

int
EndpointClientTunnel::parseFrame(Frame& frame) {
  if (bufferRead.size() == 0) {
    return 0;
  }
  int size = Frame::parse(frame, bufferRead.data(), bufferRead.size());
  if (size > 0) {
    addReadableSize(size);
    popReadData(size);
  }
  if (size < 0) {
    discard();
    callback_(this, EVENT_ERROR, NULL, 0);
  }
  return size;
}
