install: tunnel build
	cp tunnel tunnel.conf build/

build:
	mkdir -p build

tunnel: tcp_client.cpp tcp_server.cpp tunnel.cpp common.cpp logger.cpp
	$(CC) tcp_client.cpp tcp_server.cpp tunnel.cpp common.cpp logger.cpp -o tunnel -lstdc++