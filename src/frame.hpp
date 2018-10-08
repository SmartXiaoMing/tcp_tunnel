//
// Created by mabaiming on 18-8-14.
//

#ifndef TCP_TUNNEL_FRAME_HPP
#define TCP_TUNNEL_FRAME_HPP

#include <string.h>

#include <string>

#include "utils.h"

using namespace std;

const uint8_t DefaultVersion = 1;
const int FrameHeadSize = 10;
const int FrameMaxDataSize = 4096;

enum FrameState {
  STATE_NONE = 0,
  STATE_OK = 1,
  STATE_LOGIN = 2,
  STATE_CONNECT = 3,
  STATE_CLOSE = 4,
  STATE_DATA = 5,
  STATE_ACK = 6,
  STATE_SIZE,
};

class Frame {
public:
  uint8_t version;
  uint8_t state;
  uint8_t addr[6];
  string message;

  static int encodeTo(string& buffer, uint8_t state, const uint8_t* addr, const char* data, int size) {
    do {
      buffer.push_back(DefaultVersion);
      buffer.push_back(state);
      if (addr == NULL) {
        buffer.push_back(0);
        buffer.push_back(0);
        buffer.push_back(0);
        buffer.push_back(0);
        buffer.push_back(0);
        buffer.push_back(0);
      } else {
        buffer.push_back(addr[0]);
        buffer.push_back(addr[1]);
        buffer.push_back(addr[2]);
        buffer.push_back(addr[3]);
        buffer.push_back(addr[4]);
        buffer.push_back(addr[5]);
      }
      if (size <= FrameMaxDataSize) {
        char two[2];
        intToBytes(size, two, 2);
        buffer.push_back(two[0]);
        buffer.push_back(two[1]);
        buffer.append(data, size);
        break;
      } else {
        char two[2];
        intToBytes(FrameMaxDataSize, two, 2);
        buffer.push_back(two[0]);
        buffer.push_back(two[1]);
        buffer.append(data, FrameMaxDataSize);
        size -= FrameMaxDataSize;
      }
    } while (size > 0);
    return FrameHeadSize + size;
  }

  static int parse(Frame& frame, const char* buffer, int bufferSize) {
    if (bufferSize < FrameHeadSize) {
      return 0;
    }
    int size = bytesToInt(buffer + 8, 2);
    if (size < 0 || size > FrameMaxDataSize || size > bufferSize - FrameHeadSize) {
      ERROR("invalid frame with size:%d\n", size);
      return -1;
    }
    int frameSize =  size + FrameHeadSize;
    if (bufferSize < frameSize) {
      return 0;
    }
    frame.version = buffer[0];
    frame.state = buffer[1];
    frame.addr[0] = buffer[2];
    frame.addr[1] = buffer[3];
    frame.addr[2] = buffer[4];
    frame.addr[3] = buffer[5];
    frame.addr[4] = buffer[6];
    frame.addr[5] = buffer[7];
    frame.message.assign(buffer + FrameHeadSize, buffer + frameSize);
    return frameSize;
  }

  void reset() {
    state = STATE_NONE;
    memset(addr, 0, sizeof(addr));
  }

  const char* stateToStr() {
    static const char* table[] {
        "NONE", "OK", "LOGIN", "CONNECT", "CLOSE", "DATA"
    };
    if (0 <= state && state < STATE_SIZE) {
      return table[state];
    }
    return "UNKNOWN";
  }

};

#endif //TCP_TUNNEL_FRAME_HPP
