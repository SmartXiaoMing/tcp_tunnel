//
// Created by mabaiming on 17-10-15.
//

#ifndef TCP_TUNNEL_TUNNEL_UTILS_H
#define TCP_TUNNEL_TUNNEL_UTILS_H

#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <linux/if.h>
#include <sys/ioctl.h>
#include <time.h>

int logEnabled = 0;

#ifdef __DEBUG__
#define DEBUG printf
#define INFO printf
#define WARN printf
#define DEBUG_LINE do{ printf("*****%s %d %s\n", __FILE__, __LINE__, __FUNCTION__ ); }while(0)
#else
#define DEBUG_LINE do{ }while(0);
#define DEBUG(fmt, args...) do {if(logEnabled) syslog(LOG_DEBUG, fmt, ##args);} while (0)
#define INFO(fmt, args...) do {if(logEnabled) syslog(LOG_INFO, fmt, ##args);} while (0);
#define WARN(fmt, args...) do {syslog(LOG_WARNING, fmt, ##args);} while (0);
#endif

const int FULL_SIZE = 8128;

bool
isGoodCode() {
  int code = errno;
  printf("code == %d\n", code);
  return code == EAGAIN || code == EWOULDBLOCK || code == EINTR;
}

bool
getMacByName(char mac[20], int sock, const char* ifname) {
  struct ifreq ifr;
  strcpy(ifr.ifr_name, ifname);
  if (ioctl(sock, SIOCGIFFLAGS, &ifr) == 0) {
    if ((ifr.ifr_flags & IFF_LOOPBACK)) { // don't count loopback
      return false;
    }
    if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0) {
      unsigned char* b = (unsigned char *)ifr.ifr_hwaddr.sa_data;
      sprintf(
          mac, "%.2X:%.2X:%.2X:%.2X:%.2X:%.2X" ,
          b[0], b[1], b[2], b[3], b[4], b[5]
      );
      return true;
    }
  }
  return false;
}

bool
getMac(char mac[], int sock)  {
  char buf[2048];
  if (sock < 0) {
    return false;
  }
  if (getMacByName(mac, sock, "eth0")) {
    return true;
  }
  struct ifconf ifc;
  ifc.ifc_len = sizeof(buf);
  ifc.ifc_buf = buf;
  if (ioctl(sock, SIOCGIFCONF, &ifc) == -1) {
    return false;
  }
  struct ifreq* it = ifc.ifc_req;
  struct ifreq* end = it + (ifc.ifc_len / sizeof(struct ifreq));
  for (; it != end; ++it) {
    if (getMacByName(mac, sock, it->ifr_name)) {
      return true;
    }
  }
  return false;
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

#endif //TCP_TUNNEL_TUNNEL_UTILS_H
