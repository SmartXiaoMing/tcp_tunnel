#include "tcp_client.h"
#include "tcp_server.h"
#include "tunnel_package.h"

#include <string>
#include <iostream>

using namespace std;

int main(int argc, char * argv[]) {
  if (argc < 2) {
    cout << "usage " << argv[0] << " client|server" << endl;
  }
  map<string, string> paramMap;
  Common::parseCommandLine(paramMap, argc, argv);
  string confFile = Common::optValue(paramMap, "conf");
  if (!confFile.empty()) {
    if (!Common::parseFile(paramMap, confFile)) {
      log_warn << "cannot open file: " << confFile;
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
    log_error << "mode is unset";
    exit(EXIT_FAILURE);
  }
  // options: server, client
  if (mode == "server") {
    string tunnelIp = Common::optValue(paramMap, "server.tunnel.ip", "0.0.0.0");
    string tunnelPortStr = Common::optValue(paramMap, "server.tunnel.port");
    if (tunnelPortStr.empty()) {
      log_error << "server.tunnel.port is unset";
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
    log_info << "server.tunnel.ip: " << tunnelIp << ", server.tunnel.port: " << tunnelPort << ", tunnelConnection: " << tunnelConnection;
    log_info << "server.traffic.ip: " << trafficIp << ", server.traffic.port: " << trafficPortStr << ", trafficConnection: " << trafficConnection;

    TcpServer tcpServer;
    tcpServer.init(tunnelIp, tunnelPort, tunnelConnection, trafficIp, trafficPortList, trafficConnection);
    tcpServer.run();
  } else if (mode == "client") {
    string tunnelIp = Common::optValue(paramMap, "client.tunnel.ip", "127.0.0.1");
    string tunnelPortStr = Common::optValue(paramMap, "client.tunnel.port");
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
    log_info << "client.tunnel.ip: " << tunnelIp << ", client.tunnel.port: " << tunnelPort;
    log_info << "client.traffic.ip: " << trafficIp << ", client.traffic.port: " << trafficPort;
    TcpClient tcpClient;
    tcpClient.init(tunnelIp, tunnelPort, retryInterval, trafficIp, trafficPort);
    tcpClient.run();
  } else {
    log_error << "invalid mode: " << mode;
    exit(EXIT_FAILURE);
  }
  return EXIT_SUCCESS;
}