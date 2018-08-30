install: client build
	cp client* ../build/

build:
	mkdir -p ../build/

client: tunnel_client.c
	$(CC) tunnel_client.c -std=c99 -O2 -ffunction-sections -Wl,-Map=object.map,--cref,--gc-section -o client

client_mipsel: build
	make -f Makefile_mipsel

client_arm: build
	make -f Makefile_arm

clean:
	rm -rf ../build/
