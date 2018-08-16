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
isIpV4(const char* ip) {
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


const char*
selectIp(const char* host, char ipBuffer[], int size) {
  if (isIpV4(host)) {
    strcpy(ipBuffer, host);
    return ipBuffer;
  }
  struct hostent* info = gethostbyname(host);
  if (info == NULL || info->h_addrtype != AF_INET) {
    WARN("invalid host: %s\n", host);
    return NULL;
  }
  int ipCount = 0;
  for (char** ptr = info->h_addr_list; *ptr != NULL; ++ptr) {
    ipCount++;
  }
  int index = 0;
  if (ipCount > 0) {
    srand(time(0));
    index = rand() % ipCount;
  }
  return inet_ntop(AF_INET, *(info->h_addr_list + index), ipBuffer, size);
}
