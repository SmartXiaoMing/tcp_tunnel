//
// Created by mabaiming on 16-9-1.
//

#ifndef TCP_TUNNEL_COMMON_H
#define TCP_TUNNEL_COMMON_H

#include <map>
#include <string>
#include <vector>

using namespace std;

class Common {
public:
  static const int MAX_EVENTS = 500;
  static const int BUFFER_SIZE = 4096;
  static const int CONF_LINE_MAX_LENGTH = 8128;

  static bool parseFile(map<string, string> &result, const string &file);
  static bool parseCommandLine(map<string, string> &result, int argc, char *argv[]);
  static const string & optValue(const map<string, string> &kvMap, const string &key, const string &defaultValue = "");
  static bool split(vector<string> &result, const string &str, char ch);
  static int stringToInt(const string &str);
};

#endif //TCP_TUNNEL_COMMON_H
