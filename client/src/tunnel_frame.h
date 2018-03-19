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
  STATE_CONNECT = 2,
  STATE_CLOSE = 3,
  STATE_DATA = 4,
  STATE_LOGIN = 5
};

typedef struct {
  uint32_t cid;
  uint8_t state;
  Buffer* message;
} Frame;

Frame*
frameInit() {
  Frame* frame = (Frame *) calloc(1, sizeof(Frame));
  frame->cid = 0;
  frame->state = STATE_NONE;
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
frameEncodeAppend(int32_t cid, uint8_t state, char* data, int size, Buffer* buffer) {
  // 1byte version
  // 4bytes cid
  // 1byte state
  // 4byte message length
  // message
  // package.length = HeadLength + message.length
  int total = HeadLength + size;
  char result[total];
  result[0] = DefaultVersion;
  result[1] = ((cid >> 24) & 0xff);
  result[2] = ((cid >> 16) & 0xff);
  result[3] = ((cid >> 8) & 0xff);
  result[4] = ((cid >> 0) & 0xff);
  result[5] = state;
  result[6] = ((size >> 24) & 0xff);
  result[7] = ((size >> 16) & 0xff);
  result[8] = ((size >> 8) & 0xff);
  result[9] = ((size >> 0) & 0xff);
  for (int i = 0; i < size; ++i) {
    result[i + HeadLength] = data[i];
  }
  return bufferAdd(buffer, result, total);
}

int
frameAppend(Frame* frame, Buffer* buffer) {
  return frameEncodeAppend(frame->cid, frame->state, frame->message->data,
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
  int length = ((result[6] & 0xff) << 24)
               | ((result[7] & 0xff) << 16)
               | ((result[8] & 0xff) << 8)
               | (result[9] & 0xff);
  if (length < 0 || length > MaxContentLength) {
    WARN("invalid length: %d\n", length);
    return -1;
  }
  int packageLength = HeadLength + length;
  if (size < packageLength) {
    return 0;
  }
  frame->cid = ((result[1] & 0xff)  << 24)
               | ((result[2] & 0xff)  << 16)
               | ((result[3] & 0xff)  << 8)
               | (result[4] & 0xff) ;
  frame->state = result[5];
  bufferReset(frame->message);
  bufferAdd(frame->message, result + HeadLength, length);
  return packageLength;
}
#endif // TCP_TUNNEL_FRAME_H
