//
// Created by mabaiming on 16-8-29.
//

#ifndef TCP_TUNNEL_TUNNEL_PACKAGE_H
#define TCP_TUNNEL_TUNNEL_PACKAGE_H

#include "logger.h"

#include <stdint.h>

#include <iostream>
#include <string>

using namespace std;

class TunnelPackage {
public:
  static const uint8_t DefaultVersion = 1;
  static const int HeadLength = 10;
  static const int MaxContentLength = 4096;

  static const uint8_t STATE_TRAFFIC = 0;
  static const uint8_t STATE_CREATE = 1;
  static const uint8_t STATE_CREATE_SUCCESS = 2;
  static const uint8_t STATE_CREATE_FAILURE = 3;
  static const uint8_t STATE_CLOSE = 4;
  static const uint8_t STATE_HEARTBEAT = 5;
  static const uint8_t STATE_VERIFY_REQUEST = 6;
  static const uint8_t STATE_VERIFY_RESPONSE = 7;

  uint32_t fd;
  uint8_t state;
  string message;

  const string& getState() {
    static string traffic = "traffic";
    static string create = "create";
    static string create_failure = "create_failure";
    static string close = "close";
    static string heartbeat = "heartbeat";
    static string verify_request = "verify_request";
    static string verify_response = "verify_response";

    static string unknown = "unknown";
    switch(state) {
      case STATE_TRAFFIC : return traffic;
      case STATE_CREATE : return create;
      case STATE_CREATE_FAILURE : return create_failure;
      case STATE_CLOSE : return close;
      case STATE_HEARTBEAT : return heartbeat;
      case STATE_VERIFY_REQUEST : return verify_request;
      case STATE_VERIFY_RESPONSE : return verify_response;
      default : return unknown;
    }
  }

  int encode(string& result) {
    return encode(result, *this);
  }

  int decode(string result) {
    decode(*this, result);
  }

  int decode(const char* result, int size) {
    decode(*this, result, size);
  }

  static int encode(string& result, const TunnelPackage& package) {
    return encode(result, package.fd, package.state, package.message);
  }

  static int encode(string& result, int32_t fd, uint8_t state, const string& message) {
    // 1byte version
    // 4bytes fd
    // 1byte state
    // 4byte message length
    // message
    // package.length = HeadLength + message.length
    int total = HeadLength + message.size();
    result.resize(total);
    result[0] = DefaultVersion;
    result[1] = ((fd >> 24) & 0xff);
    result[2] = ((fd >> 16) & 0xff);
    result[3] = ((fd >> 8) & 0xff);
    result[4] = ((fd >> 0) & 0xff);
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

  static int decode(TunnelPackage& tunnelPackage, const string& result) {
    return decode(tunnelPackage, result.c_str(), result.size());
  }

  static int decode(TunnelPackage& tunnelPackage, const char* result, int size) {
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
    tunnelPackage.fd = ((result[1] & 0xff)  << 24)
        | ((result[2] & 0xff)  << 16)
        | ((result[3] & 0xff)  << 8)
        | (result[4] & 0xff) ;
    tunnelPackage.state = result[5];
    tunnelPackage.message.assign(result + HeadLength, length);
    return packageLength;
  }
};

#endif // TCP_TUNNEL_TUNNEL_PACKAGE_H
