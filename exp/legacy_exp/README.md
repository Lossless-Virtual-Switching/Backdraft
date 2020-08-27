# Legacy Experiment

# run\_exp.py
This script starts the containers and setups the BESS pipeline.
In the pipeline setup process a VPort is added to each docker container.

Note: it is important that docker containers are created with `--network none`.

## params
* experiment name: iperf3, apache, or memcached

# Experiments

## iperf3
container name: legacy\_exp\_iperf3
container entrypoint arguments:
* type: 'client' or 'server'
[ client only ]
* ips: list of ips (white space seperated). a process for each destination is created
[ server ]
* ip: ip address of current server

