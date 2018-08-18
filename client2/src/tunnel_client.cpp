//
// Created by mabaiming on 18-8-15.
//

#include <stdlib.h>
#include <string.h>

#include "center_client.h"
#include "endpoint.h"

using namespace std;

int main(int argc, char** argv) {
  const char* dServerHost = "127.0.0.1";
  int dServerPort = 8120;
  const char* dGroup = "test";
  const char* dName = "anonymous";
  int dLevel = 1;

  const char* serverHost = dServerHost;
  int serverPort = dServerPort;
  const char* group = dGroup;
  const char* name = dName;
  logLevel = dLevel;

  for (int i = 1; i < argc; i += 2) {
    if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
      serverHost = argv[i + 1];
    } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
      serverPort = atoi(argv[i + 1]);
    } else if (strcmp(argv[i], "--group") == 0 && i + 1 < argc) {
      group = argv[i + 1];
    } else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
      name = argv[i + 1];
    } else if (strncmp(argv[i], "--v", 3) == 0) {
      logLevel = atoi(argv[i + 1]);
    } else {
      int ret = strcmp(argv[i], "--help");
      if (ret != 0) {
        printf("\nunknown option: %s\n", argv[i]);
      }
      printf("usage: %s [options]\n\n", argv[0]);
      printf("  --host domain.com        the server host, default %s\n", dServerHost);
      printf("  --port (0~65535)         the server port, default %d\n", dServerPort);
      printf("  --group group            the group name, default %s\n", dGroup);
      printf("  --name name              the name, default %s\n", dName);
      printf("  --v [0-5]                set log level, 0-3 means OFF, ERROR, WARN, INFO, DEBUG, default %d\n", dLevel);
      printf("  --help                   show the usage then exit\n");
      printf("\n");
      printf("version 1.0, report bugs to SmartXiaoMing(95813422@qq.com)\n");
      exit(ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
    }
  }

  Endpoint::init();
  CenterClient* center = new CenterClient();
  EndpointClient::setCenter(center);
  while (true) {
    center->prepare(serverHost, serverPort, group, name);
    Endpoint::loop();
  }
  return 0;
}
