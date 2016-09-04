AR=/usr/bin/ar
CC=/usr/bin/gcc
CXX=/usr/bin/g++
LINK=/usr/bin/link
LD=/usr/bin/ld

install: tcp_client.cpp tcp_server.cpp main.cpp common.cpp logger.cpp
	$(CC) tcp_client.cpp tcp_server.cpp main.cpp common.cpp logger.cpp -o main -lstdc++

tcp_server.o: tcp_server.cpp
	$(CC) -c tcp_server.cpp -o tcp_server.o

tcp_client.o: tcp_client.cpp
	$(CC) -c tcp_client.cpp -o tcp_client.o

main.o: main.cpp
	$(CC) -c main.cpp -o main.o