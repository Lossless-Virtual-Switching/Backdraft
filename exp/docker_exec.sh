#!/bin/bash

docker build -t testimage -f Dockerfile.sqdtest  .
docker exec -it $1 bash
