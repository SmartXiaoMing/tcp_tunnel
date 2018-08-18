//
// Created by mabaiming on 18-8-15.
//

#include <stdlib.h>
#include <string.h>

#include "center_server.h"
#include "endpoint.h"

using namespace std;

int main(int argc, char** argv) {
  int dTunnelPort = 8120;
  int dTrafficPort = 8121;
  int dLevel = 1;

  int tunnelPort = dTunnelPort;
  int trafficPort = dTrafficPort;
  logLevel = dLevel;

  for (int i = 1; i < argc; i += 2) {
    if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
      tunnelPort = atoi(argv[i + 1]);
    }  else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
      trafficPort = atoi(argv[i + 1]);
    } else if (strncmp(argv[i], "--v", 3) == 0) {
      logLevel = atoi(argv[i + 1]);
    } else {
      int ret = strcmp(argv[i], "--help");
      if (ret != 0) {
        printf("\nunknown option: %s\n", argv[i]);
      }
      printf("usage: %s [options]\n\n", argv[0]);
      printf("  --tunnelPort num         the tunnel port, default %d\n", dTunnelPort);
      printf("  --trafficPort num        the traffic port, default %d\n", dTrafficPort);
      printf("  --v [0-5]                set log level, 0-3 means OFF, ERROR, WARN, INFO, DEBUG, default %d\n", dLevel);
      printf("  --help                   show the usage then exit\n");
      printf("\n");
      printf("version 1.0, report bugs to SmartXiaoMing(95813422@qq.com)\n");
      exit(ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
    }
  }

  Endpoint::init();
  CenterServer* center = new CenterServer();
  EndpointClient::setCenter(center);
  center->prepare(tunnelPort, trafficPort);
  while (true) {
    Endpoint::loop();
  }
  return 0;
}
