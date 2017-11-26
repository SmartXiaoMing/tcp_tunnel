//
// Created by mabaiming on 17-7-21.
//
#ifndef TUNNEL_BUFFER_H
#define TUNNEL_BUFFER_H

#include <malloc.h>
#include <string.h>
#include <stdbool.h>
#include <sys/syslog.h>

extern void *memmem (const void *__haystack, size_t __haystacklen,
                     const void *__needle, size_t __needlelen);

typedef struct {
  int rawSize; // 0 means borrow from other
  int size;
  char* data;
  char* raw;
  char** ref;
} Buffer;

Buffer*
bufferInit(int cap) {
  if (cap == 0) {
    return NULL;
  }
  Buffer* buffer = (Buffer *) calloc(1, sizeof(Buffer));
  if (buffer == NULL) {
    return NULL;
  }
  buffer->raw = (char* ) calloc(1, cap);
  if (buffer->raw == NULL) {
    free(buffer);
    return NULL;
  }
  buffer->rawSize = cap;
  buffer->data = buffer->raw;
  buffer->size = 0;
  buffer->ref = &buffer->raw;
  return buffer;
}

Buffer*
bufferCopy(const char* str) {
  int size = strlen(str);
  Buffer* buffer = bufferInit(size + 1);
  strcpy(buffer->data, str);
  buffer->size = size;
  return buffer;
}

void
bufferRecycle(Buffer* buffer) {
  if (buffer && buffer->rawSize) {
    free(buffer->raw);
    free(buffer);
  }
}

void
bufferReset(Buffer* buffer) {
  if (buffer) {
    buffer->data = buffer->raw;
    buffer->size = 0;
  }
}

void
bufferShadowReset(Buffer* buffer) {
  if (buffer) {
    buffer->rawSize = 0;
    buffer->raw = NULL;
    buffer->data = NULL;
    buffer->size = 0;
    buffer->ref = NULL;
  }
}

void
bufferConst(Buffer* buffer, char* str) {
  bufferShadowReset(buffer);
  buffer->data = str;
  buffer->size = strlen(str);
}

int
bufferAdd(Buffer* buffer, const char* data, int size) {
  if (buffer->rawSize == 0) {
    printf("failed to add to readonly buffer: %d\n", size);
    return 0;
  }
  if (size == 0 || data == NULL) {
    return 0;
  }
  int remainSize = buffer->rawSize - buffer->size;
  int dataOffset = buffer->data - buffer->raw;
  if (remainSize < size) {
    int newSize = buffer->size + size;
    char* dataNew = (char*) realloc(buffer->raw, newSize);
    if (dataNew == NULL) {
      return 0;
    }
    buffer->rawSize = newSize;
    buffer->raw = dataNew;
    buffer->ref = &buffer->raw;
    if (dataOffset > 0) {
      memmove(buffer->raw, buffer->raw + dataOffset, buffer->size);
    }
    buffer->data = buffer->raw;
  } else if (remainSize - dataOffset < size) {
    memmove(buffer->raw, buffer->data, buffer->size);
    buffer->data = buffer->raw;
  }
  memmove(buffer->data + buffer->size, data, size);
  buffer->size += size;
  return size;
}

int
bufferPopFront(Buffer* buffer, int size) {
  if (buffer->size < size) {
    printf("faild to bufferPopFront:%d, max:%d\n", size, buffer->size);
    return 0;
  }
  buffer->size -= size;
  buffer->data += size;
  return size;
}

int
bufferPopBack(Buffer* buffer, int size) {
  if (buffer->size < size) {
    printf("faild to bufferPopBack:%d, max:%d\n", size, buffer->size);
    return 0;
  }
  buffer->size -= size;
  return size;
}

int
bufferIndexOf(const Buffer* buffer, const Buffer* sub) {
  if (buffer->size < sub->size) {
    return -1;
  }
  char* start = (char*)memmem(buffer->data, buffer->size, sub->data, sub->size);
  if (start == NULL) {
    return -1;
  }
  return start - buffer->data;
}

int
bufferIgnoreCaseIndexOf(const Buffer* buffer, const Buffer* sub) {
  if (buffer->size < sub->size) {
    return -1;
  }
  char str[sub->size];
  for (int i = 0; i < sub->size; ++i) {
    if (sub->data[i] >= 'A' && sub->data[i] <= 'Z') {
      str[i] = sub->data[i] - 'A' + 'a';
    }
  }
  memcpy(str, sub->data, sub->size);
  for (int i = 0; i <= buffer->size - sub->size; ++i) {
    bool eq = true;
    for (int j = 0; j < sub->size; ++j) {
      char ch = buffer->data[i + j];
      if (ch >= 'A' && ch <= 'Z') {
        ch = ch - 'A' + 'a';
        if (ch != str[j]) {
          eq = false;
          break;
        }
      } else if (ch != str[j]) {
        eq = false;
        break;
      }
    }
    if (eq) {
      return i;
    }
  }
  return -1;
}

int
bufferToInt(const Buffer* buffer) {
  int value = 0;
  for (int i = 0; i < buffer->size; ++i) {
    if (buffer->data[i] < '0' || buffer->data[i] > '9') {
      break;
    }
    value *= 10;
    value += buffer->data[i] - '0';
  }
  return value;
}

int
bufferTempFrom(Buffer* buffer, const Buffer* from, int size) {
  buffer->ref = from->ref;
  buffer->raw = from->raw;
  buffer->rawSize = 0;
  buffer->data = from->data;
  if (size < from->size) {
    buffer->size = size;
  } else {
    buffer->size = from->size;
  }
  return buffer->size;
}

int
bufferTempFromStr(Buffer* buffer, char* from, int size) {
  buffer->ref = 0;
  buffer->raw = 0;
  buffer->rawSize = 0;
  buffer->data = from;
  buffer->size = size;
  return buffer->size;
}

Buffer*
bufferCloneFrom(int cap, const Buffer* from) {
  if (cap == 0) {
    return NULL;
  }
  Buffer* buffer = bufferInit(cap);
  if (cap < from->size) {
    bufferAdd(buffer, from->data, cap);
  } else {
    bufferAdd(buffer, from->data, from->size);
  }
  return buffer;
}

void
bufferTrim(Buffer* buffer) {
  while (buffer->size > 0
    && (*buffer->data == ' '
    || *buffer->data == '\t'
    || *buffer->data == '\r'
    || *buffer->data == '\n')) {
    ++buffer->data;
    --buffer->size;
  }
  while (buffer->size > 0
    && (buffer->data[buffer->size - 1] == ' '
    || buffer->data[buffer->size - 1] == '\t'
    || buffer->data[buffer->size - 1] == '\r'
    || buffer->data[buffer->size - 1] == '\n')) {
    --buffer->size;
  }
}

bool
bufferEquals(Buffer* a, Buffer* b) {
  if (a->size != b->size) {
    return false;
  }
  return memcmp(a->data, b->data, a->size) == 0;
}

bool
bufferIgnoreCaseEquals(Buffer* a, Buffer* b) {
  if (a->size != b->size) {
    return false;
  }
  for (int i = 0; i < a->size; ++i) {
    char c1 = a->data[i];
    if (c1 >= 'A' && c1 <= 'Z') {
      c1 = c1 - 'A' + 'a';
    }
    char c2 = b->data[i];
    if (c2 >= 'A' && c2 <= 'Z') {
      c2 = c2 - 'A' + 'a';
    }
    if (c1 != c2) {
      return false;
    }
  }
  return true;
}

bool
bufferStartWith(Buffer* a, Buffer* b) {
  if (a->size < b->size) {
    return false;
  }
  return memcmp(a->data, b->data, b->size) == 0;
}

void
bufferUnescape(Buffer* a) {
  // \t -> 0x9
  // \n -> 0xA
  // \r -> 0xD
  // \s -> 0x20
  // \\	-> 0x5C
  int end = 0;
  for (int i = 0; i < a->size; ++end) {
    if (a->data[i] == '\\' && i + 1 < a->size) {
      switch (a->data[i + 1]) {
        case 'n': a->data[end] = 0xA; i += 2; continue;
        case 'r': a->data[end] = 0xD; i += 2; continue;
        case 't': a->data[end] = 0x9; i += 2; continue;
        case 's': a->data[end] = 0x20; i += 2; continue;
        case '\\': a->data[end] = 0x5C; i += 2; continue;
      }
    }
    a->data[end] = a->data[i];
    ++i;
  }
  a->size = end;
}

void
bufferLowercase(Buffer* a) {
  for (int i = 0; i < a->size; ++i) {
    if ('A' <= a->data[i] && a->data[i] <= 'Z') {
      a->data[i] += 'a' - 'A';
    }
  }
}

bool
bufferToKv(Buffer* content, char ch, Buffer* key, Buffer* value) {
  char* p = memchr(content->data, ch, content->size);
  if (p == NULL) {
    return false;
  }
  *key = *content;
  key->rawSize = 0;
  key->size = p - content->data;
  *value = *content;
  value->rawSize = 0;
  value->data = p + 1;
  value->size = content->data + content->size - value->data;
  bufferTrim(key);
  bufferTrim(value);
  return true;
}

Buffer*
bufferRefresh(Buffer* buffer) {
  if (*buffer->ref != buffer->raw) {
    buffer->data = buffer->data - buffer->raw + *buffer->ref;
    buffer->raw = *buffer->ref;
  }
  return buffer;
}

bool
bufferReadFrom(Buffer* buffer, Buffer* file, char ch) {
  if (file->size == 0) {
    return false;
  }
  *buffer = *file;
  buffer->rawSize = 0;
  char* p = memchr(file->data, ch, file->size);
  if (p != NULL) {
    buffer->size = p - file->data;
    bufferPopFront(file, buffer->size + 1);
  } else {
    bufferPopFront(file, file->size);
  }
  return true;
}

int
bufferHexToInt(Buffer* buffer) {
  int value = 0;
  for (int i = 0; i < buffer->size; ++i) {
     char ch = buffer->data[i];
     if (ch >= '0' && ch <= '9') {
       value = value * 16 + ch - '0';
     } else if (ch >= 'a' && ch <= 'f') {
       value = value * 16 + 10 + ch - 'a';
     } else if (ch >= 'A' && ch <= 'F') {
       value = value * 16 + 10 + ch - 'A';
     } else {
       break;
     }
  }
  return value;
}

#endif //TUNNEL_BUFFER_H
