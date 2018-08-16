//
// Created by mabaiming on 18-8-14.
//

#ifndef TCP_TUNNEL_FRAME_HPP
#define TCP_TUNNEL_FRAME_HPP

#include <string>

#include "utils.h"

using namespace std;

const uint8_t DefaultVersion = 1;
const int FrameHeadSize = 8;
const int FrameMaxDataSize = 4096;

enum FrameState {
  STATE_NONE = 0,
  STATE_OK = 1,
  STATE_LOGIN = 2,
  STATE_CONNECT = 3,
  STATE_CLOSE = 4,
  STATE_DATA = 5,
};

class Frame {
public:
  uint8_t version;
  uint8_t state;
  uint32_t id;
  string message;

  static int encodeTo(string& buffer, uint8_t state, uint32_t id, const char* data, int size) {
    do {
      buffer.push_back(DefaultVersion);
      buffer.push_back(state);
      buffer.push_back((id >> 24) & 0xff);
      buffer.push_back((id >> 16) & 0xff);
      buffer.push_back((id >> 8) & 0xff);
      buffer.push_back(id & 0xff);
      if (size <= FrameMaxDataSize) {
        buffer.push_back((size >> 8) & 0xff);
        buffer.push_back(size & 0xff);
        buffer.append(data, size);
        break;
      } else {
        buffer.push_back(FrameMaxDataSize & 0xff);
        buffer.push_back(FrameMaxDataSize & 0xff);
        buffer.append(data, FrameMaxDataSize);
        size -= FrameMaxDataSize;
      }
    } while (size > 0);
    return FrameHeadSize + size;
  }

  static int parse(Frame& frame, const string& buffer) {
    if (buffer.size() < FrameHeadSize) {
      return 0;
    }
    int size = ((buffer[6] & 0xff) << 8) + (buffer[7] & 0xff);
    if (size < 0 || size > FrameMaxDataSize) {
      ERROR("invalid frame with size:%d\n", size);
      return -1;
    }
    int frameSize =  size + FrameHeadSize;
    if (buffer.size() < frameSize) {
      return 0;
    }
    frame.version = buffer[0];
    frame.state = buffer[1];
    frame.id = ((buffer[2] & 0xff) << 24) + ((buffer[3] & 0xff) << 16) + ((buffer[4] & 0xff) << 8) + (buffer[5] & 0xff);
    frame.message.assign(buffer.begin() + FrameHeadSize, buffer.begin() + frameSize);
    return frameSize;
  }

  void reset() {
    state = STATE_NONE;
    id = 0;
  }

};

#endif //TCP_TUNNEL_FRAME_HPP
