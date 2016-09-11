#include "tcp_client.h"
#include "tcp_monitor.h"
#include "tcp_server.h"
#include "tunnel_package.h"

#include <string>
#include <iostream>

using namespace std;
using namespace Common;

void showUsage(int argc, char* argv[]) {
  cout << "usage: " << argv[0] << " --mode=server|client|monitor [OPTION]\n";
  cout << "\t--tunnel.secret=str\tsecret to verify, unset means no verify.\n";
  cout << "options with mode=server:\n";
  cout << "\t--server.tunnel.ip=ip\ttunnel server bind ip, default 0.0.0.0\n";
  cout << "\t--server.tunnel.port=number\ttunnel bind port\n";
  cout << "\t--server.tunnel.connection=connection\tclient count, default 10\n";
  cout << "\t--server.traffic.ip=ip\ttraffic bind ip, default 0.0.0.0\n";
  cout << "\t--server.traffic.port=number\ttraffic bind port\n";
  cout << "\t--server.monitor.port=number\tserver monitor bind port\n";
  cout << "options with mode=client:\n";
  cout << "\t--client.tunnel.addr=host:port1[,port2][;]\ttunnel server addr\n";
  cout << "\t--client.tunnel.retry.interval=number\tdefault 10 seconds\n";
  cout << "\t--client.tunnel.heartbeat=number\tdefault 60 seconds\n";
  cout << "\t--client.traffic.ip=ip\ttraffic ip, default 127.0.0.1\n";
  cout << "\t--client.traffic.port=number\ttraffic port\n";
  cout << "options with mode=monitor:\n";
  cout << "\t--server.monitor.port=number\tserver monitor bind port\n";
  cout << "\n";
  cout << "version 0.0.1\n";
  cout << "Report tunnel bugs to 95813422@qq.com\n";
}

int main(int argc, char * argv[]) {
  map<string, string> inputParamMap;
  parseCommandLine(inputParamMap, argc, argv);
  string help = optValue(inputParamMap, "help", "false");
  if (help != "false") {
    showUsage(argc, argv);
    exit(EXIT_SUCCESS);
  }
  string confFile = optValue(inputParamMap, "conf");
  if (confFile.empty()) {
    confFile = "tunnel.conf";
  }
  map<string, string> paramMap;
  if (!parseFile(paramMap, confFile)) {
    log_warn << "cannot open config file: " << confFile;
  } else { // merge param with inputParam
    map<string, string>::iterator it = inputParamMap.begin();
    for (; it != inputParamMap.end(); ++it) {
      paramMap[it->first] = it->second;
    }
  }

  LoggerManager::init(
      optValue(paramMap, "log.level", "INFO"),
      optValue(paramMap, "log.file", "stdout"),
      optValue(paramMap, "log.file.append", "true") == "true",
      optValue(paramMap, "log.debug", "false") == "true"
  );

  string mode = optValue(paramMap, "mode");
  if (mode.empty()) {
    log_error << "param mode is unset";
    exit(EXIT_FAILURE);
  }

  string tunnelSecret = optValue(paramMap, "tunnel.secret");
  if (mode == "server") {
    string pifFile = optValue(paramMap, "pid.file", "server.tunnel.pid");
    savePid(pifFile);

    string tunnelIp = optValue(paramMap, "server.tunnel.ip", "0.0.0.0");
    string tunnelPortStr = optValue(paramMap, "server.tunnel.port");
    if (tunnelPortStr.empty()) {
      log_error << "server.tunnel.port is unset\n";
      exit(EXIT_FAILURE);
    }
    uint16_t tunnelPort = stringToInt(tunnelPortStr);
    if (tunnelPort <= 0) {
      log_error << "server.tunnel.port is invalid: " << tunnelPort;
      exit(EXIT_FAILURE);
    }
    int tunnelConnection
        = stringToInt(optValue(paramMap, "server.tunnel.connection", "10"));
    string trafficIp = optValue(paramMap, "server.traffic.ip", "0.0.0.0");
    string trafficPortStr = optValue(paramMap, "server.traffic.port");
    vector<string> trafficPortStrList;
    split(trafficPortStrList, trafficPortStr, ',');
    vector<uint16_t> trafficPortList;
    for (int i = 0; i < trafficPortStrList.size(); ++i) {
      uint16_t port = stringToInt(trafficPortStrList[i]);
      if (port > 0) {
        trafficPortList.push_back(port);
      }
    }
    if (trafficPortList.empty()) {
      log_error << "server.traffic.port is invalid: " << trafficPortStr;
      exit(EXIT_FAILURE);
    }
    int trafficConnection
        = stringToInt(optValue(paramMap, "server.traffic.connection", "1"));

    string monitorPortStr = optValue(paramMap, "server.monitor.port");
    if (monitorPortStr.empty()) {
      log_error << "server.monitor.port is unset";
      exit(EXIT_FAILURE);
    }
    uint16_t monitorPort = stringToInt(monitorPortStr);

    TcpServer tcpServer;
    tcpServer.init(
        tunnelIp, tunnelPort, tunnelConnection,
        trafficIp, trafficPortList, trafficConnection,
        monitorPort,
        tunnelSecret
    );
    tcpServer.run();
  } else if (mode == "client") {
    string pifFile = optValue(paramMap, "pid.file", "client.tunnel.pid");
    savePid(pifFile);

    string addrStr = optValue(paramMap, "client.tunnel.addr");
    if (addrStr.empty()) {
      log_error << "client.tunnel.addr is unset";
      exit(EXIT_FAILURE);
    }
    int retryInterval
        = stringToInt(optValue(paramMap, "client.tunnel.retry.interval", "10"));
    int heartbeat
        = stringToInt(optValue(paramMap, "client.tunnel.heartbeat", "60"));
    string trafficIp = optValue(paramMap, "client.traffic.ip", "127.0.0.1");
    string trafficPortStr = optValue(paramMap, "client.traffic.port");
    int trafficPort = stringToInt(trafficPortStr);
    if (trafficPort <= 0 || trafficPort > 65535) {
      log_error << "client.traffic.port is invalid: " << trafficPortStr;
      exit(EXIT_FAILURE);
    }
    log_info << "traffic: " << trafficIp << ":" << trafficPort;
    TcpClient tcpClient;
    tcpClient.init(
        addrStr, retryInterval, trafficIp, trafficPort, tunnelSecret, heartbeat
    );
    tcpClient.run();
  } else if (mode == "monitor") {
    string portStr = optValue(paramMap, "server.monitor.port");
    if (portStr.empty()) {
      log_error << "server.monitor.port is unset";
      exit(EXIT_FAILURE);
    }
    int port = stringToInt(portStr);
    if (port <= 0) {
      log_error << "server.monitor.port is invalid: " << portStr;
      exit(EXIT_FAILURE);
    }
    string cmd = optValue(paramMap, "server.monitor.cmd");
    if (cmd.empty()) {
      log_error << "server.monitor.cmd is unset";
      exit(EXIT_FAILURE);
    }
    TcpMonitor tcpMonitor;
    tcpMonitor.init(port);
    tcpMonitor.run(cmd);
  } else {
    log_error << "invalid mode: " << mode;
    exit(EXIT_FAILURE);
  }

  return EXIT_SUCCESS;
}