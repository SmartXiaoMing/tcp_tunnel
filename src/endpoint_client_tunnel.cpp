//
// Created by mabaiming on 19-3-28.
//

#include "endpoint_client_tunnel.h"

#include "frame.h"

void
EndpointClientTunnel::sendData(uint8_t state, const Addr* addr, const char* data, int size) {
  string buffer;
  Frame::encodeTo(buffer, state, addr, data, size);
  writeData(buffer.data(), buffer.size());
}

int
EndpointClientTunnel::parseFrame(Frame& frame) {
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