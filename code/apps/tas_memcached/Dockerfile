FROM ubuntu:18.04

# install dependencies
RUN apt-get update; \
    apt-get install -y \
        git build-essential python3 python3-pip libnuma-dev \
        autoconf automake libtool curl make g++ unzip pkg-config \
        memcached scons libevent-dev gengetopt libzmq3-dev; \
    cd /usr/bin/; \
    ln -sf python3 python

# build protobuf
RUN cd /opt/; \
    git clone https://github.com/google/protobuf.git; \
    git clone https://github.com/protobuf-c/protobuf-c; \
    cd protobuf/; \
    ./autogen.sh && ./configure && make -j && make install && ldconfig; \
    cd ../protobuf-c/ ; \
    ./autogen.sh && ./configure && make && make install && ldconfig

# setup post-loom: build DPDK, TAS
WORKDIR /root
COPY ./post-loom.tar.xz ./post-loom.tar.xz
RUN  tar -xzf ./post-loom.tar.xz; \
    rm post-loom.tar.xz
RUN cd post-loom/code/dpdk; \
    make config T=x86_64-native-linuxapp-gcc; \
    make install T=x86_64-native-linuxapp-gcc -j; \
    cd ../tas/; \
    make RTE_SDK=../dpdk/x86_64-native-linuxapp-gcc

# setup mutilate
RUN git clone https://github.com/erfan111/mutilate; \                           
  cd mutilate; \                                                                
  scons 

# add boot script
COPY ./boot.sh ./boot.sh

ENTRYPOINT ["/root/boot.sh"]

