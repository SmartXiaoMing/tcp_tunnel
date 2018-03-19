//
// Created by mabaiming on 17-10-8.
//

#ifndef TCP_TUNNEL_TUNNEL_CLIENT_H
#define TCP_TUNNEL_TUNNEL_CLIENT_H

#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>

#include "tunnel_frame.h"
#include "tunnel_buffer.h"
#include "tunnel_utils.h"
#include "tunnel_list.h"


typedef struct Tunnel {
  int fd;
  struct epoll_event ev;
  Buffer* input; // pop by traffic
  Buffer* output; // push by traffic
} Tunnel;

typedef struct Traffic {
  int fd;
  struct epoll_event ev;
  Buffer* input;
  Buffer* output;
  int cid;
  bool creating;
  bool closedByTunnel;
  bool closedBySystem; // true means no event listened
  Iterator* trafficListIt;
  Iterator* readyQueueIt;
  Iterator* recycleQueueIt;
} Traffic;

typedef struct Context {
  int epollFd;
  char* tunnelHost;
  int tunnelPort;
  Tunnel* tunnel;
  List* trafficList;
  List* recycleTrafficList;
  List* readyTrafficList;
  Frame* frame;
  Buffer* group;
  Buffer* chanelSecret;
  Buffer* password;
} Context;

Context* contextGet();
void updateEvent(int fd, struct epoll_event* ev, int toAdd, int toDelete);
Tunnel* tunnelConnect(const char* host, int port);
int tunnelOnEvent(Tunnel* tunnel, int events);
void tunnelRecycle(Tunnel* tunnel);
void tunnelOnTrafficWritable();
void tunnelOnTrafficReadable();
bool trafficMatch(void* a, void* b);
Traffic* trafficConnect(char* ip, int port, int cid);
int trafficOnEvent(Traffic* traffic, int events);
int trafficHandleFrame(Traffic* traffic);
void trafficSystemClose(Traffic* traffic);
void trafficTunnelClose(Traffic* traffic);
void trafficDataToTunnel(Tunnel* tunnel);
void trafficListOnTunnelBroken();
void trafficListCycleClear();

Context*
contextGet() {
  static struct Context* context = NULL;
  if (context == NULL) {
    context = malloc(sizeof(Context));
    context->epollFd = epoll_create1(0);
    if(context->epollFd < 0) {
      WARN("failed to epoll_create1\n");
      exit(EXIT_FAILURE);
    }
    fcntl(
        context->epollFd,
        F_SETFL,
        fcntl(context->epollFd, F_GETFL, 0) | O_NONBLOCK
    );
    context->tunnel = NULL;
    context->trafficList = listNew();
    context->recycleTrafficList = listNew();
    context->readyTrafficList = listNew();
    context->tunnelHost = "127.0.0.1";
    context->tunnelPort = 8120;
  }
  return context;
};


void
updateEvent(int fd, struct epoll_event* ev, int toAdd, int toDelete) {
  int newEvent = ev->events;
  if (toAdd) {
    newEvent |= toAdd;
  }
  if (toDelete) {
    newEvent &= ~toDelete;
  }
  if (newEvent != ev->events) {
    ev->events &= EPOLLIN;
    ev->events = newEvent;
    epoll_ctl(contextGet()->epollFd, EPOLL_CTL_MOD, fd, ev);
  }
}

Tunnel*
tunnelConnect(const char* host, int port) {
  int fd = socket(PF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    WARN("failed to socket: %d\n", fd);
    return NULL;
  }
  char ip[30];
  if (selectIp(host, ip, sizeof(ip)) == NULL) {
    WARN("invalid host: %s\n", host);
  } else {
    WARN("select ip: %s\n", ip);
  }
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr(ip);
  addr.sin_port = htons(port);
  if (connect(fd, (struct sockaddr *)&addr, sizeof(struct sockaddr)) < 0) {
    WARN("failed to connect %s:%d\n", ip, port);
    return NULL;
  }
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
  Tunnel* tunnel = malloc(sizeof(Tunnel));
  tunnel->fd = fd;
  tunnel->input = bufferInit(4096);
  tunnel->output = bufferInit(4096);
  tunnel->ev.events = (EPOLLERR | EPOLLHUP | EPOLLIN);
  tunnel->ev.data.ptr = tunnel;
  Context* context = contextGet();
  epoll_ctl(context->epollFd, EPOLL_CTL_ADD, tunnel->fd, &tunnel->ev);
  char mac[20] = "FF:FF:FF:FF:FF:FF";
  getMac(mac, fd);
  Buffer* buffer = bufferInit(1024);
  bufferAdd(buffer, "name=", 5);
  bufferAdd(buffer, mac, strlen(mac));
  frameEncodeAppend(0, STATE_LOGIN, buffer->data, buffer->size, tunnel->output);
  bufferRecycle(buffer);
  return tunnel;
}

int
tunnelOnEvent(Tunnel* tunnel, int events) {
  DEBUG("tunnel:%p, event:%d\n", tunnel, events);
  if ((events & EPOLLERR) || (events & EPOLLHUP)) {
    return -1;
  }
  if (events & EPOLLIN) {
    Buffer* buffer = tunnel->input;
    while (buffer->size < FULL_SIZE) {
      int remain = FULL_SIZE - buffer->size;
      char buf[remain];
      int len = recv(tunnel->fd, buf, remain, 0);
      DEBUG("recv tunnel: %p, fd: %d, len:%d, buffer->size:%d, buffer:%.*s\n", tunnel, tunnel->fd, len, buffer->size, buffer->size, buffer->data);
      if (len == 0) {
        return -1;
      } else if (len > 0) {
        bufferAdd(buffer, buf, len);
      } else if (!isGoodCode()) {
        return -1;
      }
      if (trafficHandleFrame(NULL) < 0) {
        DEBUG("tunnelHandleFrame error\n");
        return -1;
      }
      if (len < remain) {
        break;
      }
    }
    if (buffer->size >= FULL_SIZE) {
      updateEvent(tunnel->fd, &tunnel->ev, 0, EPOLLIN);
    }
  }
  if (events & EPOLLOUT) {
    Buffer* buffer = tunnel->output;
    bool writable = true;
    trafficDataToTunnel(tunnel);
    while (writable) {
      if (buffer->size == 0) {
        updateEvent(tunnel->fd, &tunnel->ev, 0, EPOLLOUT);
        break;
      }
      int len = send(tunnel->fd, buffer->data, buffer->size, MSG_NOSIGNAL);
      DEBUG("send tunnel: %p, fd: %d, len:%d, buffer->size:%d, buffer:%.*s\n",
            tunnel, tunnel->fd, len, buffer->size, buffer->size,
            buffer->data);
      if (len < 0 && !isGoodCode()) {
        return -1;
      }
      if (len < buffer->size) {
        writable = false;
      }
      if (len > 0) {
        bufferPopFront(buffer, len);
      }
      trafficDataToTunnel(tunnel);
    }
  }
  return 0;
}

void
tunnelRecycle(Tunnel* tunnel) {
  WARN("tunnelRecycle:%p\n", tunnel);
  trafficListOnTunnelBroken();
  bufferRecycle(tunnel->input);
  bufferRecycle(tunnel->output);
  close(tunnel->fd);
  free(tunnel);
}

void
tunnelOnTrafficWritable() {
  Tunnel* tunnel = contextGet()->tunnel;
  if (tunnel) {
    updateEvent(tunnel->fd, &tunnel->ev, EPOLLIN, 0);
  }
}

void
tunnelOnTrafficReadable() {
  Tunnel* tunnel = contextGet()->tunnel;
  if (tunnel) {
    updateEvent(tunnel->fd, &tunnel->ev, EPOLLOUT, 0);
  }
}

bool
trafficMatch(void* a, void* b) {
  return ((Traffic*)a)->cid == ((Traffic*)b)->cid;
}

Traffic*
trafficConnect(char* ip, int port, int cid) {
  Context* context = contextGet();
  int fd = socket(PF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    WARN("failed to socket: %d\n", fd);
    return NULL;
  }
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr(ip);
  addr.sin_port = htons(port);
  Traffic* traffic = malloc(sizeof(Traffic));
  traffic->ev.data.ptr = traffic;
  traffic->ev.events = (EPOLLERR | EPOLLHUP | EPOLLIN | EPOLLOUT);
  traffic->fd = fd;
  traffic->input = bufferInit(4096);
  traffic->output = bufferInit(4096);
  traffic->cid = cid;
  traffic->closedByTunnel = false;
  traffic->closedBySystem = false;
  traffic->creating = true;
  if (connect(fd, (struct sockaddr *)&addr, sizeof(struct sockaddr)) < 0) {
    traffic->closedBySystem = true;
    traffic->creating = false;
  } else {
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    epoll_ctl(context->epollFd, EPOLL_CTL_ADD, traffic->fd, &traffic->ev);
    WARN("failed to connect %s:%d\n", ip, port);
  }
  traffic->trafficListIt = listAdd(context->trafficList, traffic);
  traffic->readyQueueIt = listAdd(context->readyTrafficList, traffic);
  traffic->recycleQueueIt = NULL;
  DEBUG("success to create traffic: %p, fd:%d, it:%p\n", traffic, traffic->fd, traffic->trafficListIt);
  return traffic;
}

int
trafficOnEvent(Traffic* traffic, int events) {
  DEBUG("traffic:%p, event:%d\n", traffic, events);
  if ((events & EPOLLERR) || (events & EPOLLHUP)) {
    return -1;
  }
  if (events & EPOLLIN) {
    Buffer* buffer = traffic->input;
    while (buffer->size < FULL_SIZE) {
      int remain = FULL_SIZE - buffer->size;
      char buf[remain];
      int len = recv(traffic->fd, buf, remain, 0);
      DEBUG("recv traffic: %p, fd: %d, len:%d, buffer->size:%d, buffer:%.*s\n", traffic, traffic->fd, len, buffer->size, buffer->size, buffer->data);
      if (len == 0) {
        return -1;
      } else if (len > 0) {
        bufferAdd(buffer, buf, len);
        if (traffic->readyQueueIt != NULL) {
          traffic->readyQueueIt = listAdd(contextGet()->readyTrafficList, traffic);
          tunnelOnTrafficReadable();
        }
      } else if (!isGoodCode()) {
        return -1;
      }
    }
    if (buffer->size >= FULL_SIZE) {
      updateEvent(traffic->fd, &traffic->ev, 0, EPOLLIN);
    }
  }
  if (events & EPOLLOUT) {
    Buffer* buffer = traffic->output;
    bool writable = true;
    trafficHandleFrame(traffic);
    while (writable) {
      if (buffer->size == 0) {
        if (traffic->closedByTunnel) {
          return -1; // connection complete
        } else {
          updateEvent(traffic->fd, &traffic->ev, 0, EPOLLOUT);
        }
        break;
      }
      int len = send(traffic->fd, buffer->data, buffer->size, MSG_NOSIGNAL);
      if (len < 0 && !isGoodCode()) {
        return -1;
      }
      if (len < buffer->size) {
        writable = false;
      }
      if (len > 0) {
        bufferPopFront(buffer, len);
      }
      trafficHandleFrame(traffic);
    }
  }
  return 0;
}

int
trafficHandleFrame(Traffic* traffic) {
  struct Context* context = contextGet();
  Frame* frame = context->frame;
  Tunnel* tunnel = context->tunnel;
  Buffer* buffer = tunnel->input;
  do {
    if (frame->state == STATE_NONE) {
      int decodeSize = frameDecode(frame, buffer->data, buffer->size);
      if (decodeSize <= 0) {
        return decodeSize;
      }
      printf("frame.state:%d, cid:%d, message:%.*s\n", frame->state, frame->cid,
         frame->message->size, frame->message->data);
      bufferPopFront(buffer, decodeSize);
      tunnelOnTrafficWritable(tunnel);
    }
    if (frame->state == STATE_CONNECT) {
      char* ip = "127.0.0.1";
      int port = 1232; // TODO
      DEBUG("try to create traffic, ip:%s, port:%d\n", ip, port);
      trafficConnect(ip, port, frame->cid); // TODO
      frame->state = STATE_NONE;
      continue;
    }
    if (traffic == NULL) {
      Traffic pattern;
      pattern.cid = frame->cid;
      Iterator* it = listGet(context->trafficList, &pattern, trafficMatch);
      if (it == NULL) {
        WARN("no traffic found: %d\n", frame->cid);
        continue;
      }
      traffic = it->data;
    }
    if (traffic->cid != frame->cid) {
      return 0;
    }
    if (frame->state == STATE_CLOSE) {
      trafficTunnelClose(traffic);
    } else if (frame->state == STATE_DATA) {
      if (traffic->output->size < FULL_SIZE) {
        bufferAdd(traffic->output, frame->message->data, frame->message->size);
      } else {
        return 0;
      }
    }
    frame->state = STATE_NONE;
  } while (frame->state == STATE_NONE);
}

void
trafficDataToTunnel(Tunnel* tunnel) {
  Buffer* buffer = tunnel->output;
  struct Context* context = contextGet();
  while (buffer->size < FULL_SIZE) {
    Iterator* first = context->readyTrafficList->first;
    if (first == NULL) {
      break;
    }
    Traffic* traffic = (Traffic*) first->data;
    iteratorRemove(first);
    traffic->readyQueueIt = NULL;
    if (traffic->creating) {
      frameEncodeAppend(traffic->cid, STATE_OK, NULL, 0, buffer);
    }
    if (traffic->input->size > 0) {
      frameEncodeAppend(
          traffic->cid,
          STATE_DATA,
          traffic->input->data,
          traffic->input->size,
          buffer);
    }
    if (traffic->closedBySystem) {
      frameEncodeAppend(traffic->cid, STATE_CLOSE, NULL, 0, buffer);
      trafficTunnelClose(traffic);
    } else {
      updateEvent(traffic->fd, &traffic->ev, EPOLLIN, 0);
    }
  }
}

void
trafficSystemClose(Traffic* traffic) {
  if (!traffic->closedBySystem) {
    traffic->closedBySystem = true;
    close(traffic->fd);
    if (traffic->trafficListIt) {
      iteratorRemove(traffic->trafficListIt);
      traffic->trafficListIt = NULL;
    }
  }
  struct Context* context = contextGet();
  if (!traffic->closedByTunnel) {
    if (!traffic->readyQueueIt) {
      traffic->readyQueueIt = listAdd(context->readyTrafficList, traffic);
    }
  } else {
    if (!traffic->recycleQueueIt) {
      traffic->recycleQueueIt = listAdd(context->readyTrafficList, traffic);
    }
  }
}

void
trafficTunnelClose(Traffic* traffic) {
  if (!traffic->closedByTunnel) {
    traffic->closedByTunnel = true;
    if (traffic->readyQueueIt) {
      iteratorRemove(traffic->readyQueueIt);
      traffic->readyQueueIt = NULL;
    }
  }
  if (traffic->closedBySystem) {
    if (!traffic->recycleQueueIt) {
      traffic->recycleQueueIt = listAdd(contextGet()->readyTrafficList, traffic);
    }
  }
}

void
trafficListOnTunnelBroken() {
  struct Context* context = contextGet();
  context->frame->state = STATE_NONE;
  Iterator* it = context->trafficList->first;
  while (it) {
    listAdd(context->recycleTrafficList, it->data);
    it = it->next;
  }
  listClear(context->trafficList);
  listClear(context->readyTrafficList);
}

void
trafficListCycleClear() {
  struct Context* context = contextGet();
  Iterator* it = context->recycleTrafficList->first;
  while (it) {
    Traffic* traffic = it->data;
    bufferRecycle(traffic->input);
    bufferRecycle(traffic->output);
    if (traffic->trafficListIt) {
      iteratorRemove(traffic->trafficListIt);
    }
    if (traffic->readyQueueIt) {
      iteratorRemove(traffic->readyQueueIt);
    }
    free(traffic);
    it = it->next;
  }
  listClear(context->recycleTrafficList);
}

int main(int argc, char** argv) {
  openlog("tunnel", LOG_CONS | LOG_PID, LOG_USER);
  struct Context* context = contextGet();
  for (int i = 1; i < argc; i += 2) {
    if (strcmp(argv[i], "--tunnelHost") == 0 && i + 1 < argc) {
      context->tunnelHost = argv[i + 1];
    } else if (strcmp(argv[i], "--tunnelPort") == 0 && i + 1 < argc) {
      context->tunnelPort = atoi(argv[i + 1]);
    } else if (strcmp(argv[i], "--verbose") == 0) {
      logEnabled = 1;
      i--; // in case i += 2;
    } else {
      int ret = strcmp(argv[i], "--help");
      if (ret != 0) {
        printf("\nunknown option: %s", argv[i]);
      }
      printf("usage: %s [options]\n\n", argv[0]);
      printf("  --tunnelHost domain.com   the server host, default 127.0.0.1\n");
      printf("  --tunnelPort num          the server port, default 8120\n");
      printf("  --verbose                 show detail log\n");
      printf("  --help                    show the usage then exit\n");
      printf("\n");
      printf("version 0.2, report bugs to SmartXiaoMing\n");
      exit(ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
    }
  }

  int epollWaitTime = 10000;
  int idleTime = 0;
  while (true) {
    if (context->tunnel == NULL) {
      context->tunnel = tunnelConnect(context->tunnelHost, context->tunnelPort);
      if (context->tunnel == NULL) {
        WARN("no valid tunnel server, waiting for 30 second...\n");
        sleep(30);
        continue;
      }
      WARN("success to connect server, tunnel:%p, host:%s, port:%d\n",
           context->tunnel, context->tunnelHost, context->tunnelPort);
    }
    const int MAX_EVENTS = 100;
    struct epoll_event events[MAX_EVENTS];
    int n = epoll_wait(context->epollFd, events, MAX_EVENTS, epollWaitTime);
    if (n == -1) {
      WARN("failed to epoll_wait: %d\n", n);
      return 1;
    }
    // sleep(1);
    for (int i = 0; i < n; i++) {
      if (events[i].data.ptr == context->tunnel) {
        if (tunnelOnEvent(context->tunnel, events[i].events) < 0) {
          tunnelRecycle(context->tunnel);
          context->tunnel = NULL;
        }
      } else {
        Traffic* traffic = (Traffic*) events[i].data.ptr;
        if (trafficOnEvent(traffic, events[i].events) < 0) {
          trafficSystemClose(traffic);
        }
      }
    }
    trafficListCycleClear();
  }
  return 0;
}

#endif //TCP_TUNNEL_TUNNEL_CLIENT_H
