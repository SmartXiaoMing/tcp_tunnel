//
// Created by mabaiming on 18-9-27.
//

#ifndef TCP_TUNNEL_EVENT_MANAGER_H
#define TCP_TUNNEL_EVENT_MANAGER_H

#include "event_handler.h"
#include "utils.h"
#include <string>
#include <set>
#include <sys/epoll.h>
#include <fcntl.h>
#include <set>
#include <stdio.h>

using namespace std;

class EventHandler;

class EventManager {
public:
  static EventManager* sSingle;
  void addHandler(EventHandler* eventHandler);
  void init();
  void loop();
  int epollFd;
private:
  void update();
  void recycle();
  set<EventHandler*> sUpdateSet;
  set<EventHandler*> sRecycleSet;
};
#endif //TCP_TUNNEL_EVENT_MANAGER_H
