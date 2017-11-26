install: client build
	cp client* ../build/

build:
	mkdir -p ../build/

client:
	$(CC) tunnel_client.c -std=c99 -o client

client_mipsel: build
	make -f Makefile_mipsel

client_arm: build
	make -f Makefile_arm

clean:
	rm -rf ../build/
