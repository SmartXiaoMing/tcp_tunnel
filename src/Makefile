CXX = g++
STRIP = strip
LIB_DIR =
CXXFLAGS += -std=c++11 -ggdb -g

# release version, use the options
# -Os -fno-rtti -fno-exceptions and (STRIP) tunnel_agent
# instead of
# -ggdb

install: tunnel_broker tunnel_agent build
	cp tunnel_broker tunnel_agent ../build/

build:
	mkdir -p ../build/

tunnel_broker: tunnel_broker.cpp endpoint.cpp endpoint_server.cpp endpoint_client.cpp endpoint_client_tunnel.cpp utils.cpp frame.h clean
	$(CXX) endpoint.cpp endpoint_server.cpp endpoint_client.cpp endpoint_client_tunnel.cpp tunnel_broker.cpp utils.cpp frame.h \
	$(CXXFLAGS) \
	$(LIB_DIR) $(LDFLAGS) \
	-o tunnel_broker

tunnel_agent: tunnel_agent.cpp endpoint.cpp endpoint_server.cpp endpoint_client.cpp endpoint_client_tunnel.cpp utils.cpp frame.h clean
	$(CXX) endpoint.cpp endpoint_server.cpp endpoint_client.cpp endpoint_client_tunnel.cpp tunnel_agent.cpp utils.cpp frame.h \
	$(CXXFLAGS) \
	$(LIB_DIR) $(LDFLAGS) \
	-o tunnel_agent

tunnel_start: tunnel_start.cpp endpoint.cpp endpoint_server.cpp endpoint_client.cpp endpoint_client_tunnel.cpp utils.cpp frame.h clean
	$(CXX) endpoint.cpp endpoint_server.cpp endpoint_client.cpp endpoint_client_tunnel.cpp tunnel_start.cpp utils.cpp frame.h \
	$(CXXFLAGS) \
	$(LIB_DIR) $(LDFLAGS) \
	-o tunnel_start

tunnel_end: tunnel_end.cpp endpoint.cpp endpoint_server.cpp endpoint_client.cpp endpoint_client_tunnel.cpp utils.cpp frame.h clean
	$(CXX) endpoint.cpp endpoint_server.cpp endpoint_client.cpp endpoint_client_tunnel.cpp tunnel_end.cpp utils.cpp frame.h \
	$(CXXFLAGS) \
	$(LIB_DIR) $(LDFLAGS) \
	-o tunnel_end

clean:
	if [ -f tunnel_broker ]; then rm tunnel_broker; fi
	if [ -f tunnel_agent ]; then rm tunnel_agent; fi
	if [ -f tunnel_start ]; then rm tunnel_start; fi
	if [ -f tunnel_end ]; then rm tunnel_end; fi
	if [ -d ../build ]; then rm -rf ../build/; fi
