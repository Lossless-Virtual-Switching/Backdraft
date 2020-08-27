#!/bin/bash

# Usage:
# client:
#   * <server-request-unit-bytes> <max_simultaneous_flows> [<hostname> <port>]+
# server:
#   * server port

type=$1

if [ "x$type" = "xclient" ]
then
  echo client
  shift
  sleep 6
  ./shuffle_client $@
else
  echo server
  shift
  sleep 5
  ./shuffle_server $@
fi
