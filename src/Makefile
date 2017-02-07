install: tunnel build
	cp tunnel.conf build/
	mv tunnel build/

build:
	mkdir -p build

tunnel: common.cpp logger.cpp buffer.hpp buffer_monitor.hpp buffer_traffic.hpp  tunnel_rule.hpp tunnel_client.hpp tunnel_server.hpp tunnel_monitor.hpp tunnel.cpp
	$(CXX) common.cpp logger.cpp tunnel_rule.hpp tunnel_client.hpp tunnel_server.hpp tunnel_monitor.hpp tunnel.cpp -lm -std=c++0x -o tunnel

clean:
	rm -rf build/
