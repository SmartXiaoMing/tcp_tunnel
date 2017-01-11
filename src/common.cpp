//
// Created by mabaiming on 16-9-1.
//
#include "common.h"

#include "logger.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace std;

extern int errno;

namespace Common {

bool
Addr::parse(string value) {
  if (value.size() <= 1) {
    return false;
  }
  int p = value.find(':');
  if (p <= 0) {
    ip = "0.0.0.0";
  } else {
    ip = value.substr(0, p);
    if (!isIpV4(ip)) {
      return false;
    }
  }
  if (p >= 0) {
    port = stringToInt(value.substr(p+1));
  } else {
    port = stringToInt(value);
  }
  return port > 0 && port <= 65535;
}

string
Addr::toString() const {
  string result;
  result.assign(ip);
  result.append(":");
  result.append(intToString(port));
  return result;
}

Addr
FdToAddr::toAddr() const {
  if (fd <= 0) {
    return Addr();
  }
  struct sockaddr_in sa;
  socklen_t len = sizeof(sa);
  if (local) {
    if (getsockname(fd, (struct sockaddr *) &sa, &len) == 0) {
      return Addr(inet_ntoa(sa.sin_addr), ntohs(sa.sin_port));
    }
  } else {
    if (getpeername(fd, (struct sockaddr *) &sa, &len) == 0) {
      return Addr(inet_ntoa(sa.sin_addr), ntohs(sa.sin_port));
    }
  }
  return Addr();
}

FdToAddr
addrLocal(int fd) {
  return FdToAddr(fd, true);
}

FdToAddr
addrRemote(int fd) {
  return FdToAddr(fd, false);
}

bool
isGoodCode() {
  int code = errno;
  return code == EAGAIN || code == EWOULDBLOCK || code == EINTR;
}

string
intToString(int n) {
  char buffer[32];
  sprintf(buffer, "%u", n);
  return buffer;
}

bool
parseAddressList(vector<Addr>& addrList, const string& addressList) {
  // ip:port1,port2;host:port1,port2
  int offset = 0;
  while (offset < addressList.length()) {
    int p = addressList.find(';', offset);
    if (p == string::npos) {
      p = addressList.length();
    }
    string addr = addressList.substr(offset, p - offset);
    offset = p + 1;
    if (addr.empty()) {
      continue;
    }
    int hostPos = addr.find(':');
    if (hostPos == string::npos) {
      log_error << "invalid address: " << addr;
      return false;
    }

    string host = addr.substr(0, hostPos);
    vector<string> portStrList;
    split(portStrList, addr.substr(hostPos + 1), ',');
    if (portStrList.empty()) {
      log_error << "invalid address: " << addr << ", no port provided";
      return false;
    }
    vector<uint16_t> portList;
    for (int i = 0; i < portStrList.size(); ++i) {
      int port = stringToInt(portStrList[i]);
      if (port <= 0 || port > 65535) {
        log_error << "invalid address: " << addr << ", invalid port range";
        return false;
      }
      portList.push_back(port);
    }
    vector<string> ipList;
    if (isIpV4(host)) {
      ipList.push_back(host);
    } else {
      hostent* info = gethostbyname(host.c_str());
      if (info == NULL || info->h_addrtype != AF_INET) {
        log_warn << "address: " << host << ", cannot be resolved";
        continue;
      }
      for (char** ptr = info->h_addr_list; *ptr != NULL; ++ptr) {
        char dst[30];
        socklen_t len = 29;
        ipList.push_back(inet_ntop(AF_INET, *ptr, dst, len));
      }
    }
    for (int i = 0; i < ipList.size(); ++i) {
      Addr addr;
      addr.ip = ipList[i];
      for (int j = 0; j < portList.size(); ++j) {
        addr.port = portList[j];
        addrList.push_back(addr);
      }
    }
  }
  return true;
}

bool
isIpV4(const string& ip) {
  // 000.000.000.000
  vector<string> list;
  split(list, ip, '.');
  if (list.size() != 4) {
    return false;
  }
  for (int i = 0; i < list.size(); ++i) {
    if (list[i].empty()) {
      return false;
    }
    int v = stringToInt(list[i]);
    if (v < 0 || v > 255) {
      return false;
    }
  }
  return true;}

bool
parseCommandLine(map<string, string> &result, int argc, char *argv[]) {
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

bool
parseFile(map<string, string> &result, const string &file) {
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
    while (valueEnd > 0 && isspace(line[valueEnd - 1])) {
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

const string &
optValue(const map<string, string> &kvMap, const string &key,
    const string& defaultValue) {
  map<string, string>::const_iterator it = kvMap.find(key);
  if (it == kvMap.end()) {
    return defaultValue;
  }
  return it->second;
}

bool
split(vector<string> &result, const string &str, char ch) {
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
stringToInt(const string &str) {
  return atoi(str.c_str());
}

void
savePid(const string &file) {
  if (!file.empty()) {
    ofstream fout(file.c_str());
    if (fout.is_open()) {
      fout << getpid() << endl;
      fout.close();
    }
  }
}

string
formatTime(time_t ts) {
	struct tm* ptr;
	time_t lt;
	struct tm* localTimePtr = localtime(&ts);
	char timeStr[80];
	strftime(timeStr, 80, "%F %T", localTimePtr);
	return string(timeStr);
}

bool
getMac(string& mac, int sock)	{
	char buf[2048];
	if (sock < 0) {
		return false;
	}
	struct ifconf ifc;
	ifc.ifc_len = sizeof(buf);
	ifc.ifc_buf = buf;
	if (ioctl(sock, SIOCGIFCONF, &ifc) == -1) {
		return false;
	}
	struct ifreq* it = ifc.ifc_req;
	const struct ifreq* const end = it + (ifc.ifc_len / sizeof(struct ifreq));
	for (; it != end; ++it) {
		struct ifreq ifr;
		strcpy(ifr.ifr_name, it->ifr_name);
		if (ioctl(sock, SIOCGIFFLAGS, &ifr) == 0) {
			if ((ifr.ifr_flags & IFF_LOOPBACK)) { // don't count loopback
				continue;
			}
			if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0) {
				unsigned char* b = (unsigned char *)ifr.ifr_hwaddr.sa_data;
				char str[20];
				sprintf(
					str, "%.2X:%.2X:%.2X:%.2X:%.2X:%.2X" ,
					b[0], b[1], b[2], b[3], b[4], b[5]
				);
				mac.assign(str);
				return true;
			}
		}
	}
	return false;
}
}
