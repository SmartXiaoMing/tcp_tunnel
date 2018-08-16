//
// Created by mabaiming on 17-10-15.
//

#ifndef TCP_TUNNEL_UTILS_H
#define TCP_TUNNEL_UTILS_H

#include <stdio.h>

extern int logLevel;

#define TRACE do{ if(logLevel>4) printf("trace: %s %d %s\n", __FILE__, __LINE__, __FUNCTION__ );}while(0)
#define DEBUG(fmt, args...) do{if(logLevel>3){printf(fmt, ##args);}}while(0)
#define INFO(fmt, args...) do{if(logLevel>2){printf(fmt, ##args);}}while(0)
#define WARN(fmt, args...) do{if(logLevel>1){printf(fmt, ##args);}}while(0)
#define ERROR(fmt, args...) do{if(logLevel>0){printf(fmt, ##args);}}while(0)

bool isGoodCode();

bool isIpV4(const char* ip);
const char* selectIp(const char* host, char ipBuffer[], int size);

#endif //TCP_TUNNEL_UTILS_H
