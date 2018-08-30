install: tunnel_client tunnel_server build
	cp tunnel_client tunnel_server ../build/

build:
	mkdir -p ../build/

tunnel_client: endpoint.cpp endpoint_client.cpp center_client.cpp tunnel_client.cpp utils.cpp
	$(CXX) endpoint.cpp endpoint_client.cpp center_client.cpp tunnel_client.cpp utils.cpp -ggdb -o tunnel_client

tunnel_server: endpoint.cpp endpoint_server.cpp endpoint_client.cpp center_server.cpp tunnel_client.cpp utils.cpp
	$(CXX) endpoint.cpp endpoint_server.cpp endpoint_client.cpp center_server.cpp tunnel_server.cpp utils.cpp -O2 -ffunction-sections -Wl,-Map=object.map,--cref,--gc-section -o tunnel_server

clean:
	rm  tunnel_client tunnel_server *.map
	rm -rf ../build/
