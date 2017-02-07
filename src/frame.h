//
// Created by mabaiming on 16-8-29.
//

#ifndef TCP_TUNNEL_FRAME_H
#define TCP_TUNNEL_FRAME_H

#include "logger.h"

#include <stdint.h>

#include <string>

using namespace std;

class Frame {
public:
  static const uint8_t DefaultVersion = 1;
  static const int HeadLength = 10;
  static const int MaxContentLength = 64 * 1024;

  static const uint8_t STATE_HEARTBEAT = 0;
  static const uint8_t STATE_TRAFFIC = 1;
  static const uint8_t STATE_CREATE = 2;
  static const uint8_t STATE_CREATE_SUCCESS = 3;
  static const uint8_t STATE_CREATE_FAILURE = 4;
  static const uint8_t STATE_CLOSE = 5;
  static const uint8_t STATE_CHALLENGE_REQUEST = 6;
  static const uint8_t STATE_CHALLENGE_RESPONSE = 7;
  static const uint8_t STATE_MONITOR_REQUEST = 8;
  static const uint8_t STATE_MONITOR_RESPONSE = 9;
  static const uint8_t STATE_SET_NAME = 10;

  uint32_t cid;
  uint8_t state;
  string message;

  const string& getState() {
    static string traffic = "traffic";
    static string create = "create";
    static string create_failure = "create_failure";
    static string close = "close";
    static string heartbeat = "heartbeat";
    static string challenge_request = "challenge_request";
    static string challenge_response = "challenge_response";
    static string monitor_request = "monitor_request";
    static string monitor_response = "monitor_response";
    static string monitor_set_name = "set_name";
    static string unknown = "unknown";
    switch(state) {
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

  int encode(string& result) const {
    return encode(result, *this);
  }

  int decode(string result) {
    decode(*this, result);
  }

  int decode(const char* result, int size) {
    decode(*this, result, size);
  }

  static int encode(string& result, const Frame& package) {
    return encode(result, package.cid, package.state, package.message);
  }

  static int encode(string& result, int32_t cid, uint8_t state, const string& message) {
    // 1byte version
    // 4bytes cid
    // 1byte state
    // 4byte message length
    // message
    // package.length = HeadLength + message.length
    int total = HeadLength + message.size();
    result.resize(total);
    result[0] = DefaultVersion;
    result[1] = ((cid >> 24) & 0xff);
    result[2] = ((cid >> 16) & 0xff);
    result[3] = ((cid >> 8) & 0xff);
    result[4] = ((cid >> 0) & 0xff);
    result[5] = state;
    int length = message.size();
    result[6] = ((length >> 24) & 0xff);
    result[7] = ((length >> 16) & 0xff);
    result[8] = ((length >> 8) & 0xff);
    result[9] = ((length >> 0) & 0xff);
    for (int i = 0; i < length; ++i) {
      result[i + HeadLength] = message[i];
    }
    return total;
  }

  static int decode(Frame& frame, const string& result) {
    return decode(frame, result.c_str(), result.size());
  }

  static int decode(Frame& frame, const char* result, int size) {
    if (size < HeadLength) {
      return 0;
    }
    if (result[0] != DefaultVersion) {
      log_error << "invalid version: " << (int)DefaultVersion << endl;
      return -1;
    }
    int length = ((result[6] & 0xff) << 24)
        | ((result[7] & 0xff) << 16)
        | ((result[8] & 0xff) << 8)
        | (result[9] & 0xff);
    if (length < 0 || length > MaxContentLength) {
      log_error << "invalid length: " << length;
      return -1;
    }
    int packageLength = HeadLength + length;
    if (size < packageLength) {
      return 0;
    }
    frame.cid = ((result[1] & 0xff)  << 24)
        | ((result[2] & 0xff)  << 16)
        | ((result[3] & 0xff)  << 8)
        | (result[4] & 0xff) ;
    frame.state = result[5];
    frame.message.assign(result + HeadLength, length);
    return packageLength;
  }
};

#endif // TCP_TUNNEL_FRAME_H
