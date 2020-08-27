FROM ubuntu:18.04

RUN apt-get update; \
    apt-get install -y iperf3 net-tools iputils-ping

WORKDIR /root
COPY ./boot.sh ./boot.sh


ENTRYPOINT ["/root/boot.sh"]
