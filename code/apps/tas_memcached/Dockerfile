FROM ubuntu:18.04

# install dependencies
RUN apt-get update; \
    apt-get install -y \
        git build-essential python3 python3-pip libnuma-dev \
        autoconf automake libtool curl make g++ unzip pkg-config \
        memcached scons libevent-dev gengetopt libzmq3-dev vim \
        perl m4 libtool-bin; \
    cd /usr/bin/; \
    ln -sf python3 python

# build protobuf
RUN cd /opt/; \
    git clone https://github.com/google/protobuf.git; \
    git clone https://github.com/protobuf-c/protobuf-c; \
    cd protobuf/; \
    git checkout aedb8d726c8d2a371d999354d44ca7e1bdbb1e73; \
    ./autogen.sh && ./configure DIST_LANG=cpp && make -j && make install && ldconfig; \
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
    make RTE_SDK=../dpdk/x86_64-native-linuxapp-gcc; \
    cd ../tas-benchmark/micro_rpc_modified/; \
    make;

# setup mutilate
RUN git clone https://github.com/Lossless-Virtual-Switching/mutilate; \
    cd mutilate; \
    mv SConstruct3 SConstruct; \
    scons;

# SOCKPERF was not used in experiment
# setup sockperf
# RUN git clone https://github.com/Mellanox/sockperf.git; \
# cd sockperf/; \
# git checkout b3df070d564973c9d6a4a0e143de7703043c5713; \
# ./autogen.sh && ./configure && make -j && make install

# add boot script
COPY ["boot.sh", "run.sh", "kill.sh", "tcp.sh", "latency.sh", "./"]

ENTRYPOINT ["/root/boot.sh"]

