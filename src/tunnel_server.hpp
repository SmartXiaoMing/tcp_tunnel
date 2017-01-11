//
// Created by mabaiming on 16-12-20.
//

#ifndef TCP_TUNNEL_SERVER_H
#define TCP_TUNNEL_SERVER_H

#include "buffer.hpp"
#include "buffer_traffic.hpp"
#include "buffer_monitor.hpp"
#include "event_manager.h"
#include "common.h"
#include "logger.h"
#include "frame.h"

#include <set>

using namespace std;

class TunnelServer : public EventManager {
  class TunnelBuffer {
    public:
    TunnelBuffer(){}
    TunnelBuffer(shared_ptr<Buffer> buffer_): buffer(buffer_) {}
    shared_ptr<Buffer> buffer;
    set<int> trafficIdSet;
  };



private:
  map<int, TrafficBuffer> trafficMap;
  map<int, TunnelBuffer> tunnelMap;
	map<int, MonitorBuffer> monitorMap;

public:
  void init(const Addr& tunnel, const Addr& traffic, const Addr& monitor) {
    listen(traffic.ip, traffic.port, DefaultConnection, FD_TYPE_TRAFFIC);
    listen(tunnel.ip, tunnel.port, DefaultConnection, FD_TYPE_TUNNEL);
	  listen(monitor.ip, monitor.port, DefaultConnection, FD_TYPE_MONITOR);
  }

  void onBufferCreated(shared_ptr<Buffer> buffer) {
	  if (buffer->getType() == FD_TYPE_TRAFFIC) {
      if (tunnelMap.empty()) {
	      log_error << "no available connection to assign";
        buffer->close();
        return;
      }
      TrafficBuffer trafficBuffer(buffer);
      trafficBuffer.state = TrafficBuffer::TRAFFIC_CREATING;
      int totalCount = 0;
      map<int, TunnelBuffer>::iterator it = tunnelMap.begin();
      for (; it != tunnelMap.end(); ++it) {
        totalCount += it->second.trafficIdSet.size();
      }
      int ceil = totalCount / tunnelMap.size() + 1;
      it = tunnelMap.begin();
      for (; it != tunnelMap.end(); ++it) {
				if (it->second.trafficIdSet.size() < ceil) {
				  // 选择第一个合适的连接
					trafficBuffer.tunnelId = it->first;
					trafficBuffer.state = TrafficBuffer::TRAFFIC_CREATING;
					it->second.trafficIdSet.insert(buffer->getId());
					trafficMap[buffer->getId()] = trafficBuffer;
					return;
				}
      }
		  log_error << "no available connection to assign";
      buffer->close();
		} else if (buffer->getType() == FD_TYPE_TUNNEL) {
		  tunnelMap[buffer->getId()] = TunnelBuffer(buffer);
		} else if (buffer->getType() == FD_TYPE_MONITOR) {
	    monitorMap[buffer->getId()] = MonitorBuffer(buffer);
    }
  }

  bool handleTunnelData() {
    bool success = false;
    map<int, TunnelBuffer>::iterator tunnelIt = tunnelMap.begin();
    while (tunnelIt != tunnelMap.end()) {
      shared_ptr<Buffer> tunnelBuffer = tunnelIt->second.buffer;
      Frame frame;
      int n = tunnelBuffer->readFrame(frame);
	    if (n == -1) {
		    // 清理tunnel
		    set<int>::iterator it = tunnelIt->second.trafficIdSet.begin();
		    for(;it != tunnelIt->second.trafficIdSet.end();++it) {
			    map<int, TrafficBuffer>::iterator it2 = trafficMap.find(*it);
			    if (it2 != trafficMap.end()) {
				    it2->second.buffer->close();
				    trafficMap.erase(it2);
			    }
		    }
		    tunnelIt = tunnelMap.erase(tunnelIt);
		    success = true;
		    continue;
	    }
	    while (n > 0) {
		    if (frame.state == Frame::STATE_SET_NAME) {
			    tunnelBuffer->setName(frame.message);
			    tunnelBuffer->popRead(n);
			    success = true;
			    n = tunnelBuffer->readFrame(frame);
			    continue;
		    }
		    map<int, TrafficBuffer>::iterator trafficIt = trafficMap.find(frame.cid);
		    if (trafficIt != trafficMap.end()) {
			    shared_ptr<Buffer> trafficBuffer = trafficIt->second.buffer;
			    if (trafficBuffer->writableSize() == -1) {
				    tunnelIt->second.trafficIdSet.erase(trafficBuffer->getId());
				    trafficBuffer->close();
				    trafficMap.erase(trafficIt);
			    } else if (frame.state == Frame::STATE_TRAFFIC) {
				    int s = trafficBuffer->write(frame.message);
				    if (s == 0) {
					    break;
				    }
			    } else if (frame.state == Frame::STATE_CLOSE) {
				    tunnelIt->second.trafficIdSet.erase(trafficBuffer->getId());
				    trafficBuffer->close();
				    trafficMap.erase(trafficIt);
			    } else {
				    log_warn << "ignore state: " << (int) frame.state;
			    }
		    }
		    tunnelBuffer->popRead(n);
		    success = true;
		    n = tunnelBuffer->readFrame(frame);
	    }
      ++tunnelIt;
    }
    return success;
  }

  bool handleTrafficData() {
	  bool success = false;
    map<int, TrafficBuffer>::iterator it = trafficMap.begin();
    while (it != trafficMap.end()) {
	    int cid = it->first;
	    TrafficBuffer& trafficBuffer = it->second;
      map<int, TunnelBuffer>::iterator tunnelIt
	      = tunnelMap.find(trafficBuffer.tunnelId);
      if (tunnelIt == tunnelMap.end()) {
	      trafficBuffer.buffer->close();
	      it = trafficMap.erase(it);
	      success = true;
	      continue;
      }
      if (tunnelIt->second.buffer->writableSizeForFrame() == -1) {
	      tunnelIt->second.trafficIdSet.erase(trafficBuffer.buffer->getId());
	      trafficBuffer.buffer->close();
	      it = trafficMap.erase(it);
	      success = true;
	      continue;
      }
			TunnelBuffer& tunnelBuffer = tunnelIt->second;
      if (trafficBuffer.state == TrafficBuffer::TRAFFIC_CREATING) {
	      int n = tunnelBuffer.buffer->writeFrame(cid, Frame::STATE_CREATE);
        if (n == 0) {
	        ++it;
	        continue;
        } else {
	        trafficBuffer.state = TrafficBuffer::TRAFFIC_OK;
	        success = true;
        }
      }
      if (trafficBuffer.state == TrafficBuffer::TRAFFIC_OK) {
	      int n = 0;
	      while((n = trafficBuffer.buffer->readableSize()) > 0) {
		      string result;
		      int maxWriteSize = tunnelBuffer.buffer->writableSizeForFrame();
		      if (maxWriteSize == 0) {
			      break;
		      }
		      int readSize = trafficBuffer.buffer->read(result, maxWriteSize);
		      tunnelBuffer.buffer->writeFrame(cid, result);
		      trafficBuffer.buffer->popRead(readSize);
		      success = true;
        }
	      if (n == -1) {
		      trafficBuffer.state = TrafficBuffer::TRAFFIC_CLOSING;
		      success = true;
	      }
      }
      if (trafficBuffer.state == TrafficBuffer::TRAFFIC_CLOSING) {
        int n = tunnelBuffer.buffer->writeFrame(cid, Frame::STATE_CLOSE);
        if (n > 0) {
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
		map<int, MonitorBuffer>::iterator it = monitorMap.begin();
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
				if (frame.state != Frame::STATE_MONITOR_REQUEST) {
					monitorBuffer.buffer->popRead(n);
					success = true;
					continue;
				}
				if (frame.message == "list") {
					if (it->second.sendBuffer.size() >= 102400) {
						// blocked and break
						break;
					}
					string result;
					result.resize(30 + tunnelMap.size() * 22 + trafficMap.size() * 35);
					result.append("tunnelSize\t");
					result.append(intToString(tunnelMap.size()));
					result.append("\n");
					map<int, TunnelBuffer>::iterator it1 = tunnelMap.begin();
					for (; it1 != tunnelMap.end(); ++it1) {
						result.append(it1->second.buffer->toString());
						result.append("\t");
						result.append(intToString(it1->second.trafficIdSet.size()));
						result.append("\n");
					}
					result.append("trafficSize\t");
					result.append(intToString(trafficMap.size()));
					result.append("\n");
					map<int, TrafficBuffer>::iterator it2 = trafficMap.begin();
					for (; it2 != trafficMap.end(); ++it2) {
						result.append(it2->second.buffer->toString()).append("\t->\t");
						int tunnelId = it2->second.tunnelId;
						map<int, TunnelBuffer>::iterator it3 = tunnelMap.find(tunnelId);
						if (it3 == tunnelMap.end()) {
							result.append("invalid\n");
						} else {
							result.append(it3->second.buffer->getName());
							result.append("\n");
						}
					}
					it->second.sendBuffer.append(result);
				}
				monitorBuffer.buffer->popRead(n);
				success = true;
				continue;
			}

			int writeSize = monitorBuffer.buffer->writableSizeForFrame();
			if (monitorBuffer.sendBuffer.size() > 0 && writeSize > 0) {
				success = true;
				if (writeSize >= monitorBuffer.sendBuffer.size()) {
					monitorBuffer.buffer->writeFrame(
						frame.cid,
            Frame::STATE_MONITOR_RESPONSE,
						monitorBuffer.sendBuffer
					);
					monitorBuffer.buffer->close();
					monitorBuffer.sendBuffer.clear();
					it = monitorMap.erase(it);
					continue;
				} else {
					string data = monitorBuffer.sendBuffer.substr(0, writeSize);
					monitorBuffer.buffer->writeFrame(
						frame.cid,
            Frame::STATE_MONITOR_RESPONSE,
						data
					);
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