#include "frame.h"
#include "tunnel_client.hpp"
#include "tunnel_monitor.hpp"
#include "tunnel_server.hpp"


#include <string>
#include <iostream>

using namespace std;
using namespace Common;

void showUsage(int argc, char* argv[]) {
  cout << "usage: " << argv[0] << " --mode=server|client|monitor [OPTION]\n";
  cout << "\t--tunnel.secret=str\tsecret to verify, unset means no verify.\n";
  cout << "options with mode=server:\n";
  cout << "\t--server.tunnel.address=[ip:]port\ttunnel bind ip:port\n";
  cout << "\t--server.tunnel.connection=connection\tclient count, default 10\n";
  cout << "\t--server.traffic.address=[ip:]port\ttraffic bind ip:port\n";
  cout << "\t--server.monitor.address=[ip:]port\tserver monitor bind ip:port\n";
  cout << "options with mode=client:\n";
  cout << "\t--client.tunnel.address=[host]:port\ttunnel server host:port\n";
  cout << "\t--client.tunnel.retry.interval=number\tdefault 10 seconds\n";
  cout << "\t--client.traffic.address=[host:]port\ttraffic address\n";
  cout << "options with mode=monitor:\n";
  cout << "\t--monitor.address=[host:]port\tmonitor connection address\n";
  cout << "\n";
  cout << "version 0.0.2\n";
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
    string tunnelAddressStr = optValue(paramMap, "server.tunnel.address", "");
    Addr tunnelAddr;
    if (!tunnelAddr.parse(tunnelAddressStr)) {
      log_error << "server.tunnel.address is invalid: " << tunnelAddressStr;
      exit(EXIT_FAILURE);
    }
    string trafficAddressStr = optValue(paramMap, "server.traffic.address", "");
    Addr trafficAddr;
    if (!trafficAddr.parse(trafficAddressStr)) {
      log_error << "server.traffic.address is invalid: " << trafficAddressStr;
      exit(EXIT_FAILURE);
    }
    string monitorAddressStr = optValue(paramMap, "server.monitor.address", "");
    Addr monitorAddr;
    if (!monitorAddr.parse(monitorAddressStr)) {
      log_error << "server.monitor.address is invalid: " << monitorAddressStr;
      exit(EXIT_FAILURE);
    }
    TunnelServer server;
	  server.init(tunnelAddr, trafficAddr, monitorAddr);
	  server.run();
  } else if (mode == "client") {
    string pifFile = optValue(paramMap, "pid.file", "client.tunnel.pid");
    savePid(pifFile);

	  string tunnelAddr = optValue(paramMap, "client.tunnel.address", "");
	  vector<Addr> addrList;
	  if (!parseAddressList(addrList, tunnelAddr)) {
		  log_error << "client.tunnel.address is invalid: " << tunnelAddr;
		  exit(EXIT_FAILURE);
	  }
	  string trafficAddressStr = optValue(paramMap, "client.traffic.address", "");
	  Addr trafficAddr;
	  if (!trafficAddr.parse(trafficAddressStr)) {
		  log_error << "client.traffic.address is invalid: " << trafficAddressStr;
		  exit(EXIT_FAILURE);
	  }
	  string monitorAddressStr = optValue(paramMap, "client.monitor.address", "");
	  Addr monitorAddr;
	  if (!monitorAddr.parse(monitorAddressStr)) {
		  log_error << "client.monitor.address is invalid: " << monitorAddressStr;
		  exit(EXIT_FAILURE);
	  }
    TunnelClient client;
	  client.init(tunnelAddr, trafficAddr, monitorAddr);
	  client.run();
  } else if (mode == "monitor") {
	  string monitorAddressStr = optValue(paramMap, "monitor.address", "");
	  Addr monitorAddr;
	  if (!monitorAddr.parse(monitorAddressStr)) {
		  log_error << "monitor.address is invalid: " << monitorAddressStr;
		  exit(EXIT_FAILURE);
	  }
    string cmd = optValue(paramMap, "monitor.cmd");
    if (cmd.empty()) {
      log_error << "server.monitor.cmd is unset";
      exit(EXIT_FAILURE);
    }
	  TunnelMonitor monitor;
	  monitor.init(monitorAddr, cmd);
	  monitor.run();
  } else {
    log_error << "invalid mode: " << mode;
    exit(EXIT_FAILURE);
  }

  return EXIT_SUCCESS;
}