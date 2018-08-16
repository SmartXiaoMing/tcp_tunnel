install: tunnel_client tunnel_server build
	cp tunnel ../build/

build:
	mkdir -p ../build/

tunnel_client: endpoint.cpp center_client.cpp tunnel_client.cpp utils.cpp
	$(CXX) endpoint.cpp center_client.cpp tunnel_client.cpp utils.cpp -O2 -ffunction-sections -Wl,-Map=object.map,--cref,--gc-section -o tunnel_client

tunnel_server: endpoint.cpp center_server.cpp tunnel_client.cpp utils.cpp
	$(CXX) endpoint.cpp center_server.cpp tunnel_server.cpp utils.cpp -O2 -ffunction-sections -Wl,-Map=object.map,--cref,--gc-section -o tunnel_server

clean:
	rm  tunnel *.map
	rm -rf ../build/
