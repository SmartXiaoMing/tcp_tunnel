install: tunnel build
	cp tunnel ../build/

build:
	mkdir -p ../build/

tunnel: tunnel_branch.cpp tunnel_trunk.cpp event_manager.cpp event_handler.cpp utils.cpp frame.hpp clean
	$(CXX) tunnel_branch.cpp tunnel_trunk.cpp event_manager.cpp event_handler.cpp utils.cpp frame.hpp tunnel.cpp \
	-Os  -fno-rtti -fno-exceptions \
	-o tunnel
	strip tunnel

clean:
	if [ -f tunnel ]; then rm  tunnel; fi
	if [ -d ../build ]; then rm -rf ../build/; fi
