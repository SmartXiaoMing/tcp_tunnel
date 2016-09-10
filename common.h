//
// Created by mabaiming on 16-9-1.
//

#ifndef TCP_TUNNEL_COMMON_H
#define TCP_TUNNEL_COMMON_H

#include <stdint.h>

#include <map>
#include <string>
#include <vector>

using namespace std;

namespace Common {

const int BUFFER_SIZE = 4096;
const int CONF_LINE_MAX_LENGTH = 8128;
const int MAX_EVENTS = 500;

struct Addr {
  string ip;
  uint16_t port;
  string toString();
};

string getAddress(int sockfd);
bool isIpV4(const string& ip);
const string & optValue(
    const map<string, string>& kvMap,
    const string& key,
    const string& defaultValue = ""
);
bool parseAddressList(vector<Addr>& addrList, const string& addressList);
bool parseCommandLine(map<string, string> &result, int argc, char *argv[]);
bool parseFile(map<string, string> &result, const string &file);
void savePid(const string& file);
bool split(vector<string> &result, const string &str, char ch);
int stringToInt(const string &str);

}

#endif //TCP_TUNNEL_COMMON_H
