#!/bin/bash

nohup ./tunnel_agent \
--brokerHost 192.129.212.38 \
--brokerPort 8122 \
--name office \
--peerName proxy \
--targetAddress guess \
--serverIp 0.0.0.0 \
--serverPort  9001 \
--v 5 > agent_local.log &
