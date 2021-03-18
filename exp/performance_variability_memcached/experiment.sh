#!/bin/bash
echo "make sure servers are running"
$(nohup ./udp_client.sh &)
sleep 20
$(nohup ./client.sh &)
