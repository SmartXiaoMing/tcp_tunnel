//
// Created by mabaiming on 18-8-14.
//

#ifndef TCP_TUNNEL_FRAME_HPP
#define TCP_TUNNEL_FRAME_HPP

#include <string.h>

#include <string>

#include "utils.h"

using namespace std;

const int MinFrameSize = 11;
const int FrameMaxDataSize = 4096;

enum FrameState {
  STATE_NONE = 0,
  STATE_TUNNEL_LOGIN = 1,
  STATE_TUNNEL_ERROR = 2,
  STATE_TRAFFIC_CONNECT = 3,
  STATE_TRAFFIC_DATA = 4,
  STATE_TRAFFIC_ACK = 5,
  STATE_TRAFFIC_CLOSE = 6,
  STATE_SIZE
};

class Frame {
public:
  /*
   * totalMinSize = 1 + 1 + 4 + 1 + 1 + 2 = 10
   *
   * state: 1 byte
   * owner: 1 byte
   * session: 4 byte
   * from: 1 byte + fromSize
   * to: 1 byte + toSize
   * message: 2 byte + messageSize   *
   */
  static const uint8_t OwnerMe = 0;
  static const uint8_t OwnerPeer = 1;
  uint8_t state;
  uint8_t owner;
  int session;
  string from;
  string to;
  string message;

  static int encodeTo(string& buffer, const Frame& frame) {
    return encodeTo(buffer, frame.state, frame.owner, frame.session, &frame.from, &frame.to, frame.message);
  }

  static int encodeTo(string& buffer, uint8_t state, uint8_t owner, uint32_t session,
                      const string* from, const string* to, const string& data) {
    return encodeTo(buffer, state, owner, session, from, to, data.c_str(), data.size());
  }

  static int encodeTo(string& buffer, uint8_t state, uint8_t owner, uint32_t session,
                      const string* from, const string* to, const char* data, int size) {
    do {
      buffer.push_back(state);
      buffer.push_back(owner);
      buffer.push_back((session >> 24) & 0xff);
      buffer.push_back((session >> 16) & 0xff);
      buffer.push_back((session >> 8) & 0xff);
      buffer.push_back(session & 0xff);

      if (from == NULL) {
        buffer.push_back(0);
      } else {
        buffer.push_back((uint8_t)from->size());
        buffer.append(from->begin(), from->end());
      }
      if (to == NULL) {
        buffer.push_back(0);
      } else {
        buffer.push_back((uint8_t)to->size());
        buffer.append(to->begin(), to->end());
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
    return buffer.size();
  }

  static int parse(Frame& frame, const char* buffer, int bufferSize) {
    if (bufferSize < 5) {
      return 0;
    }

    frame.state = buffer[0];
    frame.owner = buffer[1];
    int v1 = (buffer[2] & 0xff) << 24;
    int v2 = (buffer[3] & 0xff) << 16;
    int v3 = (buffer[4] & 0xff) << 8;
    int v4 = (buffer[5] & 0xff);
    frame.session = (v1 | v2 | v3 | v4);
    int fromSize = buffer[6];
    int offset = 7;
    if (fromSize > 0) {
      if (fromSize + offset > bufferSize) {
        return 0;
      }
      frame.from.assign(buffer + offset, buffer + offset + fromSize);
      offset += fromSize;
    }
    if (offset + 1 > bufferSize) {
      return 0;
    }
    int toSize = buffer[offset];
    offset++;
    if (toSize > 0) {
      if (toSize + offset > bufferSize) {
        return 0;
      }
      frame.to.assign(buffer + offset, buffer + offset + toSize);
      offset += toSize;
    }
    if (offset + 2 > bufferSize) {
      return 0;
    }
    int messageSize = bytesToInt(buffer + offset, 2);
    if (messageSize < 0 || messageSize > FrameMaxDataSize) {
      ERROR("invalid frame with size:%d", messageSize);
      return -1;
    }
    offset += 2;
    if (offset + messageSize > bufferSize) {
      return 0;
    }
    frame.message.assign(buffer + offset, buffer + offset + messageSize);
    return offset + messageSize;
  }

  void reset() { // TODO
    state = STATE_NONE;
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
        "NONE", "TUNNEL_LOGIN", "TUNNEL_ERROR", "TRAFFIC_CONNECT", "TRAFFIC_DATA", "TRAFFIC_ACK", "TRAFFIC_CLOSE"
    };
    if (0 <= state && state < STATE_SIZE) {
      return table[state];
    }
    return "UNKNOWN";
  }

  void setReply(uint8_t state, const string& message) {
    this->state = state;
    this->owner = 1 - this->owner;
    this->message = message;
    string from = this->from;
    this->from = this->to;
    this->to = from;
  }
};
#endif //TCP_TUNNEL_FRAME_HPP
