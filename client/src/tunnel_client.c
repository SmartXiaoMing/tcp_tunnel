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
  char ip[30];
  int port;
  Buffer* input;
  Buffer* output;
  uint8_t addr[6];
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
  Buffer* secret;
  Buffer* password;
  int passwordSize;
} Context;

Context* contextGet();
void updateEvent(int fd, struct epoll_event* ev, int toAdd, int toDelete);
Tunnel* tunnelConnect(const char* host, int port);
int tunnelOnEvent(Tunnel* tunnel, int events);
void tunnelRecycle(Tunnel* tunnel);
void tunnelOnTrafficWritable();
void tunnelOnTrafficReadable();
bool trafficMatch(void* a, void* b);
void trafficConnect(Frame* frame);
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
    context->group = bufferCopy("sample");
    context->secret = bufferCopy("123456");
    context->password = NULL;
    context->passwordSize = 8;
    context->frame = frameInit();
    srand(time(0));
  }
  return context;
};

void
contextResetPassword() {
  Context* context = contextGet();
  bufferRecycle(context->password);
  context->password = bufferInit(context->passwordSize);
  char pwd[context->passwordSize];
  for (int i = 0; i < context->passwordSize; ++i) {
    pwd[i] = rand() % 256;
  }
  bufferAdd(context->password, pwd, context->passwordSize);
}


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
    ev->events = newEvent;
    epoll_ctl(contextGet()->epollFd, EPOLL_CTL_MOD, fd, ev);
  }
}

Tunnel*
tunnelConnect(const char* host, int port) {
  DEBUG("tunnelConnect, host:%s, port:%d", host, port);
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
  tunnel->ev.events = (EPOLLERR | EPOLLHUP | EPOLLIN | EPOLLOUT);
  tunnel->ev.data.ptr = tunnel;
  Context* context = contextGet();
  contextResetPassword();
  epoll_ctl(context->epollFd, EPOLL_CTL_ADD, tunnel->fd, &tunnel->ev);
  char mac[20] = "FF:FF:FF:FF:FF:FF";
  getMac(mac, fd);
  Buffer* buffer = bufferInit(1024);
  bufferAdd(buffer, "name=", 5);
  bufferAdd(buffer, mac, strlen(mac));
  bufferAdd(buffer, "&group=", 7);
  bufferAdd(buffer, context->group->data, context->group->size);
  Buffer* passwordCopy
      = bufferCloneFrom(context->password->size, context->password);
  bufferXor(passwordCopy, context->secret);
  char passwordText[passwordCopy->size * 2 + 1];
  DEBUG("passwordCopy->size:%d\n", passwordCopy->size);
  int passwordTextSize = bufferToHexStr(passwordCopy, passwordText);
  bufferAdd(buffer, "&key=", 5);
  bufferAdd(buffer, passwordText, passwordTextSize);
  bufferRecycle(passwordCopy);
  frameEncodeAppend(STATE_LOGIN, NULL, buffer->data, buffer->size, tunnel->output);
  bufferRecycle(buffer);
  return tunnel;
}

int
tunnelOnEvent(Tunnel* tunnel, int events) {
  DEBUG("tunnelOnEvent, event:%d", events);
  if ((events & EPOLLERR) || (events & EPOLLHUP)) {
    return -1;
  }
  if (events & EPOLLIN) {
    Buffer* buffer = tunnel->input;
    while (buffer->size < FULL_SIZE) {
      int remain = FULL_SIZE - buffer->size;
      char buf[remain];
      int len = recv(tunnel->fd, buf, remain, 0);
      DEBUG("tunnelOnEvent recv size:%d, buffer remain:%d", len, remain);
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
      DEBUG("tunnelOnEvent send size:%d, buffer.size:%d", len, buffer->size);
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
  DEBUG("tunnelRecycle:%p\n", tunnel);
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
  Traffic* t1 = a;
  Traffic* t2 = b;
  for (int i = 0; i < sizeof(t1->addr); ++i) {
    if (t1->addr[i] != t2->addr[i]) {
      return false;
    }
  }
  return true;
}

void
trafficConnect(Frame* frame) {
  DEBUG("enter trafficConnect");
  Context* context = contextGet();
  Traffic* traffic = malloc(sizeof(Traffic));
  traffic->ev.data.ptr = traffic;
  traffic->ev.events = (EPOLLERR | EPOLLHUP | EPOLLIN | EPOLLOUT);
  traffic->fd = 0;
  traffic->input = bufferInit(4096);
  traffic->output = bufferInit(4096);
  memcpy(traffic->addr, frame->addr, sizeof(frame->addr));
  traffic->closedByTunnel = false;
  traffic->closedBySystem = false;
  traffic->creating = false;
  traffic->trafficListIt = listAdd(context->trafficList, traffic);
  traffic->recycleQueueIt = NULL;
  Buffer ipBuffer, portBuffer;
  if (!bufferToKv(frame->message, ':', &ipBuffer, &portBuffer)) {
    ERROR("failed to parse trafficConfig:%.*s",
      frame->message->size, frame->message->data);
    return;
  }
  char ip[ipBuffer.size + 1];
  bufferToStr(&ipBuffer, traffic->ip);
  traffic->port = bufferToInt(&portBuffer);
  DEBUG("success to parse trafficConfig:%.*s",
    frame->message->size, frame->message->data);
  DEBUG("try to trafficConnect %s:%d\n", traffic->ip, traffic->port);
  traffic->fd = socket(PF_INET, SOCK_STREAM, 0);
  if (traffic->fd < 0) {
    ERROR("failed to socket: %d\n", traffic->fd);
    traffic->closedBySystem = true;
    traffic->creating = false;
    return;
  }
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr(traffic->ip);
  addr.sin_port = htons(traffic->port);
  if (connect(traffic->fd, (struct sockaddr *)&addr, sizeof(struct sockaddr)) < 0) {
    traffic->closedBySystem = true;
    traffic->creating = false;
    ERROR("failed to trafficConnect %s:%d\n", traffic->ip, traffic->port);
  } else {
    traffic->closedBySystem = false;
    traffic->creating = true;;
    fcntl(traffic->fd, F_SETFL, fcntl(traffic->fd, F_GETFL, 0) | O_NONBLOCK);
    epoll_ctl(context->epollFd, EPOLL_CTL_ADD, traffic->fd, &traffic->ev);
    DEBUG("success to trafficConnect %s:%d\n", traffic->ip, traffic->port);
  }
  traffic->readyQueueIt = listAdd(context->readyTrafficList, traffic);
}

int
trafficOnEvent(Traffic* traffic, int events) {
  DEBUG("trafficOnEvent for %s:%d, event:%d",
    traffic->ip, traffic->port, events);
  if ((events & EPOLLERR) || (events & EPOLLHUP)) {
    return -1;
  }
  if (events & EPOLLIN) {
    Buffer* buffer = traffic->input;
    while (buffer->size < FULL_SIZE) {
      int remain = FULL_SIZE - buffer->size;
      char buf[remain];
      int len = recv(traffic->fd, buf, remain, 0);
      if (len == 0) {
        DEBUG("trafficOnEvent for %s:%d, recv close",
          traffic->ip, traffic->port);
        return -1;
      } else if (len > 0) {
        DEBUG("trafficOnEvent for %s:%d, recv size:%d, remain:%d",
              traffic->ip, traffic->port, len, remain);
        bufferAdd(buffer, buf, len);
        if (traffic->readyQueueIt == NULL) {
          DEBUG("traffic->readyQueueIt:%p", traffic->readyQueueIt);
          traffic->readyQueueIt
            = listAdd(contextGet()->readyTrafficList, traffic);
          tunnelOnTrafficReadable();
        }
        tunnelOnTrafficReadable(); // TODO ?
      } else if (len < 0) {
        if (!isGoodCode()) {
          DEBUG("trafficOnEvent for %s:%d, recv error",
            traffic->ip, traffic->port);
          return -1;
        }
        break;
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
        DEBUG("trafficOnEvent for %s:%d, send error",
          traffic->ip, traffic->port);
        return -1;
      }
      if (len < buffer->size) {
        writable = false;
      }
      if (len > 0) {
        DEBUG("trafficOnEvent for %s:%d, send size:%d, buffer size:%d",
              traffic->ip, traffic->port, len, buffer->size);
        bufferPopFront(buffer, len);
      }
      trafficHandleFrame(traffic);
    }
  }
  return 0;
}

int
trafficHandleFrame(Traffic* traffic) {
  Context* context = contextGet();
  Frame* frame = context->frame;
  Tunnel* tunnel = context->tunnel;
  Buffer* buffer = tunnel->input;
  do {
    if (frame->state == STATE_NONE) {
      int decodeSize = frameDecode(frame, buffer->data, buffer->size);
      if (decodeSize <= 0) {
        return decodeSize;
      }
      bufferPopFront(buffer, decodeSize);
      tunnelOnTrafficWritable(tunnel);
    }
    if (frame->message->size > 0) {
      bufferXor(frame->message, context->password);
    }
    DEBUG("tunnel recv frame(addr = %p, state = %s, data[%d] = %.*s)",
      frame->addr, stateToStr(frame->state), frame->message->size,
      frame->message->size, frame->message->data);
    if (frame->state == STATE_CONNECT) {
      trafficConnect(frame);
      frame->state = STATE_NONE;
      continue;
    }
    if (traffic == NULL) {
      Traffic pattern;
      memcpy(pattern.addr, frame->addr, sizeof(pattern.addr));
      Iterator* it = listGet(context->trafficList, &pattern, trafficMatch);
      if (it == NULL) {
        WARN("no traffic found: %p\n", frame->addr);
        continue;
      }
      traffic = it->data;
    }
    if (memcmp(traffic->addr, frame->addr, sizeof(frame->addr)) != 0) {
      return 0;
    }
    if (frame->state == STATE_CLOSE) {
      trafficTunnelClose(traffic);
    } else if (frame->state == STATE_DATA) {
      if (traffic->output->size < FULL_SIZE) {
        bufferAdd(traffic->output, frame->message->data, frame->message->size);
        updateEvent(traffic->fd, &traffic->ev, EPOLLOUT, 0);
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
      DEBUG("trafficDataToTunnel, readyList is emtpy");
      break;
    }
    Traffic* traffic = (Traffic*) first->data;
    iteratorRemove(first);
    traffic->readyQueueIt = NULL;
    if (traffic->creating) {
      DEBUG("trafficDataToTunnel, push frame(addr = %p, state = %s, data[0])",
        traffic->addr, stateToStr(STATE_OK));
      frameEncodeAppend(STATE_OK, traffic->addr, NULL, 0, buffer);
    }
    if (traffic->input->size > 0) {
      bufferXor(traffic->input, context->password); // encrypt
      DEBUG("trafficDataToTunnel, push frame(addr = %p, state = %s, data[%d] = %.*s)",
        traffic->addr, stateToStr(STATE_DATA), traffic->input->size,
        traffic->input->size, traffic->input->data);
      frameEncodeAppend(
          STATE_DATA,
          traffic->addr,
          traffic->input->data,
          traffic->input->size,
          buffer);
      bufferPopFront(traffic->input, traffic->input->size);
    }
    if (traffic->closedBySystem) {
      DEBUG("trafficDataToTunnel, push frame(addr = %p, state = %s, data[0])",
            traffic->addr, stateToStr(STATE_CLOSE));
      frameEncodeAppend(STATE_CLOSE, traffic->addr, NULL, 0, buffer);
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
    traffic->fd = 0;
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
    Traffic* traffic = it->data;
    listAdd(context->recycleTrafficList, traffic);
    traffic->trafficListIt = NULL;
    traffic->readyQueueIt = NULL;
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
    traffic->input = NULL;
    traffic->output = NULL;
    if (traffic->trafficListIt) {
      iteratorRemove(traffic->trafficListIt);
      traffic->trafficListIt = NULL;
    }
    if (traffic->readyQueueIt) {
      iteratorRemove(traffic->readyQueueIt);
      traffic->readyQueueIt = NULL;
    }
    if (traffic->fd) {
      close(traffic->fd);
    }
    free(traffic);
    it->data = NULL;
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
    } else if (strcmp(argv[i], "--group") == 0 && i + 1 < argc) {
      bufferRecycle(context->group);
      context->group = bufferCopy(argv[i + 1]);
    } else if (strcmp(argv[i], "--secret") == 0 && i + 1 < argc) {
      bufferRecycle(context->secret);
      context->secret = bufferCopy(argv[i + 1]);
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
      printf("  --group groupName         the group name, default sample\n");
      printf("  --secret groupSecret      the group secret, default 123456\n");
      printf("  --verbose                 show detail log\n");
      printf("  --help                    show the usage then exit\n");
      printf("\n");
      printf("version 0.2, report bugs to SmartXiaoMing\n");
      exit(ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
    }
  }

  int epollWaitTime = 3000;
  int idleTime = 0;
  while (true) {
    if (context->tunnel == NULL) {
      context->tunnel = tunnelConnect(context->tunnelHost, context->tunnelPort);
      if (context->tunnel == NULL) {
        WARN("no valid tunnel server, waiting for 30 second...\n");
        sleep(30);
        continue;
      }
      WARN("success to connect server %s:%d\n",
        context->tunnelHost, context->tunnelPort);
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
