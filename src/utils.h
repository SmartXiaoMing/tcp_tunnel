//
// Created by mabaiming on 17-10-15.
//

#ifndef TCP_TUNNEL_UTILS_H
#define TCP_TUNNEL_UTILS_H

#include <stdio.h>
#include <string.h>
#include <string>

using namespace std;

extern int logLevel;

#define TRACE do{ if(logLevel>4) printf("trace: %s %d %s\n", __FILE__, __LINE__, __FUNCTION__ );}while(0)
#define DEBUG(fmt, args...) do{if(logLevel>3){fprintf(stdout, fmt "\n", ##args);fflush(stdout);}}while(0)
#define INFO(fmt, args...) do{if(logLevel>2){fprintf(stdout, fmt "\n", ##args);fflush(stdout);}}while(0)
#define WARN(fmt, args...) do{if(logLevel>1){fprintf(stdout, fmt "\n", ##args);fflush(stdout);}}while(0)
#define ERROR(fmt, args...) do{if(logLevel>0){fprintf(stdout, fmt "\n", ##args);fflush(stdout);}}while(0)

typedef struct Addr {
  uint8_t b[6];
  uint32_t tid;
  Addr(): tid(0) {}
  Addr(uint32_t id): tid(id) {
    memset(b, 0, sizeof(b));
  }
  bool operator < (const Addr& that) const {
    int d = tid - that.tid;
    if (d != 0) {
      return d;
    }
    return memcmp((const char*)b, (const char*)that.b, 6) < 0;
  }
  bool operator == (const Addr& that) const {
    int d = tid - that.tid;
    if (d != 0) {
      return false;
    }
    return memcmp((const char*)b, (const char*)that.b, 6) == 0;
  }
} Addr;

bool isGoodCode();

bool isIpV4(const char* ip);
const char* selectIp(const char* host, char ipBuffer[], int size);
int createClient(const char* ip, int port);
int createServer(const char* ip, int port, int connectionCount);
const char* addrToStr(const uint8_t *b);
const char* eventToStr(int event);
uint32_t bytesToInt(const char* b, int size);
const char* intToBytes(int v, char* b, int size);
bool parseIpPort(const string& buffer, char* ip, int* port);

Addr sockFdToAddr(int sockfd);
char* fdToLocalAddr(int sockfd, char* addr);
char* fdToPeerAddr(int sockfd, char* addr);

#endif //TCP_TUNNEL_UTILS_H
