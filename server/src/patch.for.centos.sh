#!/bin/bash

sed -i "s/it = bufferMap.erase(it);/bufferMap.erase(it++);/g" *
sed -i "s/it = monitorMap.erase(it);/monitorMap.erase(it++);/g" *
sed -i "s/it = trafficMap.erase(it);/trafficMap.erase(it++);/g" *
sed -i "s/tunnelIt = tunnelMap.erase(tunnelIt);/tunnelMap.erase(tunnelIt++);/g" *