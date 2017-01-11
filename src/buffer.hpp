//
// Created by mabaiming on 16-10-23.
//

#ifndef TCP_TUNNEL_BUFFER_H
#define TCP_TUNNEL_BUFFER_H

#include "common.h"
#include "frame.h"

#include <string>

using namespace Common;
using namespace std;

class Buffer {
public:
  Buffer(int type_, int fd_) {
    gId += 1;
    id = gId;
    type = type_;
	  fd = fd_;
    isOK = true;
    isReadEOF = false;
    isWriteEOF = false;
	  outputSize = 0;
	  inputSize = 0;
	  ts = time(NULL);
  }
  ~Buffer() {
   }

  int getId() {
    return id;
  }

  int getType() {
    return type;
  }

	bool getOK() {
		return isOK;
	}

	bool getError() {
		return !isOK;
	}

	void setError() {
		isOK = false;
	}

	bool getReadEOF() {
		return isReadEOF;
	}

	void setReadEOF() {
		isReadEOF = true;
	}

	bool getWriteEOF() {
		return isWriteEOF;
	}

	void close() {
		isWriteEOF = true;
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
		if (!isOK) {
			result.append("ERROR\t");
		} else if (isReadEOF) {
			if (isWriteEOF) {
				result.append("EOF\t");
			} else {
				result.append("READ_EOF\t");
			}
		} else if (isWriteEOF) {
			result.append("WRITE_EOF\t");
		} else {
			result.append("OK\t");
		}
		result.append(formatTime(ts)).append("\t");
		result.append(intToString(inputSize)).append("+");
		result.append(intToString(readBuffer.size())).append("\t");
		result.append(intToString(outputSize)).append("+");
		result.append(intToString(writeBuffer.size()));
		return result;
	}

	// called by user below
  int readableSize() {
		if (isReadEOF && readBuffer.empty()) {
			return -1;
		}
    return readBuffer.size();
  }

  int read(string& result, int maxSize) {
    int rSize = readableSize();
    if (rSize == -1) {
      return -1;
    }
    if (rSize < maxSize) {
      maxSize = rSize;
    }
    if (maxSize > 0) {
      result.append(readBuffer.data(), maxSize);
    }
    return maxSize;
  }

	int readFrame(Frame& package) {
		int n = readBuffer.empty() ? 0 : package.decode(readBuffer);
		if (isReadEOF && n == 0) {
			return -1;
		}
		return n;
	}

  int popRead(int size) {
    if (size >= readBuffer.size()) {
      size = readBuffer.size();
      readBuffer.clear();
	    inputSize += size;
      return size;
    }
    if (size > 0) {
      readBuffer.assign(readBuffer.begin() + size, readBuffer.end());
	    inputSize += size;
    }
    return size;
  }

  int writableSize() {
	  if (isWriteEOF || isReadEOF || !isOK) {
		  return -1;
	  }
    int n = gMaxSize - writeBuffer.size();
	  return n > 0 ? n : 0;
  }

	int writableSizeForFrame() {
		if (isWriteEOF || isReadEOF || !isOK) {
			return -1;
		}
		int n = gMaxSize - writeBuffer.size() - Frame::HeadLength;
		if (n > BUFFER_SIZE) {
			n = BUFFER_SIZE;
		}
		return n > 0 ? n : 0;
	}

  int write(const string& data) {
    int wSize = writableSize();
    if (wSize == -1) {
      return -1;
    }
    if (wSize >= data.size()) {
      writeBuffer.append(data);
      return data.size();
    }
    return 0;
  }

  int writeFrame(int cid, char state, const string& data) {
    int left = writableSize();
    if (left == -1) {
      return -1;
    }
    if (left < Frame::HeadLength + data.size()) {
      return 0;
    }
    Frame frame;
	  frame.cid = cid;
	  frame.state = state;
    frame.message = data;
    string result;
	  frame.encode(result);
    writeBuffer.append(result);
    return result.size();
  }

  int writeFrame(int id, const string& data) {
    return writeFrame(id, Frame::STATE_TRAFFIC, data);
  }

  int writeFrame(int id, char state) {
    return writeFrame(id, state, "");
  }

  int getReadBufferSize() {
	  return readBuffer.size();
  }

  int readBufferLeft() {
    if (!isOK || isReadEOF) {
      return -1;
    }
    return gMaxSize - readBuffer.size();
  }

  int appendToReadBuffer(char* buf, int len) {
	  if (readBufferLeft() < len) {
		  return 0;
	  }
    readBuffer.append(buf, len);
	  return len;
  }

	int getWriteBufferSize() {
		return writeBuffer.size();
	}

  const char* getWriteBuffer() {
    return writeBuffer.data();
  }

  int popWrite(int size) {
    if (size >= writeBuffer.size()) {
      size = writeBuffer.size();
      writeBuffer.clear();
	    outputSize += size;
      return size;
    }
    if (size > 0) {
      writeBuffer.assign(writeBuffer.begin() + size, writeBuffer.end());
	    outputSize += size;
    }
    return size;
  }

protected:
  static int gMaxSize;
  static int gId;
  bool isOK;
  bool isReadEOF;
  bool isWriteEOF;
  int id;
  int type;
  string readBuffer;
  string writeBuffer;
	int fd;
	int outputSize;
	int inputSize;
	time_t ts;
	string name;
};

int Buffer::gId = 0;
int Buffer::gMaxSize = 4096;

#endif //TCP_TUNNEL_BUFFER_H
