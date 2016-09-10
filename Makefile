install: tunnel build
	cp tunnel tunnel.conf build/

build:
	mkdir -p build

tunnel: tcp_client.cpp tcp_server.cpp tunnel.cpp common.cpp logger.cpp tcp_base.h tcp_monitor.cpp
	$(CC) tcp_client.cpp tcp_server.cpp tunnel.cpp common.cpp logger.cpp tcp_monitor.cpp -o tunnel -lstdc++

clean:
	rm -rf build/ tunnel