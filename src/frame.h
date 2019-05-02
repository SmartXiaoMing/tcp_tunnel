//
// Created by mabaiming on 18-8-14.
//

#ifndef TCP_TUNNEL_FRAME_HPP
#define TCP_TUNNEL_FRAME_HPP

#include <string.h>

#include <string>

#include "utils.h"

using namespace std;

const int FrameHeadSize = 13;
const int FrameMaxDataSize = 4096;

enum FrameState {
  STATE_NONE = 0,
  STATE_LOGIN = 1,
  STATE_CONNECT = 2,
  STATE_DATA = 3,
  STATE_ACK = 4,
  STATE_CLOSE = 5,
  STATE_RESET = 6,
  STATE_SIZE
};

class Frame {
public:
  uint8_t state;
  Addr addr; // TODO
  string message;

  static int encodeTo(string& buffer, uint8_t state, const Addr* addr, const char* data, int size) {
    do {
      buffer.push_back(state);
      if (addr == NULL) {
        buffer.push_back(0);
        buffer.push_back(0);
        buffer.push_back(0);
        buffer.push_back(0);
        buffer.push_back(0);
        buffer.push_back(0);
        buffer.push_back(0);
        buffer.push_back(0);
        buffer.push_back(0);
        buffer.push_back(0);
      } else {
        buffer.push_back(addr->b[0]);
        buffer.push_back(addr->b[1]);
        buffer.push_back(addr->b[2]);
        buffer.push_back(addr->b[3]);
        buffer.push_back(addr->b[4]);
        buffer.push_back(addr->b[5]);
        buffer.push_back((addr->tid >> 24) & 0xff);
        buffer.push_back((addr->tid >> 16) & 0xff);
        buffer.push_back((addr->tid >> 8) & 0xff);
        buffer.push_back(addr->tid & 0xff);
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
    int size = bytesToInt(buffer + FrameHeadSize - 2, 2);
    if (size < 0 || size > FrameMaxDataSize) {
      ERROR("invalid frame with size:%d\n", size);
      return -1;
    }
    int frameSize =  size + FrameHeadSize;
    if (bufferSize < frameSize) {
      return 0;
    }
    frame.state = buffer[0];
    frame.addr.b[0] = buffer[1];
    frame.addr.b[1] = buffer[2];
    frame.addr.b[2] = buffer[3];
    frame.addr.b[3] = buffer[4];
    frame.addr.b[4] = buffer[5];
    frame.addr.b[5] = buffer[6];
    frame.addr.tid = (((buffer[7] << 24) & 0xff000000)
                     | ((buffer[8] << 16) & 0xff0000)
                     | ((buffer[9] << 8) & 0xff00)
                     | (buffer[10]& 0xff));
    frame.message.assign(buffer + FrameHeadSize, buffer + frameSize);
    return frameSize;
  }

  void reset() {
    state = STATE_NONE;
    memset(&addr, 0, sizeof(addr));
  }

  const char* stateToStr() {
    static const char* table[] {
        "NONE", "LOGIN", "CONNECT", "DATA", "ACK", "CLOSE"
    };
    if (0 <= state && state < STATE_SIZE) {
      return table[state];
    }
    return "UNKNOWN";
  }

  static const char* stateToStr(int state) {
    static const char* table[] {
        "NONE", "LOGIN", "CONNECT", "DATA", "ACK", "CLOSE"
    };
    if (0 <= state && state < STATE_SIZE) {
      return table[state];
    }
    return "UNKNOWN";
  }
};

#endif //TCP_TUNNEL_FRAME_HPP
