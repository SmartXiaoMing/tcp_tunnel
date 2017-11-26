//
// Created by mabaiming on 17-1-24.
//

#ifndef TCP_TUNNEL_TUNNEL_RULE_HPP
#define TCP_TUNNEL_TUNNEL_RULE_HPP

#include "logger.h"

#include <string>
#include <vector>

using namespace std;

class TunnelRule {
private:
	struct Rule {
		string name;
		string remoteHost;
		int remotePort;
		int localPort;
		Rule(): name(), remoteHost(), remotePort(0), localPort(0) {}
	};
  vector<Rule> ruleList;
public:
	bool match(const string& name, const string& remoteHost,
     int remotePort, int localPort) {
		if (ruleList.empty()) {
			return true;
		}
		for (size_t i = 0; i < ruleList.size(); ++i) {
			const Rule& rule = ruleList[i];
			if (!rule.name.empty() && rule.name != name) {
				continue;
			}
			if (!rule.remoteHost.empty() && rule.remoteHost != remoteHost) {
				continue;
			}
			if (rule.remotePort > 0 && rule.remotePort != remotePort) {
				continue;
			}
			if (rule.localPort > 0 && rule.localPort != localPort) {
				continue;
			}
			return true;
		}
		return false;
	}

	bool parseFile(const string& file) {
		ifstream fin(file.c_str());
		if (!fin.is_open()) {
			log_error << "file not found: " << file;
			return false;
		}
		char buffer[CONF_LINE_MAX_LENGTH];
		int num = 0;
		while (fin.getline(buffer, CONF_LINE_MAX_LENGTH)) {
			++num;
			string line = buffer;
			if (line.empty() || line[0] == '#') {
				continue;
			}
			int valueEnd = line.find('#');
			if (valueEnd == string::npos) {
				valueEnd = line.length();
			}
      map<string, string> kv;
      parseKVList(kv, line.substr(0, valueEnd));
			if (kv.empty()) {
				log_warn << "skip bad line: " << line << " at line " << num;
        continue;
			}
			Rule rule;
      map<string, string>::iterator kvIt = kv.begin();
      for (; kvIt != kv.end(); ++kvIt) {
        if (kvIt->first == "name") {
          rule.name = kvIt->second;
        } else if (kvIt->first == "remoteHost") {
          rule.remoteHost = kvIt->second;
        } else if (kvIt->first == "remotePort") {
          rule.remotePort = stringToInt(kvIt->second);
        } else if (kvIt->first == "localPort") {
          rule.localPort = stringToInt(kvIt->second);
        } else {
          log_warn << "unsupported key: " << kvIt->first << " at line " << num;
        }
      }
      log_debug << "add rule, name: " << rule.name
        << ", remoteHost: " << rule.remoteHost
        << ", remotePort: " << rule.remotePort
        << ", localPort: " << rule.localPort;
			ruleList.push_back(rule);
		}
		return true;
	}

};
#endif //TCP_TUNNEL_TUNNEL_RULE_HPP
