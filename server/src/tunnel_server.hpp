//
// Created by mabaiming on 16-12-20.
//

#ifndef TCP_TUNNEL_SERVER_H
#define TCP_TUNNEL_SERVER_H

#include "buffer.hpp"
#include "buffer_traffic.hpp"
#include "buffer_monitor.hpp"
#include "common.h"
#include "event_manager.h"
#include "frame.h"
#include "logger.h"
#include "tunnel_rule.hpp"

#include <set>

using namespace std;

class TunnelServer : public EventManager {
private:
  class TunnelBuffer {
    public:
    TunnelBuffer() : remotePort(0), createCount(0), failedCount(0){}
    TunnelBuffer(shared_ptr<Buffer> buffer_): buffer(buffer_),
      remotePort(0), createCount(), failedCount(0) {}
    shared_ptr<Buffer> buffer;
    string name;
    string remoteHost;
    int remotePort;
    int createCount;
    int failedCount;
    set<int> trafficIdSet;
  };
  typedef map<int, TrafficBuffer>::iterator TrafficIt;
  typedef map<int, TunnelBuffer>::iterator TunnelIt;
  typedef map<int, MonitorBuffer>::iterator MonitorIt;
  map<int, TrafficBuffer> trafficMap;
  map<int, TunnelBuffer> tunnelMap;
  map<int, MonitorBuffer> monitorMap;

  int tunnelSharedCount;
	TunnelRule tunnelRule;

public:
  void init(const Addr& tunnel, const vector<Addr>& trafficList,
    const Addr& monitor, int tunnelShared, const string& ruleFile) {
    tunnelSharedCount = tunnelShared;
    if (tunnelShared <= 0) {
      log_error << "tunnel shared count cannot <= 0, use default 1 instead";
      tunnelSharedCount = 1;
    }
	  if (!ruleFile.empty() && !tunnelRule.parseFile(ruleFile)) {
		  log_warn << "cannot to parse ruleFile: " << ruleFile;
	  }
    for (size_t i = 0; i < trafficList.size(); ++i) {
      const Addr& addr = trafficList[i];
      listen(addr.ip, addr.port, DefaultConnection, FD_TYPE_TRAFFIC);
    }
    listen(tunnel.ip, tunnel.port, DefaultConnection, FD_TYPE_TUNNEL);
    listen(monitor.ip, monitor.port, DefaultConnection, FD_TYPE_MONITOR);
  }

  void onBufferCreated(shared_ptr<Buffer> buffer, const ListenInfo& info) {
    if (info.type == FD_TYPE_TRAFFIC) {
      if (tunnelMap.empty()) {
        log_error << "no available connection to assign";
        buffer->close();
        return;
      }
      if (!chooseTunnel(buffer, info)) {
        buffer->close();
      }
    } else if (info.type == FD_TYPE_TUNNEL) {
      tunnelMap[buffer->getId()] = TunnelBuffer(buffer);
      log_info << "new tunnel: " << buffer->getId();
    } else if (info.type == FD_TYPE_MONITOR) {
      monitorMap[buffer->getId()] = MonitorBuffer(buffer);
    }
  }

  bool chooseTunnel(shared_ptr<Buffer> buffer, const ListenInfo& info) {
    // 当一个tunnel连接进来，根据规则，试图分配到一个traffic
    // 如果不能分配，就放到一个待连接池中，有一个永不用，有一个看时机
    // 当一个traffic断开或一个tunnel断开，重新分配
    TrafficBuffer trafficBuffer(buffer);
    trafficBuffer.state = TrafficBuffer::TRAFFIC_CREATING;
    TunnelIt bestIt = tunnelMap.end();
    TunnelIt it = tunnelMap.begin();
    for (; it != tunnelMap.end(); ++it) {
      if (it->second.trafficIdSet.size() >= tunnelSharedCount) {
        continue;
      }
      bool r = tunnelRule.match(
        it->second.name,
        it->second.remoteHost,
        it->second.remotePort,
        info.port
      );
      if (!r) {
        continue;
      }
      // 选择第一个合适的连接
      if (bestIt == tunnelMap.end()) {
        bestIt = it;
        if (bestIt->second.trafficIdSet.empty()) { // found
          break;
        }
        continue;
      }
      if (bestIt->second.trafficIdSet.size() > it->second.trafficIdSet.size()) {
        bestIt = it; // found
        break;
      }
    }
    if (bestIt != tunnelMap.end()) {
      trafficBuffer.tunnelId = bestIt->first;
      trafficBuffer.state = TrafficBuffer::TRAFFIC_CREATING;
      bestIt->second.trafficIdSet.insert(buffer->getId());
      trafficMap[buffer->getId()] = trafficBuffer;
      log_info << "traffic:" << buffer->getId()
         << " choose tunnel: " << bestIt->first;
      return true;
    }
    log_error << "no available connection to assign";
    return false;
  }

  bool handleTunnelData() {
    bool success = false;
    TunnelIt tunnelIt = tunnelMap.begin();
    while (tunnelIt != tunnelMap.end()) {
      TunnelBuffer& tunnelBuffer = tunnelIt->second;
      Frame frame;
      if (tunnelBuffer.buffer->isClosed()) {
        // 清理tunnel
        set<int>::iterator it = tunnelIt->second.trafficIdSet.begin();
        for(;it != tunnelIt->second.trafficIdSet.end();++it) {
          TrafficIt it2 = trafficMap.find(*it);
          if (it2 != trafficMap.end()) {
            it2->second.buffer->close();
            trafficMap.erase(it2);
          }
        }
        tunnelIt = tunnelMap.erase(tunnelIt);
        success = true;
        continue;
      }
      int n = 0;
      while ((n = tunnelBuffer.buffer->readFrame(frame)) > 0) {
        log_debug << "recv from client: " << tunnelBuffer.name
	        << ", cid: " << frame.cid
          << ", state: " << frame.getState()
          << ", message.size: " << frame.message.size();
        if (frame.state == Frame::STATE_SET_NAME) {
          map<string, string> kv;
          parseKVList(kv, frame.message);
          map<string, string>::iterator kvIt = kv.begin();
          for (; kvIt != kv.end(); ++kvIt) {
            if (kvIt->first == "name") {
              tunnelBuffer.name = kvIt->second;
            } else if (kvIt->first == "remoteHost") {
              tunnelBuffer.remoteHost = kvIt->second;
            } else if (kvIt->first == "remotePort") {
              tunnelBuffer.remotePort = stringToInt(kvIt->second);
            }
          }
          tunnelBuffer.buffer->popRead(n);
          success = true;
          continue;
        }
	      if (frame.state == Frame::STATE_HEARTBEAT) {
		      tunnelBuffer.buffer->popRead(n);
		      success = true;
		      continue;
	      }
        TrafficIt trafficIt = trafficMap.find(frame.cid);
        if (trafficIt != trafficMap.end()) {
          shared_ptr<Buffer> trafficBuffer = trafficIt->second.buffer;
          if (trafficBuffer->writableSize() == -1
              || frame.state == Frame::STATE_CLOSE
              || frame.state == Frame::STATE_CREATE_FAILURE) {
            if (frame.state == Frame::STATE_CREATE_FAILURE) {
              tunnelIt->second.failedCount++;
            }
            tunnelIt->second.trafficIdSet.erase(trafficBuffer->getId());
            trafficBuffer->close();
            trafficMap.erase(trafficIt);
          } else if (frame.state == Frame::STATE_TRAFFIC) {
            int s = trafficBuffer->write(frame.message);
            if (s == 0) {
              break;
            }
            log_debug << "send to traffic: " << trafficBuffer->getAddr()
              << ", cid: " << frame.cid
              << ", state: " << frame.getState()
              << ", message.size: " << frame.message.size();
          } else if (frame.state == Frame::STATE_CONTROL_RESPONSE) {
            cout << "control response, id: " << tunnelIt->first 
              << ", result: " << frame.message << endl;
          } else {
            log_warn << "ignore state: " << (int) frame.state;
          }
        } else {
	        log_warn << "ignore state: " << (int) frame.state;
        }
        tunnelBuffer.buffer->popRead(n);
        success = true;
      }
      if (n < 0) { // bad frame
        // 清理tunnel
        set<int>::iterator it = tunnelIt->second.trafficIdSet.begin();
        for(;it != tunnelIt->second.trafficIdSet.end();++it) {
          TrafficIt it2 = trafficMap.find(*it);
          if (it2 != trafficMap.end()) {
            it2->second.buffer->close();
            trafficMap.erase(it2);
          }
        }
        tunnelIt = tunnelMap.erase(tunnelIt);
        success = true;
        continue;
      }
      ++tunnelIt;
    }
    return success;
  }

  bool handleTrafficData() {
    bool success = false;
    TrafficIt it = trafficMap.begin();
    while (it != trafficMap.end()) {
      int cid = it->first;
      TrafficBuffer& trafficBuffer = it->second;
      TunnelIt tunnelIt = tunnelMap.find(trafficBuffer.tunnelId);
      if (tunnelIt == tunnelMap.end()) {
        trafficBuffer.buffer->close();
        it = trafficMap.erase(it);
        success = true;
        continue;
      }
      TunnelBuffer& tunnelBuffer = tunnelIt->second;
      if (tunnelBuffer.buffer->isClosed()) {
        tunnelIt->second.trafficIdSet.erase(trafficBuffer.buffer->getId());
        trafficBuffer.buffer->close();
        it = trafficMap.erase(it);
        success = true;
        continue;
      }
      // 下面这个if不要优化掉，可以帮助理解逻辑
      // 对于read=0断开，直到缓冲区无数据数据，才有isClosed=true
      if (trafficBuffer.buffer->isClosed()
        && trafficBuffer.state != TrafficBuffer::TRAFFIC_CLOSING) {
        trafficBuffer.state = TrafficBuffer::TRAFFIC_CLOSING;
        success = true;
      }
      if (trafficBuffer.state == TrafficBuffer::TRAFFIC_CREATING) {
        if (tunnelBuffer.buffer->writableSize() < Frame::HeadLength) {
          ++it;
          continue;
        }
        Frame frame;
        frame.cid = cid;
        frame.state = Frame::STATE_CREATE;
        frame.message = "";
        tunnelBuffer.buffer->writeFrame(frame);
        trafficBuffer.state = TrafficBuffer::TRAFFIC_OK;
        tunnelIt->second.createCount++;
        success = true;
        log_debug << "send to client: " << tunnelBuffer.buffer->getAddr()
	        << ", cid: " << frame.cid
          << ", state: " << frame.getState()
          << ", message.size: " << frame.message.size();
      }
      if (trafficBuffer.state == TrafficBuffer::TRAFFIC_OK) {
        int n  = 0;
        while((n = trafficBuffer.buffer->readableSize()) > 0) {
          int maxWriteSize
            = tunnelBuffer.buffer->writableSize() - Frame::HeadLength;
          if (maxWriteSize <= 0) {
            break;
          }
          string result;
          int readSize = trafficBuffer.buffer->read(result, maxWriteSize);
          Frame frame;
          frame.cid = cid;
          frame.state = Frame::STATE_TRAFFIC;
          frame.message = result;
          tunnelBuffer.buffer->writeFrame(frame);
          log_debug << "send to client: " << tunnelBuffer.buffer->getAddr()
	          << ", cid: " << frame.cid
            << ", state: " << frame.getState()
            << ", message.size: " << frame.message.size();
          trafficBuffer.buffer->popRead(readSize);
          success = true;
        }
        if (n == -1) {
          trafficBuffer.state = TrafficBuffer::TRAFFIC_CLOSING;
        }
      }
      if (trafficBuffer.state == TrafficBuffer::TRAFFIC_CLOSING) {
        if (tunnelBuffer.buffer->writableSize() >= Frame::HeadLength) {
          Frame frame;
          frame.cid = cid;
          frame.state = Frame::STATE_CLOSE;
          frame.message = "";
          tunnelBuffer.buffer->writeFrame(frame);
          log_debug << "send to client: "  << tunnelBuffer.buffer->getAddr()
	          << ", cid: " << frame.cid
            << ", state: " << frame.getState()
            << ", message.size: " << frame.message.size();
          tunnelIt->second.trafficIdSet.erase(trafficBuffer.buffer->getId());
          trafficBuffer.buffer->close();
          it = trafficMap.erase(it);
          success = true;
          continue;
        }
      }
      ++it;
    }
    return success;
  }

  bool handleMonitorData() {
    bool success = false;
    MonitorIt it = monitorMap.begin();
    while (it != monitorMap.end()) {
      MonitorBuffer& monitorBuffer = it->second;
      if (monitorBuffer.buffer->readableSize() == -1) {
        monitorBuffer.buffer->close();
        it = monitorMap.erase(it);
        continue;
      }
      Frame frame;
      int n = 0;
      while ((n = monitorBuffer.buffer->readFrame(frame)) > 0) {
        log_debug << "recv from monitor, cid: " << frame.cid
           << ", state: " << frame.getState()
           << ", message.size: " << frame.message.size();
        if (frame.state != Frame::STATE_MONITOR_REQUEST) {
          monitorBuffer.buffer->popRead(n);
          success = true;
          continue;
        }
        // control:params
        int p = frame.message.find(':');
        string ctrl, params;
        if (p == string::npos) {
          ctrl = frame.message;
        } else {
          ctrl = frame.message.substr(0, p);
          params = frame.message.substr(p + 1);
        }
        if (ctrl == "list") {
          if (it->second.sendBuffer.size() >= 102400) {
            // blocked and break
            break;
          }
          string result;
          result.resize(30 + tunnelMap.size() * 22 + trafficMap.size() * 35);
          result.append("tunnelSize\t");
          result.append(intToString(tunnelMap.size()));
          result.append("\n");
          TunnelIt it1 = tunnelMap.begin();
          for (; it1 != tunnelMap.end(); ++it1) {
            shared_ptr<Buffer>& buffer = it1->second.buffer;
            result.append(intToString(buffer->getId())).append("\t");
            result.append(buffer->getAddr()).append("(");
            result.append(it1->second.name).append(")\t");
            result.append(buffer->isClosed() ? "CLOSED\t" :"OK\t");
            result.append(formatTime(buffer->getTs())).append("\t");
            result.append(intToString(buffer->getInputSize())).append("\t");
            result.append(intToString(buffer->getOutputSize())).append("\t");
            result.append(intToString(it1->second.createCount)).append("/");
            result.append(intToString(it1->second.failedCount)).append("\t");
            result.append(intToString(it1->second.trafficIdSet.size()));
            result.append(" -> ");
            result.append(it1->second.remoteHost).append(":");
            result.append(intToString(it1->second.remotePort)).append("\n");
          }
          result.append("trafficSize\t");
          result.append(intToString(trafficMap.size()));
          result.append("\n");
          TrafficIt it2 = trafficMap.begin();
          for (; it2 != trafficMap.end(); ++it2) {
            shared_ptr<Buffer>& buffer = it2->second.buffer;
            result.append(intToString(buffer->getId())).append("\t");
            result.append(buffer->getAddr()).append("\t");
            result.append(buffer->isClosed() ? "CLOSED\t" :"OK\t");
            result.append(formatTime(buffer->getTs())).append("\t");
            result.append(intToString(buffer->getInputSize())).append("\t");
            result.append(intToString(buffer->getOutputSize())).append("\t");
            result.append("\t->\t");
            int tunnelId = it2->second.tunnelId;
            TunnelIt it3 = tunnelMap.find(tunnelId);
            if (it3 == tunnelMap.end()) {
              result.append("invalid\n");
            } else {
              result.append(it3->second.buffer->getAddr()).append("(");
              result.append(it3->second.name).append(")\n");
            }
          }
          it->second.sendBuffer.append(result);
        }
        if (ctrl == "remote") {
          map<string, string> input;
          parseKVQuery(input, params);
          string idStr = input["id"];
          int id = stringToInt(idStr);
          TunnelIt it = tunnelMap.find(id);
          if (it != tunnelMap.end()) {
            input.erase("id");
            string query = makeQuery(input);
            Frame frame;
            frame.state = Frame::STATE_CONTROL_REQUEST;
            frame.message = query;
            it->second.buffer->writeFrame(frame);
          }
        }
        monitorBuffer.buffer->popRead(n);
        success = true;
        continue;
      }
      if (n < 0) { // bad frame
        monitorBuffer.buffer->close();
        it = monitorMap.erase(it);
        continue;
      }

      int writeSize = monitorBuffer.buffer->writableSize() - Frame::HeadLength;
      if (monitorBuffer.sendBuffer.size() > 0 && writeSize > 0) {
        success = true;
        if (writeSize >= monitorBuffer.sendBuffer.size() + Frame::HeadLength) {
          Frame frame1;
          frame1.cid = frame.cid;
          frame1.state = Frame::STATE_MONITOR_RESPONSE;
          frame1.message = monitorBuffer.sendBuffer;
          monitorBuffer.buffer->writeFrame(frame1);
          log_debug << "send to monitor: " << monitorBuffer.buffer->getAddr()
	           << ", cid: " << frame1.cid
             << ", state: " << frame1.getState()
             << ", message.size: " << frame1.message.size();
          monitorBuffer.buffer->close();
          monitorBuffer.sendBuffer.clear();
          it = monitorMap.erase(it);
          continue;
        } else {
          Frame frame1;
          frame1.cid = frame.cid;
          frame1.state = Frame::STATE_MONITOR_RESPONSE;
          frame1.message = monitorBuffer.sendBuffer.substr(0, writeSize);
          monitorBuffer.buffer->writeFrame(frame1);
	        log_debug << "send to monitor: " << monitorBuffer.buffer->getAddr()
            << ", cid: " << frame1.cid
            << ", state: " << frame1.getState()
            << ", message.size: " << frame1.message.size();
          monitorBuffer.sendBuffer.assign(
            monitorBuffer.sendBuffer.begin() + writeSize,
            monitorBuffer.sendBuffer.end()
          );
        }
      }
      ++it;
    }
    return success;
  }

  bool exchangeData() {
    bool r1 = handleMonitorData();
    bool r2 = handleTunnelData();
    bool r3 = handleTrafficData();
    return r1 || r2 || r3;
  }

  int idle() {
    return 0;
  }
};

#endif //TCP_TUNNEL_SERVER_H
