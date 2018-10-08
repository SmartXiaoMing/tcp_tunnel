//
// Created by mabaiming on 18-9-28.
//

#ifndef TCP_TUNNEL_TUNNEL_BRANCH_H
#define TCP_TUNNEL_TUNNEL_BRANCH_H

#include "event_manager.h"
#include "event_handler.h"
#include "utils.h"
#include "frame.hpp"

class Branch: public EventHandler {
public:
  Branch(int fd, const uint8_t * addr): EventHandler(fd, 40960), halfBufferSize_(20480), addr_(addr) {}
  void onError();
  int onRead(const char* data, int size);
  void onWritten(bool fromFull, int writtenSize);
  void updateAck(int size);
private:
  int halfBufferSize_;
  int writtenSize_;
  int readableSize_;
  const uint8_t* addr_;
};
#endif //TCP_TUNNEL_TUNNEL_BRANCH_H
