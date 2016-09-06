#include "tcp_client.h"
#include "tcp_server.h"
#include "tunnel_package.h"

#include <string>
#include <iostream>

using namespace std;

void showUsage(string name) {
	cout << "usage: " << name << " --mode=server|client --tunnel.port=number [OPTION]" << endl;
	cout << "options with mode=server:" << endl;
	cout << "\t--server.tunnel.ip=ip\ttunnel server bind ip, default 0.0.0.0" << endl;
	cout << "\t--server.tunnel.port=number\ttunnel server bind port, default use tunnel.port" << endl;
	cout << "\t--server.tunnel.connection=connection\tallow client connection count, default 10" << endl;
	cout << "\t--server.traffic.ip=ip\ttraffic downstream bind ip, default 0.0.0.0" << endl;
	cout << "\t--server.traffic.port=number\ttraffic downstream bind port" << endl;
	cout << "options with mode=client:" << endl;
	cout << "\t--client.tunnel.ip=ip\ttunnel server's ip" << endl;
	cout << "\t--client.tunnel.port=number\ttunnel server's port, default use tunnel.port" << endl;
	cout << "\t--client.traffic.ip=ip\ttraffic upstream's ip" << endl;
	cout << "\t--client.traffic.port=number\ttraffic upstream's port" << endl;
	cout << endl;
	cout << "version 0.0.1" << endl;
	cout << "Report tcptunnel bugs to 95813422@qq.com" << endl;
}

int main(int argc, char * argv[]) {
  map<string, string> inputParamMap;
  Common::parseCommandLine(inputParamMap, argc, argv);
	string help = Common::optValue(inputParamMap, "help", "false");
	if (help != "false") {
    showUsage(argv[0]);
    exit(EXIT_SUCCESS);
	}
  string confFile = Common::optValue(inputParamMap, "conf");
  if (confFile.empty()) {
    confFile = "tunnel.conf";
  }
  map<string, string> paramMap;
	if (!Common::parseFile(paramMap, confFile)) {
		log_warn << "cannot open config file: " << confFile;
	} else { // merge param with inputParam
		for (map<string, string>::iterator it = inputParamMap.begin(); it != inputParamMap.end(); ++it) {
			paramMap[it->first] = it->second;
		}
	}

  LoggerManager::init(
    Common::optValue(paramMap, "log.level", "INFO"),
    Common::optValue(paramMap, "log.file", "stdout"),
    Common::optValue(paramMap, "log.file.append", "true") == "true",
    Common::optValue(paramMap, "log.debug", "false") == "true"
  );

  string mode = Common::optValue(paramMap, "mode");
  if (mode.empty()) {
    log_error << "param mode is unset";
    exit(EXIT_FAILURE);
  }
  string tunnelPort = Common::optValue(paramMap, "tunnel.port");
  if (mode == "server") {
    string tunnelIp = Common::optValue(paramMap, "server.tunnel.ip", "0.0.0.0");
    string tunnelPortStr = Common::optValue(paramMap, "server.tunnel.port", tunnelPort);
    if (tunnelPortStr.empty()) {
      log_error << "server.tunnel.port is unset" << endl;
      exit(EXIT_FAILURE);
    }
    uint16_t tunnelPort = Common::stringToInt(tunnelPortStr);
    if (tunnelPort <= 0) {
      log_error << "server.tunnel.port is invalid: " << tunnelPort;
      exit(EXIT_FAILURE);
    }
    int tunnelConnection = Common::stringToInt(Common::optValue(paramMap, "server.tunnel.connection", "1"));

    string trafficIp = Common::optValue(paramMap, "server.traffic.ip", "0.0.0.0");
    string trafficPortStr = Common::optValue(paramMap, "server.traffic.port");
    vector<string> trafficPortStrList;
    Common::split(trafficPortStrList, trafficPortStr, ',');
    vector<uint16_t> trafficPortList;
    for (int i = 0; i < trafficPortStrList.size(); ++i) {
      uint16_t port = Common::stringToInt(trafficPortStrList[i]);
      if (port > 0) {
        trafficPortList.push_back(port);
      }
    }
    if (trafficPortList.empty()) {
      log_error << "server.traffic.port is unset or invalid: " << trafficPortStr;
      exit(EXIT_FAILURE);
    }
    int trafficConnection = Common::stringToInt(Common::optValue(paramMap, "server.traffic.connection", "1"));
    log_info << "listen tunnel(" << tunnelConnection << "): " << tunnelIp << ":" << tunnelPort;
    log_info << "listen traffic(" << trafficConnection << "): " << trafficIp << ":" << trafficPortStr;

    TcpServer tcpServer;
    tcpServer.init(tunnelIp, tunnelPort, tunnelConnection, trafficIp, trafficPortList, trafficConnection);
    tcpServer.run();
  } else if (mode == "client") {
    string tunnelIp = Common::optValue(paramMap, "client.tunnel.ip", "127.0.0.1");
    string tunnelPortStr = Common::optValue(paramMap, "client.tunnel.port", tunnelPort);
    uint16_t tunnelPort = Common::stringToInt(tunnelPortStr);
    if (tunnelPort <= 0) {
      log_error << "client.tunnel.port is unset or invalid: " << tunnelPortStr;
      exit(EXIT_FAILURE);
    }
    int retryInterval = Common::stringToInt(Common::optValue(paramMap, "client.tunnel.retry.interval", "1"));

    string trafficIp = Common::optValue(paramMap, "client.traffic.ip", "127.0.0.1");
    string trafficPortStr = Common::optValue(paramMap, "client.traffic.port");
    uint16_t trafficPort = Common::stringToInt(trafficPortStr);
    if (trafficPort <= 0) {
      log_error << "client.traffic.port is unset or invalid: " << trafficPortStr;
      exit(EXIT_FAILURE);
    }
    log_info << "connect tunnel: " << tunnelIp << ":" << tunnelPort;
    log_info << "traffic upstream: " << trafficIp << ":" << trafficPort;
    TcpClient tcpClient;
    tcpClient.init(tunnelIp, tunnelPort, retryInterval, trafficIp, trafficPort);
    tcpClient.run();
  } else {
    log_error << "invalid mode: " << mode;
    exit(EXIT_FAILURE);
  }
  return EXIT_SUCCESS;
}