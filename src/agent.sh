#!/bin/bash

./tunnel_agent \
--brokerHost 127.0.0.1 \
--brokerPort 8122 \
--name home \
--peerName proxy \
--targetAddress guess \
--serverIp 0.0.0.0 \
--serverPort  9002 \
--v 5
