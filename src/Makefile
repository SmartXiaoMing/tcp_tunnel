install: tunnel build
	cp tunnel ../build/

build:
	mkdir -p ../build/

tunnel: endpoint.cpp center.cpp tunnel.cpp utils.cpp clean
	$(CXX) endpoint.cpp center.cpp tunnel.cpp utils.cpp \
	-Os  -fno-rtti -fno-exceptions \
	-o tunnel
	strip tunnel

clean:
	if [ -f tunnel ]; then rm  tunnel; fi
	if [ -d ../build ]; then rm -rf ../build/; fi
