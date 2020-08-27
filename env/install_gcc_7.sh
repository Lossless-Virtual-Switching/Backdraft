#!/bin/sh

sudo apt-get update -y
sudo apt-get install -y software-properties-common
sudo add-apt-repository ppa:ubuntu-toolchain-r/test
sudo apt update
sudo apt install g++-7 -y

sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-7 60 \
                         --slave /usr/bin/g++ g++ /usr/bin/g++-7 
sudo update-alternatives --config gcc
echo version:
echo gcc: $(gcc --version)
echo g++: $(g++ --version)

