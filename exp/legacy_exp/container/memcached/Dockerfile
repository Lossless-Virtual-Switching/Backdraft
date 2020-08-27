# Backdraft Legacy Experiment
# ApacheBench Dockerfile

FROM ubuntu:18.04

RUN apt-get update; \
  apt-get install -y net-tools iputils-ping; \
  apt-get install -y memcached; \
  apt-get install -y git; \
  apt-get install -y scons libevent-dev gengetopt libzmq3-dev build-essential; \
  apt-get install -y iperf3

WORKDIR /root

RUN git clone https://github.com/erfan111/mutilate; \
  cd mutilate; \
  scons

WORKDIR /root/mutilate

COPY ./shuffle_client .
COPY ./shuffle_server .
COPY ./boot.sh ./boot.sh

ENTRYPOINT ["./boot.sh"]
