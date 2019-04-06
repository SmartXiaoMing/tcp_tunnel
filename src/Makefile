install: tunnel_broker tunnel_agent build
	cp tunnel_broker tunnel_agent ../build/

build:
	mkdir -p ../build/

tunnel_broker: tunnel_broker.cpp endpoint.cpp endpoint_server.cpp endpoint_client.cpp endpoint_client_tunnel.cpp utils.cpp frame.h clean
	$(CXX) endpoint.cpp endpoint_server.cpp endpoint_client.cpp endpoint_client_tunnel.cpp tunnel_broker.cpp utils.cpp frame.h \
	-std=c++11 \
    -Os  -fno-rtti -fno-exceptions \
	-o tunnel_broker
	strip tunnel_broker

tunnel_agent: tunnel_agent.cpp endpoint.cpp endpoint_server.cpp endpoint_client.cpp endpoint_client_tunnel.cpp utils.cpp frame.h clean
	$(CXX) endpoint.cpp endpoint_server.cpp endpoint_client.cpp endpoint_client_tunnel.cpp tunnel_agent.cpp utils.cpp frame.h \
	-std=c++11 \
	-Os  -fno-rtti -fno-exceptions \
	-o tunnel_agent
	strip tunnel_agent

clean:
	if [ -f tunnel_broker ]; then rm tunnel_broker; fi
	if [ -f tunnel_agent ]; then rm tunnel_agent; fi
	if [ -d ../build ]; then rm -rf ../build/; fi
