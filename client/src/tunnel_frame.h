//
// Created by mabaiming on 16-8-29.
//

#ifndef TCP_TUNNEL_FRAME_H
#define TCP_TUNNEL_FRAME_H

#include "tunnel_buffer.h"
#include "tunnel_utils.h"

#include <stdint.h>

const uint8_t DefaultVersion = 1;
const int HeadLength = 10;
const int MaxContentLength = 64 * 1024;

enum FrameState {
  STATE_NONE = 0,
  STATE_OK = 1,
  STATE_LOGIN = 2,
  STATE_CONNECT = 3,
  STATE_CLOSE = 4,
  STATE_DATA = 5,
};

char* stateToStr(int state) {
  switch (state) {
    case STATE_NONE : return "NONE";
    case STATE_OK : return "OK";
    case STATE_CONNECT : return "CONNECT";
    case STATE_CLOSE : return "CLOSE";
    case STATE_DATA : return "DATA";
    case STATE_LOGIN : return "LOGIN";
  }
  return "UNKNOWN";
}

typedef struct {
  uint8_t state;
  uint8_t addr[6];
  Buffer* message;
} Frame;

Frame*
frameInit() {
  Frame* frame = (Frame *) calloc(1, sizeof(Frame));
  frame->state = STATE_NONE;
  memset(frame->addr, 0, sizeof(frame->addr));
  frame->message = bufferInit(4096);
  return frame;
}

void
frameRecycle(Frame* frame) {
  if (!frame) {
    return;
  }
  bufferRecycle(frame->message);
  free(frame);
}

int
framePackageSize(Frame* frame) {
  return HeadLength + frame->message->size;
}

int
frameEncodeAppend(uint8_t state, uint8_t addr[6], char* data, int size,
    Buffer* buffer) {
  // 1byte version
  // 1byte state
  // 6bytes addr
  // 4byte message length
  // message
  // package.length = HeadLength + message.length
  int total = HeadLength + size;
  char result[total];
  result[0] = DefaultVersion;
  result[1] = ((size >> 8) & 0xff);
  result[2] = ((size >> 0) & 0xff);
  result[3] = state;
  if (addr != NULL) {
    result[4] = addr[0];
    result[5] = addr[1];
    result[6] = addr[2];
    result[7] = addr[3];
    result[8] = addr[4];
    result[9] = addr[5];
  } else {
    result[4] = 0;
    result[5] = 0;
    result[6] = 0;
    result[7] = 0;
    result[8] = 0;
    result[9] = 0;
  }
  for (int i = 0; i < size; ++i) {
    result[i + HeadLength] = data[i];
  }
  return bufferAdd(buffer, result, total);
}

int
frameAppend(Frame* frame, Buffer* buffer) {
  return frameEncodeAppend(frame->state, frame->addr, frame->message->data,
    frame->message->size, buffer);
}

int
frameDecode(Frame* frame, char* result, int size) {
  if (size < HeadLength) {
    return 0;
  }
  if (result[0] != DefaultVersion) {
    WARN("invalid version: %d for %d\n", result[0], DefaultVersion);
    return -1;
  }
  int length = ((result[1] & 0xff) << 8) | (result[2] & 0xff);
  if (length < 0 || length > MaxContentLength) {
    WARN("invalid length: %d\n", length);
    return -1;
  }
  int packageLength = HeadLength + length;
  if (size < packageLength) {
    return 0;
  }
  frame->state = result[3];
  frame->addr[0] = result[4];
  frame->addr[1] = result[5];
  frame->addr[2] = result[6];
  frame->addr[3] = result[7];
  frame->addr[4] = result[8];
  frame->addr[5] = result[9];
  bufferReset(frame->message);
  bufferAdd(frame->message, result + HeadLength, length);
  return packageLength;
}
#endif // TCP_TUNNEL_FRAME_H
