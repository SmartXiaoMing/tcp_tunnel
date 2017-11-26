//
// Created by mabaiming on 16-9-1.
//

#ifndef TCP_TUNNEL_COMMON_H
#define TCP_TUNNEL_COMMON_H

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <map>
#include <string>
#include <vector>

using namespace std;

namespace Common {

const int BUFFER_SIZE = 4096;
const int CONF_LINE_MAX_LENGTH = 8128;
const int MAX_EVENTS = 500;
const int DefaultConnection = 30000;

struct Addr {
  string ip;
  uint16_t port;
  Addr(): ip("0.0.0.0"), port(0) {}
  Addr(const char* ip_, uint16_t port_): ip(ip_), port(port_) {}
  string toString() const;
  bool parse(string address);
};

class FdToAddr {
  int fd;
  bool local;
public:
  FdToAddr(int fd_, bool local_): fd(fd_), local(local_) {}
  Addr toAddr() const;
};

bool isIpV4(const string& ip);
const string & optValue(
    const map<string, string>& kvMap,
    const string& key,
    const string& defaultValue = ""
);
FdToAddr addrLocal(int fd);
FdToAddr addrRemote(int fd);
string intToString(int n);
bool isGoodCode();
bool parseAddressList(vector<Addr>& addrList, const string& addressList);
bool parseCommandLine(map<string, string> &result, int argc, char *argv[]);
bool parseFile(map<string, string>& result, const string &file);
bool parseKVList(map<string, string>& result, const string& content);
bool parseKVQuery(map<string, string>& result, const string& line);
string makeQuery(map<string, string>& result);
void savePid(const string& file);
bool split(vector<string> &result, const string &str, char ch);
int stringToInt(const string &str);
bool split(vector<string>& result, const string& str, const string& charList);
string skip(const string& str, const string& charList);
string formatTime(time_t ts);
bool getMacByName(string& mac, int sock, const char* name);
bool getMac(string& mac, int sock);
}
#endif //TCP_TUNNEL_COMMON_H
