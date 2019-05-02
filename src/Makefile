CXX = g++
STRIP = strip
LIB_DIR =
CXXFLAGS += -ggdb

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
	-std=c++11 \
	$(LIB_DIR) $(LDFLAGS) \
	-o tunnel_broker

tunnel_agent: tunnel_agent.cpp endpoint.cpp endpoint_server.cpp endpoint_client.cpp endpoint_client_tunnel.cpp utils.cpp frame.h clean
	$(CXX) endpoint.cpp endpoint_server.cpp endpoint_client.cpp endpoint_client_tunnel.cpp tunnel_agent.cpp utils.cpp frame.h \
	-std=c++11 \
	$(LIB_DIR) $(LDFLAGS) \
	-o tunnel_agent

clean:
	if [ -f tunnel_broker ]; then rm tunnel_broker; fi
	if [ -f tunnel_agent ]; then rm tunnel_agent; fi
	if [ -d ../build ]; then rm -rf ../build/; fi
