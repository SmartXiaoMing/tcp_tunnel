//
// Created by mabaiming on 18-8-16.
//

#include "endpoint_client.h"
#include "endpoint_server.h"
#include "center_server.h"

void
CenterServer::prepare(int tunnelPort, const char* confFile) {
  tunnel_ = EndpointServer::create(EndpointClient::TYPE_TUNNEL, "0.0.0.0", tunnelPort);
  if (tunnel_ == NULL) {
    ERROR("failed  to prepare for tunnel server, port:%d\n", tunnelPort);
    exit(EXIT_FAILURE);
  }
  tunnel_->setCenter(this);
  FILE* file = fopen(confFile, "r");
  if (file == NULL) {
    ERROR("failed  to open config file:%s\n", confFile);
  }
  int localPort;
  int remoteIp;
  int remotePort;
  char group[];
  string name;
  char line[1024];
  while (fgets(line, 1023, file)) {
    if (line[0] == '#') {
      continue;
    }
    int localPort, remotePort;
    char group[100], name[100], remoteIp[30];
    if (sscanf(line, "%d %s %s %s %d", &localPort, group, name, remoteIp, &remotePort) != 5) {
      ERROR("invalid line:'%s' for file:%s\n", line, confFile);
      continue;
    }
    EndpointServer* trafficServer = EndpointServer::create(
        EndpointClient::TYPE_TRAFFIC, "0.0.0.0", localPort, group, name, remoteIp, remotePort);
    if (trafficServer == NULL) {
      ERROR("failed  to prepare for traffic server, port:%d\n", localPort);
      continue;
    }
    trafficServer->setCenter(this);
    trafficServerSet_->insert(trafficServer);
  }
}

int
CenterServer::getRemainBufferSizeFor(EndpointClient* endpoint) {
  if (endpoint->getType() == EndpointClient::TYPE_TRAFFIC) {
    map<EndpointClient*, EndpointClient*>::itertor it = tunnelClients_.find(endpoint);
    if (it == tunnelClients_.end()) {
      ERROR("tunnel not found for traffic client\n");
      return 0;
    }
    return it->second->getWriteBufferRemainSize() - FrameHeadSize;
  } else {
    map<int, pair<EndpointClient*, string>>::itertor it = trafficClients_.find(endpoint->getId());
    if (it == trafficClients_.end()) {
      ERROR("traffic not found for id:%d\n", endpoint->getId());
      return 0;
    }
    return BufferCapacity - it->second->second.size();
  }
}

void
CenterServer::appendDataToBufferFor(EndpointClient* endpoint, const char* data, int size) {
  if (endpoint->getType() == EndpointClient::TYPE_TRAFFIC) {
    map<EndpointClient*, EndpointClient*>::itertor it = tunnelClients_.find(endpoint);
    if (it == tunnelClients_.end()) {
      ERROR("tunnel not found for traffic client\n");
      return 0;
    }
    string buffer;
    Frame::encodeTo(buffer, STATE_DATA, endpoint->getId(), data, size);
    it->second->appendDataToWriteBuffer(buffer.data(), buffer.size());
    return it->second->getWriteBufferRemainSize() - FrameHeadSize;
  } else {
    map<int, pair<EndpointClient*, string>>::itertor it = trafficClients_.find(endpoint->getId());
    if (it == trafficClients_.end()) {
      ERROR("traffic not found for id:%d\n", endpoint->getId());
      return;
    }
    it->second->append(data, size);
    // TODO handle data
  }
}

void
CenterServer::notifyWritableFor(EndpointClient* endpoint) { // TODO
  if (endpoint->getType() == EndpointClient::TYPE_TRAFFIC) {
    map<EndpointClient*, EndpointClient*>::itertor it = tunnelClients_.find(endpoint);
    if (it == tunnelClients_.end()) {
      ERROR("tunnel not found for traffic client\n");
      return;
    }
    string buffer;
    Frame::encodeTo(buffer, STATE_DATA, endpoint->getId(), data, size);
    it->second->appendDataToWriteBuffer(buffer.data(), buffer.size());
    return it->second->getWriteBufferRemainSize() - FrameHeadSize;
  } else {
    map<int, pair<EndpointClient*, string>>::itertor it = trafficClients_.find(endpoint->getId());
    if (it == trafficClients_.end()) {
      ERROR("traffic not found for id:%d\n", endpoint->getId());
      return;
    }
    it->second->append(data, size);
    // TODO handle data
  }
}

void
CenterServer::notifyBrokenFor(EndpointClient* endpoint) {
  if (endpoint->getType() == EndpointClient::TYPE_TRAFFIC) {
    map<int, EndpointClient*>::iterator it = trafficClients_.find(endpoint->getId());
    if (it != clients_.end()) {
      string buffer;
      Frame::encodeTo(buffer, STATE_CLOSE, endpoint->getId(), NULL, 0);
      it->second->appendDataToWriteBuffer(buffer.data(), buffer.size());
    }
  } else {
    map<int, EndpointClient*>::iterator it = tunnelClients_.find(endpoint->getId());
    if (it != users_.end()) {
      it->second->setBroken();
      tunnelClients_.erase(it);
    }
  }
}

void CenterServer::notifyNewClient(EndpointClient* endpoint) {
  if (endpoint->getType() == EndpointClient::TYPE_TRAFFIC) {
    users_[endpoint->getId()] = endpoint;
  } else {
    clients_[endpoint->getId()] = endpoint;
  }
}
