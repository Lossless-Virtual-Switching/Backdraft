#!/bin/sh
sudo ./build/vport_test_app -l 4 --file-prefix=m1 --proc-type=secondary \
  --no-pci \
  --socket-mem 15000,15000 \
  -- \
  client server_vport
