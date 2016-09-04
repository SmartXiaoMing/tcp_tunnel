//
// Created by mabaiming on 16-9-1.
//
#include "common.h"

#include "stdint.h"
#include "stdlib.h"
#include "logger.h"

#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace std;

bool
Common::parseFile(map<string, string>& result, const string& file) {
  ifstream fin(file.c_str());
  if (!fin.is_open()) {
    return false;
  }
  char buffer[CONF_LINE_MAX_LENGTH];
  while (fin.getline(buffer, CONF_LINE_MAX_LENGTH)) {
    string line = buffer;
    if (line.empty() || line[0] == '#') {
      continue;
    }
    int valueEnd = line.find('#');
    if (valueEnd == string::npos) {
      valueEnd = line.length();
    }
    while (valueEnd > 0 && isspace(line[valueEnd-1])) {
      --valueEnd;
    }
    if (valueEnd == 0) {
      continue;
    }
    int keyEnd = line.find('=');
    if (keyEnd >= valueEnd) {
      continue;
    }
    if (keyEnd == string::npos) {
      result[line.substr(0, keyEnd)] = "";
      continue;
    }
    string key = line.substr(0, keyEnd);
    string value = line.substr(keyEnd + 1, valueEnd - keyEnd - 1);
    result[key] = value;
  }
  return true;
};

bool
Common::parseCommandLine(map<string, string>& result, int argc, char * argv[]) {
  for (int i = 1; i < argc; ++i) {
    string param = argv[i];
    int pos = param.find('=');
    if (pos == string::npos) {
      pos = param.length();
    }
    int start = 0;
    while (param[start] == '-' && start < param.size()) {
      ++start;
    }
    if (start == 0 || start > 2) {
      log_error << "unrecognized param: " << param;
      exit(EXIT_FAILURE);
    }
    string key = param.substr(start, pos - start);
    string value = pos == param.length() ? "" : param.substr(pos + 1);
    result[key] = value;
  }
  return true;
}

const string&
Common::optValue(const map<string, string>& kvMap, const string& key, const string& defaultValue) {
  map<string, string>::const_iterator it = kvMap.find(key);
  if (it == kvMap.end()) {
    return defaultValue;
  }
  return it->second;
}

bool
Common::split(vector<string>& result, const string& str, char ch) {
  int offset = 0;
  int p = str.find(ch, offset);
  while (p != string::npos) {
    string s = str.substr(offset, p);
    if (!s.empty()) {
      result.push_back(s);
    }
    offset = p + 1;
    p = str.find(ch, offset);
  }
  if (offset < str.length()) {
    result.push_back(str.substr(offset));
  }
  return true;
}

int
Common::stringToInt(const string& str) {
  return atoi(str.c_str());
}
