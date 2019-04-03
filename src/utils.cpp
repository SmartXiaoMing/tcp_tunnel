//
// Created by mabaiming on 17-10-15.
//

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <time.h>

#include "utils.h"

int logLevel = 0;

bool
isGoodCode() {
  int code = errno;
  return code == EAGAIN || code == EWOULDBLOCK || code == EINTR;
}

bool
isIpV4(const char *ip) {
  if (ip == NULL) {
    return false;
  }
  int dot = 0;
  while (*ip) {
    if (*ip == '.') {
      dot++;
    } else if (*ip < '0' || *ip > '9') {
      return false;
    }
    ++ip;
  }
  return dot == 3;
}


const char *
selectIp(const char *host, char ipBuffer[], int size) {
  if (isIpV4(host)) {
    strcpy(ipBuffer, host);
    return ipBuffer;
  }
  struct hostent *info = gethostbyname(host);
  if (info == NULL || info->h_addrtype != AF_INET) {
    WARN("invalid host: %s\n", host);
    return NULL;
  }
  int ipCount = 0;
  for (char **ptr = info->h_addr_list; *ptr != NULL; ++ptr) {
    ipCount++;
  }
  int index = 0;
  if (ipCount > 0) {
    srand(time(0));
    index = rand() % ipCount;
  }
  return inet_ntop(AF_INET, *(info->h_addr_list + index), ipBuffer, size);
}

int
createClient(const char *ip, int port) {
  int fd = socket(PF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return fd;
  }
  struct sockaddr_in saddr;
  memset(&saddr, 0, sizeof(saddr));
  saddr.sin_family = AF_INET;
  saddr.sin_addr.s_addr = inet_addr(ip);
  saddr.sin_port = htons(port);
  if (connect(fd, (struct sockaddr *) &saddr, sizeof(struct sockaddr)) < 0) {
    return -1;
  }
  return fd;
}

int
createServer(const char *ip, int port, int connectionCount) {
  int fd = socket(PF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return fd;
  }
  struct sockaddr_in saddr;
  memset(&saddr, 0, sizeof(saddr));
  saddr.sin_family = AF_INET;
  saddr.sin_addr.s_addr = inet_addr(ip);
  saddr.sin_port = htons(port);
  if (bind(fd, (struct sockaddr *) &saddr, sizeof(saddr)) < 0) {
    return -1;
  }
  if (listen(fd, connectionCount) < 0) {
    return -1;
  }
  return fd;
}

const char *
addrToStr(const uint8_t *b) {
  static uint8_t last[6] = {255, 0, 0, 0, 0, 0};
  static char str[40];
  if (b == NULL) {
    return "0.0.0.0:0";
  }
  if (memcmp((void *) last, (void *) b, sizeof(last)) == 0) {
    return str;
  }
  sprintf(str, "%u.%u.%u.%u:%d", b[0], b[1], b[2], b[3], (int) ((b[4] << 8) | (b[5] & 0xff)));
  memcpy((void *) last, (void *) b, sizeof(last));
  return str;
}

const char *
eventToStr(int event) {
  static int lastEvent = 0;
  static char str[64] = {'|'};
  if (event != lastEvent) {
    char *p = str + 1;
    if (event & EPOLLIN) {
      memcpy(p, "IN|", 3);
      p += 3;
    }
    if (event & EPOLLPRI) {
      memcpy(p, "PRI|", 4);
      p += 4;
    }
    if (event & EPOLLOUT) {
      memcpy(p, "OUT|", 4);
      p += 4;
    }
    if (event & EPOLLRDNORM) {
      memcpy(p, "RDNORM|", 7);
      p += 7;
    }
    if (event & EPOLLRDBAND) {
      memcpy(p, "RDBAND|", 7);
      p += 7;
    }
    if (event & EPOLLWRNORM) {
      memcpy(p, "WRNORM|", 7);
      p += 7;
    }
    if (event & EPOLLWRBAND) {
      memcpy(p, "WRBAND|", 7);
      p += 7;
    }
    if (event & EPOLLMSG) {
      memcpy(p, "MSG|", 4);
      p += 4;
    }
    if (event & EPOLLERR) {
      memcpy(p, "ERR|", 4);
      p += 4;
    }
    if (event & EPOLLHUP) {
      memcpy(p, "HUP|", 4);
      p += 4;
    }
    if (event & EPOLLRDHUP) {
      memcpy(p, "RDHUP|", 6);
      p += 6;
    }
    *p = '\0';
    lastEvent = event;
  }
  return str;
}

uint32_t
bytesToInt(const char *b, int size) {
  uint32_t v = b[0];
  for (int i = 1; i < size; ++i) {
    v = (v << 8);
    v |= (b[i] & 0xff);
  }
  return v;
}

const char *
intToBytes(int v, char *b, int size) {
  while (size--) {
    b[size] = (v & 0xff);
    v = (v >> 8);
  }
  return b;
}

bool
parseIpPort(const string &buffer, char *ip, int *port) {
  int colon = buffer.find(':');
  if (colon < 0) {
    *port = 80;
    memcpy(ip, buffer.data(), buffer.size());
    ip[buffer.size()] = '\0';
  } else {
    memcpy(ip, buffer.data(), colon);
    ip[colon] = '\0';
    if (sscanf(buffer.data() + colon + 1, "%d", port) != 1) {
      return false;
    }
  }
  return true;
}

Addr sockFdToAddr(int sockfd) {
  struct sockaddr_in peeraddr;
  socklen_t len = sizeof(struct sockaddr);
  getpeername(sockfd, (struct sockaddr *) &peeraddr, &len);
  int port = ntohs(peeraddr.sin_port);
  unsigned char *data = (unsigned char *) &peeraddr.sin_addr;
  Addr addr;
  addr.b[0] = data[0];
  addr.b[1] = data[1];
  addr.b[2] = data[2];
  addr.b[3] = data[3];
  addr.b[4] = ((port >> 8) & 0xff);
  addr.b[5] = (port & 0xff);
  addr.b[6] = 0;
  return addr;
}

char* fdToLocalAddr(int sockfd, char* str) {
  struct sockaddr_in addr;
  socklen_t len = sizeof(struct sockaddr);
  getsockname(sockfd, (struct sockaddr *) &addr, &len);
  int port = ntohs(addr.sin_port);
  unsigned char* data = (unsigned char *) &addr.sin_addr;
  sprintf(str, "%u.%u.%u.%u:%u", data[0], data[1], data[2], data[3], port);
  return str;
}

char* fdToPeerAddr(int sockfd, char* str) {
  struct sockaddr_in addr;
  socklen_t len = sizeof(struct sockaddr);
  getpeername(sockfd, (struct sockaddr *) &addr, &len);
  int port = ntohs(addr.sin_port);
  unsigned char* data = (unsigned char *) &addr.sin_addr;
  sprintf(str, "%u.%u.%u.%u:%u", data[0], data[1], data[2], data[3], port);
  return str;
}

