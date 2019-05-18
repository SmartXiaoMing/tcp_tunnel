//
// Created by mabaiming on 19-5-18.
//

#ifndef TCP_TUNNEL_CONSTR_H
#define TCP_TUNNEL_CONSTR_H

class Constr {
public:
  Constr(): data(NULL), size(0) {}
  Constr(const char* d, int s): data(d), size(s) {}
  Constr(const char* str) {
    reset(str);
  }
  void reset(const char* str) {
    data = str;
    size = strlen(str);
    reset(data, size);
  }
  void reset(const char* d, int s) {
    data = d;
    size = s;
  }
  void popFront(int s) {
    data += s;
    size -= s;
  }
  void popBack(int s) {
    size -= s;
  }
  void removeFront(const Constr& chars) {
    while (size > 0 && chars.indexOf(data[0]) >= 0) {
      ++data;
      --size;
    }
  }
  void removeBack(const Constr& chars) {
    while (size > 0 && chars.indexOf(data[size - 1]) >= 0) {
      --size;
    }
  }
  bool startsWith(const Constr& str) const {
    return size >= str.size && memcmp(data, str.data, str.size) == 0;
  }
  bool endsWith(const Constr& str) const {
    return size >= str.size && memcmp(data + size - str.size, str.data, str.size) == 0;
  }
  bool equals(const Constr& str) const  {
    return size == str.size && memcmp(data, str.data, str.size) == 0;
  }
  Constr subFrom(int begin) {
    return Constr(data + begin, size - begin);
  }
  Constr subBetween(int begin, int end) {
    return Constr(data + begin, end - begin);
  }
  int split(const Constr& str, Constr* result, int maxSize) const { // keep all tokens
    int resultSize = 0;
    Constr tmp(data, size);
    while (tmp.size > 0) {
      if (resultSize + 1 >= maxSize) {
        result[resultSize].reset(tmp.data, tmp.size);
        ++resultSize;
        return resultSize;
      }
      int pos = tmp.indexOf(str);
      if (pos < 0) {
        result[resultSize].reset(tmp.data, tmp.size);
        ++resultSize;
        return resultSize;
      } else {
        result[resultSize].reset(tmp.data, pos);
        tmp.popFront(pos + str.size);
        ++resultSize;
      }
    }
    return resultSize;
  }
  int readUtil(const Constr& flag, Constr& result) {
    const char* start = (const char* )memmem(data, size, flag.data, flag.size);
    if (start == NULL) {
      result.reset(NULL, 0);
      return 0;
    }
    int resultSize = start - data;
    result.reset(data, resultSize);
    return resultSize + flag.size;
  }
  int indexOf(const Constr& str) const  {
    if (str.size > size) {
      return -1;
    }
    const char* start = (const char* )memmem(data, size, str.data, str.size);
    if (start == NULL) {
      return -1;
    }
    return start - data;
  }
  int indexOf(char ch) const {
    const char* p = (const char*)memchr(data, ch, size);
    if (p == NULL) {
      return -1;
    }
    return p - data;
  }
  void trim() {
    while (size > 0 && *data == ' ') {
      ++data;
      --size;
    }
    while (size > 0 && data[size - 1] == ' ') {
      --size;
    }
  }
  const char* data;
  int size;
};
#endif //TCP_TUNNEL_CONSTR_H
