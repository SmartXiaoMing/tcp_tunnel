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
  STATE_HEARTBEAT = 0,
  STATE_TRAFFIC = 1,
  STATE_CREATE = 2,
  STATE_CREATE_SUCCESS = 3,
  STATE_CREATE_FAILURE = 4,
  STATE_CLOSE = 5,
  STATE_CHALLENGE_REQUEST = 6,
  STATE_CHALLENGE_RESPONSE = 7,
  STATE_MONITOR_REQUEST = 8,
  STATE_MONITOR_RESPONSE = 9,
  STATE_SET_NAME = 10,
  STATE_CONTROL_REQUEST = 11,
  STATE_CONTROL_RESPONSE = 12
};
typedef struct {
  uint32_t cid;
  uint8_t state;
  Buffer message;
} Frame;

int
framePackageSize(Frame* frame) {
  return HeadLength + frame->message.size;
}

const char*
frameState(Frame* frame) {
  static char* traffic = "traffic";
  static char* create = "create";
  static char* create_failure = "create_failure";
  static char* close = "close";
  static char* heartbeat = "heartbeat";
  static char* challenge_request = "challenge_request";
  static char* challenge_response = "challenge_response";
  static char* monitor_request = "monitor_request";
  static char* monitor_response = "monitor_response";
  static char* monitor_set_name = "set_name";
  static char* unknown = "unknown";
  switch(frame->state) {
    case STATE_HEARTBEAT : return heartbeat;
    case STATE_TRAFFIC : return traffic;
    case STATE_CREATE : return create;
    case STATE_CREATE_FAILURE : return create_failure;
    case STATE_CLOSE : return close;
    case STATE_CHALLENGE_REQUEST : return challenge_request;
    case STATE_CHALLENGE_RESPONSE : return challenge_response;
    case STATE_MONITOR_REQUEST : return monitor_request;
    case STATE_MONITOR_RESPONSE : return monitor_response;
    case STATE_SET_NAME : return monitor_set_name;
    default : return unknown;
  }
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
  return frameEncodeAppend(frame->cid, frame->state, frame->message.data,
    frame->message.size, buffer);
}

int
frameDecode(Frame* frame,   char* result, int size) {
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
  bufferTempFromStr(&frame->message, result + HeadLength, length);
  return packageLength;
}
#endif // TCP_TUNNEL_FRAME_H
