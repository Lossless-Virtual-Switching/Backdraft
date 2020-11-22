#!/bin/bash

echo ./build/burst_composer -l 2,4 --file-prefix=m1 --proc-type=primary \
  --no-pci --socket-mem 15000,15000 --

./build/burst_composer -l 2,4 --file-prefix=m1 --proc-type=primary \
  --no-pci --socket-mem 15000,15000 --

