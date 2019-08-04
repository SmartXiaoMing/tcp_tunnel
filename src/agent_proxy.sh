#!/bin/bash

./tunnel_agent \
--brokerHost 127.0.0.1 \
--brokerPort 8122 \
--name proxy \
--peerName home \
--targetAddress guess \
--serverIp 0.0.0.0 \
--serverPort  9003 \
--v 5
