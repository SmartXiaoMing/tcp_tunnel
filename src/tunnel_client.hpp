//
// Created by mabaiming on 16-12-20.
//

#ifndef TCP_TUNNEL_CLIENT_H
#define TCP_TUNNEL_CLIENT_H

#include "buffer.hpp"
#include "buffer_traffic.hpp"
#include "event_manager.h"
#include "common.h"
#include "logger.h"
#include "frame.h"
#include "buffer_monitor.hpp"

class TunnelClient : public EventManager {
private:
	typedef map<int, TrafficBuffer>::iterator TrafficIt;
	typedef map<int, MonitorBuffer>::iterator MonitorIt;
  map<int, TrafficBuffer> trafficMap;
	map<int, MonitorBuffer> monitorMap;
  shared_ptr<Buffer> tunnelBuffer;
  string tunnelServerList;
	Addr trafficAddr;

public:
  void init(const string& tunnel, const Addr& traffic, const Addr& monitor) {
    tunnelServerList = tunnel;
	  trafficAddr = traffic;
	  listen(monitor.ip, monitor.port, DefaultConnection, FD_TYPE_MONITOR);
	  reset();
  }

  void onBufferCreated(shared_ptr<Buffer> buffer) {
	  if (buffer->getType() == FD_TYPE_MONITOR) {
		  monitorMap[buffer->getId()] = MonitorBuffer(buffer);
	  }
  }

  void reset() {
    if (!trafficMap.empty()) {
      TrafficIt it = trafficMap.begin();
      for (; it != trafficMap.end();++it) {
	      it->second.buffer->close();
      }
      trafficMap.clear();
    }
	  while (true) {
		  Addr tunnel;
		  if (!chooseTunnelAddr(tunnel)) {
			  log_error << "no valid tunnel server, waiting for 30 second...";
			  sleep(30);
			  continue;
		  }
		  shared_ptr<Buffer> buffer = connect(tunnel.ip, tunnel.port, FD_TYPE_TUNNEL);
		  if (buffer.get() != NULL && !buffer->isClosed()) {
			  tunnelBuffer = buffer;
				tunnelBuffer->writeFrame(0, Frame::STATE_SET_NAME, buffer->getMac());
			  log_info << "use tunnel server: " << tunnel.ip << ":" << tunnel.port;
			  break;
		  } else {
			  log_error << "get tunnel server failed, waiting for 10 second...";
			  sleep(10);
		  }
	  }
  }

	bool chooseTunnelAddr(Addr& tunnel) {
	  vector<Addr> addrList;
		if (!parseAddressList(addrList, tunnelServerList)) {
			return false;
		}
		int i = rand() % addrList.size();
		tunnel = addrList[i];
		return true;
	}

  bool handleTunnelData() {
    if (tunnelBuffer.get() == NULL || tunnelBuffer->isClosed()) {
      reset();
      return true;
    }
    bool success = false;
  	Frame frame;
    int n = 0;
  	while ((n = tunnelBuffer->readFrame(frame)) > 0) {
      TrafficIt it = trafficMap.find(frame.cid);
      if (it == trafficMap.end()) {
        if (frame.state == Frame::STATE_CREATE) {
          shared_ptr<Buffer> buffer = connect(trafficAddr.ip, trafficAddr.port, FD_TYPE_TRAFFIC);
          TrafficBuffer trafficBuffer(buffer);
          if (buffer.get() == NULL || buffer->isClosed()) {
	          trafficBuffer.state = TrafficBuffer::TRAFFIC_CLOSING;
          } else {
	          trafficBuffer.state = TrafficBuffer::TRAFFIC_OK;
          }
          trafficMap[frame.cid] = trafficBuffer;
        }
        tunnelBuffer->popRead(n);
	      success = true;
      } else {
        TrafficBuffer& trafficBuffer = it->second;
		    if (frame.state == Frame::STATE_CLOSE) {
          trafficBuffer.buffer->close();
          trafficMap.erase(it);
          tunnelBuffer->popRead(n);
			    success = true;
        } else if (frame.state == Frame::STATE_TRAFFIC) {
          int s = trafficBuffer.buffer->write(frame.message);
          if (s == 0) {
            break; // block and break
          } else {
            tunnelBuffer->popRead(n);
	          success = true;
          }
        } else {
          log_warn << "ignore state: " << (int) frame.state;
          tunnelBuffer->popRead(n);
			    success = true;
        }
      }
    }
    return success;
  }

  int handleTrafficData() {
    if (tunnelBuffer.get() == NULL || tunnelBuffer->isClosed()) {
      reset();
      return 0;
    }
    bool success = false;
    TrafficIt it = trafficMap.begin();
    while (it != trafficMap.end()) {
      int cid = it->first;
      TrafficBuffer& trafficBuffer = it->second;
	    if (trafficBuffer.buffer->isClosed()
        && trafficBuffer.state != TrafficBuffer::TRAFFIC_CLOSING) {
		    trafficBuffer.state = TrafficBuffer::TRAFFIC_CLOSING;
	    }
      if (trafficBuffer.state == TrafficBuffer::TRAFFIC_OK) {
        int n = 0;
        while((n = trafficBuffer.buffer->readableSize()) > 0) {
          int maxWriteSize = tunnelBuffer->writableSizeForFrame();
          if (maxWriteSize == 0) {
            break;
          }
	        string result;
          int readSize = trafficBuffer.buffer->read(result, maxWriteSize);
          tunnelBuffer->writeFrame(cid, result);
          trafficBuffer.buffer->popRead(readSize);
	        success = true;
        }
        if (n == -1) {
          trafficBuffer.state = TrafficBuffer::TRAFFIC_CLOSING;
        }
      }
      if (trafficBuffer.state == TrafficBuffer::TRAFFIC_CLOSING) {
        int n = tunnelBuffer->writeFrame(cid, Frame::STATE_CLOSE);
        if (n > 0) {
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

	int handleMonitorData() {
		bool success = false;
		MonitorIt it = monitorMap.begin();
		while (it != monitorMap.end()) {
			MonitorBuffer& monitorBuffer = it->second;
			if (monitorBuffer.buffer->readableSize() == -1) {
				it = monitorMap.erase(it);
				success = true;
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
					result.append("tunnelSize\t1\n");
					result.append(tunnelBuffer->toString());
					result.append("\n");
					result.append("trafficSize\t");
					result.append(intToString(trafficMap.size()));
					result.append("\n");
					TrafficIt it2 = trafficMap.begin();
					for (; it2 != trafficMap.end(); ++it2) {
						result.append(it2->second.buffer->toString());
						result.append("\n");
					}
					it->second.sendBuffer.append(result);
				}
				monitorBuffer.buffer->popRead(n);
				success = true;
				continue;
			}

			int writeSize = monitorBuffer.buffer->writableSizeForFrame();
			if (monitorBuffer.sendBuffer.size() > 0 && writeSize > 0) {
				if (writeSize >= monitorBuffer.sendBuffer.size()) {
					monitorBuffer.buffer->writeFrame(
						frame.cid,
						Frame::STATE_MONITOR_RESPONSE,
						monitorBuffer.sendBuffer
					);
					monitorBuffer.buffer->close();
				} else {
					string data = monitorBuffer.sendBuffer.substr(0, writeSize);
					monitorBuffer.buffer->writeFrame(
						frame.cid,
						Frame::STATE_MONITOR_RESPONSE,
						data
					);
					monitorBuffer.sendBuffer.append(
						monitorBuffer.sendBuffer.begin() + writeSize,
						monitorBuffer.sendBuffer.end()
					);
				}
				success = true;
			}
			++it;
		}
		return success;
	}

	bool exchangeData() {
	  if (tunnelBuffer.get() == NULL) {
      reset();
      return false;
    }
	  bool r1 = handleMonitorData();
		bool r2 = handleTunnelData();
		bool r3 = handleTrafficData();
		return r1 || r2 || r3;
  }

	int idle() {
	  if (tunnelBuffer.get() != NULL) {
      return tunnelBuffer->writeFrame(0, Frame::STATE_HEARTBEAT);
    }
    return 0;
	}
};

#endif //TCP_TUNNEL_CLIENT_H