FROM ubuntu:18.04

WORKDIR /root

COPY ./shuffle/shuffle_client .
COPY ./shuffle/shuffle_server .
COPY ./boot.sh .

ENTRYPOINT ["./boot.sh"]

