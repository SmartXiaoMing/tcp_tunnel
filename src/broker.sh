#!/bin/bash

nohup ./tunnel_broker \
--port 8122 \
--v 9 > broker.txt &
