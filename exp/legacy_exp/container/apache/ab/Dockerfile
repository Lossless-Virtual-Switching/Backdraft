# Backdraft Legacy Experiment
# ApacheBench Dockerfile

FROM ubuntu:18.04

RUN apt-get update; \
  apt-get install -y apache2-utils

WORKDIR /root

COPY ./boot.sh ./boot.sh

ENTRYPOINT ["./boot.sh"]
