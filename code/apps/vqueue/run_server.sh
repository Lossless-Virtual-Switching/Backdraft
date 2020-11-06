#!/bin/sh
sudo ./build/vport_test_app -l 6 --file-prefix=m1 --proc-type=primary \
  --no-pci \
  --socket-mem 15000,15000 \
  -- \
  server $1
