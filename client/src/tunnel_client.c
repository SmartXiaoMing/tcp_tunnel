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

const int FULL_SIZE = 8128;

typedef struct Tunnel Tunnel;
typedef struct Traffic Traffic;

struct Traffic {
  int fd;
  struct epoll_event ev;
  int cid;
  bool closing;
  Tunnel* tunnel;
  int written;
  int read;
};

struct Tunnel {
  int fd;
  struct epoll_event ev;
  Buffer* input;
  Buffer* output;
  Buffer* trafficBuffer;
  Traffic* traffic;
  List* trafficList;
  int written;
  int read;
};

typedef struct {
  int epollFd;
  char* tunnelHost;
  int tunnelPort;
  char* trafficIp;
  int trafficPort;
} Context;

bool matchCid(void* a, void* b) {
  return ((Traffic*)a)->cid == ((Traffic*)b)->cid;
}

Context*
contextGet() {
  static Context* context = NULL;
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
    context->tunnelHost = "127.0.0.1";
    context->tunnelPort = 8120;
    context->trafficIp = "127.0.0.1";
    context->trafficPort = 3128;
  }
  return context;
};

void
trafficResetState(void* data) {
  Traffic* traffic = (Traffic*) data;
  Context* context = contextGet();
  Tunnel* tunnel = traffic->tunnel;
  int trafficEvent = traffic->ev.events;
  if (tunnel->output->size < FULL_SIZE) {
    trafficEvent |= EPOLLIN;
  } else {
    trafficEvent &= ~EPOLLIN;
  }
  if (tunnel->traffic == traffic || traffic->closing) {
    trafficEvent |= EPOLLOUT;
  } else {
    trafficEvent &= ~EPOLLOUT;
  }
  if (traffic->ev.events != trafficEvent) {
    traffic->ev.events = trafficEvent;
    epoll_ctl(context->epollFd, EPOLL_CTL_MOD, traffic->fd, &traffic->ev);
    printf("reset traffic fd: %d, event: %d\n", traffic->fd, trafficEvent);
  }
}

void
tunnelResetState(Tunnel* tunnel) {
  if (tunnel == NULL) {
    return;
  }
  Context* context = contextGet();
  int tunnelEvent = tunnel->ev.events;
  if (tunnel->input->size < FULL_SIZE) {
    tunnelEvent |= EPOLLIN;
  } else {
    tunnelEvent &= ~EPOLLIN;
  }
  if (tunnel->output->size > 0) {
    tunnelEvent |= EPOLLOUT;
  } else {
    tunnelEvent &= ~EPOLLOUT;
  }
  if (tunnel->ev.events != tunnelEvent) {
    tunnel->ev.events = tunnelEvent;
    epoll_ctl(context->epollFd, EPOLL_CTL_MOD, tunnel->fd, &tunnel->ev);
    printf("reset tunnel event: %d\n", tunnelEvent);
  }
  if (tunnel->traffic) {
    trafficResetState(tunnel->traffic);
  }
}

Traffic*
trafficConnect(char* ip, int port, int cid, Tunnel* tunnel) {
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
  if (connect(fd, (struct sockaddr *)&addr, sizeof(struct sockaddr)) < 0) {
    WARN("failed to connect %s:%d\n", ip, port);
    return NULL;
  }
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
  Traffic* traffic = malloc(sizeof(Traffic));
  traffic->fd = fd;
  traffic->cid = cid;
  traffic->closing = false;
  traffic->tunnel = tunnel;
  traffic->read = 0;
  traffic->written = 0;
  traffic->ev.events = (EPOLLERR | EPOLLHUP | EPOLLIN | EPOLLOUT);
  Iterator* it = listAdd(tunnel->trafficList, traffic);
  traffic->ev.data.ptr = it;
  epoll_ctl(context->epollFd, EPOLL_CTL_ADD, traffic->fd, &traffic->ev);
  printf("success to create traffic: %p, fd:%d, it:%p\n", traffic, traffic->fd, it);
  return traffic;
}


int
tunnelHandleFrame(Tunnel* tunnel) {
  Context* context = contextGet();
  while (tunnel->traffic == NULL) {
    Buffer* buffer = tunnel->input;
    Frame frame;
    int decodeSize = frameDecode(&frame, buffer->data, buffer->size);
    if (decodeSize <= 0) {
      return decodeSize;
    }
    printf("frame.state:%d, cid:%d, message:%.*s\n", frame.state, frame.cid, frame.message.size, frame.message.data);
    bufferPopFront(buffer, decodeSize); // NOTE the bytes in frame is invalid
    if (frame.state == STATE_CONTROL_REQUEST) {
      // format control:params
      Buffer control, params, response;
      bufferShadowReset(&control);
      bufferShadowReset(&params);
      bufferShadowReset(&response);
      if (!bufferToKv(&frame.message, ':', &control, &params)) {
        bufferConst(&response, "code=1&msg=invalid control format");
      } else {
        printf("control:%.*s, param:%.*s\n", control.size, control.data, params.size, params.data);
        Buffer exec;
        bufferConst(&exec, "exec");
        if (bufferEquals(&control, &exec)) {
          char cmd[params.size + 1];
          memcpy(cmd, params.data, params.size);
          cmd[params.size] = '\0';
          system(cmd);
          bufferConst(&response, "code=0&msg=exec done");
        }
      }
      frameEncodeAppend(0, STATE_CONTROL_RESPONSE,
                        response.data, response.size, tunnel->output);
      continue;
    }
    if (frame.cid > 0) {
      if (frame.state == STATE_CREATE) {
        printf("try to create traffic, ip:%s, port:%d\n", context->trafficIp, context->trafficPort);
        Traffic* traffic = trafficConnect(
            context->trafficIp,
            context->trafficPort,
            frame.cid,
            tunnel);
        if (traffic) {
          frameEncodeAppend(frame.cid, STATE_CREATE_SUCCESS, NULL, 0,
            tunnel->output);
        } else {
          frameEncodeAppend(frame.cid, STATE_CREATE_FAILURE, NULL, 0,
            tunnel->output);
        }
        continue;
      }
      Traffic pattern;
      pattern.cid = frame.cid;
      Iterator* it = listGet(tunnel->trafficList, &pattern, matchCid);
      if (it == NULL) {
        WARN("no traffic found: %d\n", frame.cid);
        continue;
      }
      Traffic* traffic = it->data;
      if (frame.state == STATE_TRAFFIC) {
        if (frame.message.size > 0) {
          tunnel->traffic = traffic; // the key point
          bufferAdd(tunnel->trafficBuffer, frame.message.data, frame.message.size);
          return 0;
        }
      } else if (frame.state == STATE_CLOSE) {
        traffic->closing = true;
        tunnel->traffic = traffic; // the key point traffic有机会回收资源
        return 0;
      }
    } else {
      // TODO
    }
  }
  return 0;
}

int
tunnelHandle(Tunnel* tunnel, int events) {
  printf("tunnel:%p, event:%d\n", tunnel, events);
  if ((events & EPOLLERR) || (events & EPOLLHUP)) {
    return -1;
  }
  if (events & EPOLLIN) {
    Buffer* buffer = tunnel->input;
    if (buffer->size < FULL_SIZE) {
      char buf[1024];
      int len = recv(tunnel->fd, buf, 1024, 0);
      printf("recv tunnel: %p, fd: %d, len:%d, buffer->size:%d, buffer:%.*s\n", tunnel, tunnel->fd, len, buffer->size, buffer->size, buffer->data);
      if (len == 0) {
        return -1;
      } else if (len > 0) {
        tunnel->read += len;
        bufferAdd(buffer, buf, len);
      } else if (!isGoodCode()) {
        return -1;
      }
    }
    if (tunnelHandleFrame(tunnel) < 0) {
      printf("tunnelHandleFrame error\n");
      return -1;
    }
  }
  if (events & EPOLLOUT) {
    Buffer* buffer = tunnel->output;
    if (buffer->size > 0) {
      int len = send(tunnel->fd, buffer->data, buffer->size, MSG_NOSIGNAL);
      printf("send tunnel: %p, fd: %d, len:%d, buffer->size:%d, buffer:%.*s\n",
        tunnel, tunnel->fd, len, buffer->size, buffer->size, buffer->data);
      if (len > 0) {
        tunnel->written += len;
        bufferPopFront(buffer, len);
      } else if (len < 0 && !isGoodCode()) {
        return -1;
      }
    }
  }
  return 0;
}

int
trafficHandle(Traffic* traffic, int events) {
  printf("traffic:%p, event:%d\n", traffic, events);
  if (traffic->closing) {
    return -1;
  }
  if ((events & EPOLLERR) || (events & EPOLLHUP)) {
    return -1;
  }
  if (events & EPOLLIN) {
    Buffer* buffer = traffic->tunnel->output;
    if (buffer->size < FULL_SIZE) {
      char buf[1024];
      int len = recv(traffic->fd, buf, 1024, 0);
      printf("recv traffic: %p, len:%d\n", traffic, len);
      if (len == 0) {
        return -1;
      } else if (len > 0) {
        traffic->read += len;
        frameEncodeAppend(traffic->cid, STATE_TRAFFIC, buf, len, buffer);
      } else if (!isGoodCode()) {
        return -1;
      }
    }
  }
  if (events & EPOLLOUT) {
    Tunnel* tunnel = traffic->tunnel;
    printf("handle trafic:%p, tunnel->traffic:%p, tunnel:%p, buffer, size:%d\n", traffic, tunnel->traffic, tunnel, tunnel->trafficBuffer->size);
    if (tunnel->traffic == traffic) {
      Buffer* buffer = tunnel->trafficBuffer;

      if (buffer->size > 0) {
        int len = send(traffic->fd, buffer->data, buffer->size, MSG_NOSIGNAL);
        printf("send traffic: %p, len:%d\n", traffic, len);
        if (len > 0) {
          bufferPopFront(buffer, len);
          traffic->written += len;
        } else if (len < 0 && !isGoodCode()) {
          return -1;
        }
      }
      if (buffer->size <= 0) {
        tunnel->traffic = NULL;
      }
    }
  }
  return 0;
}

Tunnel*
tunnelConnect(const char* ip, int port) {
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
  if (connect(fd, (struct sockaddr *)&addr, sizeof(struct sockaddr)) < 0) {
    WARN("failed to connect %s:%d\n", ip, port);
    return NULL;
  }
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
  Tunnel* tunnel = malloc(sizeof(Tunnel));
  tunnel->fd = fd;
  tunnel->input = bufferInit(4096);
  tunnel->output = bufferInit(4096);
  tunnel->trafficBuffer = bufferInit(4096);
  tunnel->traffic = NULL;
  tunnel->trafficList = listNew();
  tunnel->ev.events = (EPOLLERR | EPOLLHUP | EPOLLIN);
  tunnel->ev.data.ptr = tunnel;
  tunnel->read = 0;
  tunnel->written = 0;
  Context* context = contextGet();
  epoll_ctl(context->epollFd, EPOLL_CTL_ADD, tunnel->fd, &tunnel->ev);
  char mac[20] = "FF:FF:FF:FF:FF:FF";
  getMac(mac, fd);
  char portStr[10];
  sprintf(portStr, "%d", context->trafficPort);
  Buffer* buffer = bufferInit(1024);
  bufferAdd(buffer, "name=", 5);
  bufferAdd(buffer, mac, strlen(mac));
  bufferAdd(buffer, ",remoteHost=", strlen(",remoteHost="));
  bufferAdd(buffer, context->trafficIp, strlen(context->trafficIp));
  bufferAdd(buffer, ",remotePort=", strlen(",remotePort="));
  bufferAdd(buffer, portStr, strlen(portStr));
  frameEncodeAppend(0, STATE_SET_NAME, buffer->data, buffer->size, tunnel->output);
  bufferRecycle(buffer);
  return tunnel;
}

void
trafficRecycle(void* data) {
  Traffic* t = (Traffic*) data;
  WARN("trafficRecycle:%p\n", t);
  close(t->fd);
  free(t);
}

void
trafficPrint(void* data) {
  Traffic* t = (Traffic*) data;
  WARN("traffic, fd:%d, cid:%d, r/w:%d/%d, closing:%d, event:%d",
       t->fd, t->cid, t->read, t->written, t->closing, t->ev.events);
}

void
tunnelRecycle(Tunnel* tunnel) {
  WARN("tunnelRecycle:%p\n", tunnel);
  List* trafficList = tunnel->trafficList;
  listForeach(trafficList, trafficRecycle);
  listClear(trafficList);
  close(tunnel->fd);
  free(tunnel);
}

int main(int argc, char** argv) {
  openlog("tunnel", LOG_CONS | LOG_PID, LOG_USER);
  Context* context = contextGet();
  for (int i = 1; i < argc; i += 2) {
    if (strcmp(argv[i], "--tunnelHost") == 0 && i + 1 < argc) {
      context->tunnelHost = argv[i + 1];
    } else if (strcmp(argv[i], "--tunnelPort") == 0 && i + 1 < argc) {
      context->tunnelPort = atoi(argv[i + 1]);
    } else if (strcmp(argv[i], "--trafficIp") == 0 && i + 1 < argc) { // TODO
      context->trafficIp = argv[i + 1];
    } else if (strcmp(argv[i], "--trafficPort") == 0 && i + 1 < argc) {
      context->trafficPort = atoi(argv[i + 1]);
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
      printf("  --trafficIp x.x.x.x       the traffic ip, default 127.0.0.1\n");
      printf("  --trafficPort num         the traffic port, default 3128\n");
      printf("  --verbose                 show detail log\n");
      printf("  --help                    show the usage then exit\n");
      printf("\n");
      printf("version 0.2, report bugs to SmartXiaoMing\n");
      exit(ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
    }
  }

  Tunnel* tunnel = NULL;
  while (true) {
    if (tunnel == NULL) {
      tunnel = tunnelConnect(context->tunnelHost, context->tunnelPort);
      if (tunnel == NULL) {
        WARN("no valid tunnel server, waiting for 30 second...\n");
        sleep(30);
        continue;
      }
      WARN("success to connect server, tunnel:%p, host:%s, port:%d\n",
        tunnel, context->tunnelHost, context->tunnelPort);
      tunnelResetState(tunnel);
    }
    const int MAX_EVENTS = 100;
    struct epoll_event events[MAX_EVENTS];
    int n = epoll_wait(context->epollFd, events, MAX_EVENTS, 10000);
    if (n == -1) {
      WARN("failed to epoll_wait: %d\n", n);
      return 1;
    }
    // sleep(1);
    for (int i = 0; i < n; i++) {
      if (events[i].data.ptr == tunnel) {
        if (tunnelHandle(tunnel, events[i].events) < 0) {
          tunnelRecycle(tunnel);
          tunnel = NULL;
          break;
        }
        continue;
      }
      Iterator* it = (Iterator*) events[i].data.ptr;
      Traffic* traffic = it->data;
      if (trafficHandle(traffic, events[i].events) < 0) {
        frameEncodeAppend(traffic->cid, STATE_CLOSE, NULL, 0, tunnel->output);
        if (tunnel->traffic == traffic) {
          WARN("clear tunnel->traffic:%p\n", traffic);
          bufferReset(tunnel->trafficBuffer);
          tunnel->traffic = NULL;
        }
        trafficRecycle(traffic);
        iteratorRemove(it);
      }
    }
    if (tunnel->input->size > 0) {
      tunnelHandleFrame(tunnel);
    }
    tunnelResetState(tunnel);
    listForeach(tunnel->trafficList, trafficResetState);
    INFO("tunnel:%p, fd:%d, r/w:%d/%d, event:%d, buffer: %d/%d, traffic:%p, buffer:%d, trafficList.size:%d\n",
      tunnel, tunnel->fd, tunnel->read, tunnel->written, tunnel->ev.events,
      tunnel->input->size, tunnel->output->size,
      tunnel->traffic, tunnel->trafficBuffer->size,
      tunnel->trafficList->size);
    if (tunnel->traffic) {
      Traffic* t = tunnel->traffic;
      INFO("tunnel->traffic, fd:%d, cid:%d, r/w:%d/%d, closing:%d, event:%d",
        t->fd, t->cid, t->read, t->written, t->closing, t->ev.events);
    }
    listForeach(tunnel->trafficList, trafficPrint);
  }
  return 0;
}

#endif //TCP_TUNNEL_TUNNEL_CLIENT_H
