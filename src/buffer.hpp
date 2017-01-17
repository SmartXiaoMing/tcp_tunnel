//
// Created by mabaiming on 16-10-23.
//

#ifndef TCP_TUNNEL_BUFFER_H
#define TCP_TUNNEL_BUFFER_H

#include "common.h"
#include "frame.h"

#include <string>
#include <memory>

using namespace Common;
using namespace std;
class Stream {
public:
  static const int MaxSize = 4096;
  Stream() {
    eof = false;
    throughSize = 0;
  }
  int readableSize() {
    if (eof && buffer.empty()) {
      return -1;
    }
    return buffer.size();
  }
  int read(char* data, int len) {
    int n = readableSize();
    if (n == -1) {
      return -1;
    }
    if (len > 0 && n > 0) {
      if (len > n) {
        len = n;
      }
      memcpy(data, buffer.data(), len);
      return len;
    }
    return 0;
  }
  int read(string& result, int len) {
    int n = readableSize();
    if (n == -1) {
      return -1;
    }
    if (len > 0 && n > 0) {
      if (len > n) {
        len = n;
      }
      result.append(buffer.data(), len);
      return len;
    }
    return 0;
  }
  int readFrame(Frame& frame) {
    int n = readableSize();
    if (n == -1) {
      return -1;
    }
    return frame.decode(buffer.data(), n);
  }
  const char* getReadData() {
    return buffer.data();
  }
  int popRead(int len) {
    int n = readableSize();
    if (n == -1) {
      return -1;
    }
    if (len > 0 && n > 0) {
      if (len >= n) {
	      throughSize += n;
        buffer.clear();
        return n;
      } else {
        throughSize += len;
        buffer.assign(buffer.data() + len, buffer.size() - len);
        return len;
      }
    }
    return 0;
  }
  int writableSize() {
    if (eof) {
      return -1;
    }
    int n = MaxSize - buffer.size();
    return n > 0 ? n : 0;
  }
  int writableSizeForFrame() {
    int n = writableSize();
    if (n == -1) {
      return -1;
    }
    n -= Frame::HeadLength;
    return n < 0 ? 0 : n;
  }
  int write(const char* data, int len) {
    int n = writableSize();
    if (n == -1) {
      return -1;
    }
    if (len > 0 && n > 0) {
      if (len > n) {
        len = n;
      }
      buffer.append(data, len);
      return len;
    }
    return 0;
  }
  int writeAll(const char* data, int len) {
    int n = writableSize();
    if (n == -1) {
      return -1;
    }
    if (len > 0 && len <= n) {
      buffer.append(data, len);
      return len;
    }
    return 0;
  }
  void close() {
    eof = true;
  }
  bool eof;
  string buffer;
  int throughSize;
};

class Buffer {
public:
  Buffer(){}
  Buffer(int type_, int fd_) {
    index = 0;
    gId += 1;
    id = gId;
    type = type_;
    fd = fd_;
    ts = time(NULL);
    stream[0].reset(new Stream());
    stream[1].reset(new Stream());
  }
  Buffer reverse() {
    Buffer buffer = *this;
    buffer.index = 1 - index;
    return buffer;
  }
  int readableSize() {
    return stream[index]->readableSize();
  }
  int read(char* result, int len) {
    return stream[index]->read(result, len);
  }
  int read(string& result, int len) {
    return stream[index]->read(result, len);
  }
  int readFrame(Frame& frame) {
    return stream[index]->readFrame(frame);
  }
  const char* getReadData() {
    return stream[index]->getReadData();
  }
  int popRead(int len) {
    return stream[index]->popRead(len);
  }
  int writableSize() {
    return stream[1-index]->writableSize();
  }
  int writableSizeForFrame() {
    return stream[1-index]->writableSizeForFrame();
  }
  int write(const char* data, int len) {
    return stream[1-index]->writeAll(data, len);
  }
  int write(const string& data) {
    return stream[1-index]->writeAll(data.data(), data.size());
  }
  int writeFrame(int cid, char state, const string& data) {
    int n = writableSize();
    if (n == -1) {
      return -1;
    }
    if (n < data.size() + Frame::HeadLength) {
      return 0;
    }
    Frame frame;
    frame.cid = cid;
    frame.state = state;
    frame.message = data;
    string result;
    frame.encode(result);
    return write(result);
  }
  int writeFrame(int id, const string& data) {
    return writeFrame(id, Frame::STATE_TRAFFIC, data);
  }
  int writeFrame(int id, char state) {
    return writeFrame(id, state, "");
  }
  void close() {
    stream[1-index]->close();
  }
  bool isClosed() {
    return stream[index]->readableSize() == -1
           || stream[1-index]->writableSize() == -1;
  }
  int getInputSize() {
    return stream[index]->throughSize;
  }
  int getOutputSize() {
    return stream[1-index]->throughSize;
  }

  int getWriteBufferSize() {
    return stream[1-index]->buffer.size();
  }
  int getReadBufferSize() {
    return stream[index]->buffer.size();
  }
  int getId() {
    return id;
  }

  int getType() {
    return type;
  }

  void setName(const string& name_) {
    if (name_.size() < 32) {
      name = name_;
    } else {
      name.assign(name_.c_str(), 32);
    }
  }

  string getMac() {
    string mac;
    if (Common::getMac(mac, fd)) {
      return mac;
    }
    return "unknown";
  }

  string getName() {
    string result;
    result.append(FdToAddr(fd, false).toAddr().toString());
    if (!name.empty()) {
      result.append("(").append(name).append(")");
    }
    return result;
  }

  string toString() {
    string result;
    result.append(intToString(id)).append("\t");
    result.append(getName()).append("\t");
    if (isClosed()) {
      result.append("CLOSED\t");
    } else {
      result.append("OK\t");
    }
    result.append(formatTime(ts)).append("\t");
    result.append(intToString(getInputSize())).append("\t");
    result.append(intToString(getOutputSize()));
    return result;
  }

protected:
  static int gId;
  int id;
  int type;
  int fd;
  time_t ts;
  string name;
  int index;
  shared_ptr<Stream> stream[2];
};

int Buffer::gId = 0;

#endif //TCP_TUNNEL_BUFFER_H
