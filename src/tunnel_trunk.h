//
// Created by mabaiming on 18-9-28.
//

#ifndef TCP_TUNNEL_TUNNEL_TRUNK_H
#define TCP_TUNNEL_TUNNEL_TRUNK_H

#include <map>

#include "event_handler.h"
#include "utils.h"
#include "frame.hpp"
#include "tunnel_branch.h"

using namespace std;

class Trunk: public EventHandler {
public:
  static Trunk* sSingle;
  static void prepare(const char* host, int port, const char* group, const char* name);

  Trunk(int fd): EventHandler(fd, 409600) {}
  void sendDataToTunnel(uint8_t state, const uint8_t* addr, const char* data, int size);
  bool processFrame();
  void onError();
  int onRead(const char* data, int size);
  void onWritten(bool fromFull, int writtenSize);
private:
  Frame frame_;
  map<const uint8_t*, Branch*, AddrCompare> branches_;
};
#endif //TCP_TUNNEL_TUNNEL_TRUNK_H
