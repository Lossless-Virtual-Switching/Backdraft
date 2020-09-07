# Short Long Flow Experiment
For running experiment you can run `setup_node.py`. This scrip reads the 
cluster config file (default file is cluster\_config.yml) and runs some
containers and connect them to BESS pipeline.

# Configuration
For running this experiment two configuration files are need.
1. cluster\_config.yml
2. flow\_config.json

cluster\_config.yml: is a yaml file describing nodes and their connections.
flow\_config.json: has configuration for application.

## Cluster Configuration
The cluster configuration is given in a yaml format writen in file named `cluster_config.yml`.
In this config file, general information of a node along with the instances
running on it are given. Each instance is either a tcp client or a tcp server.
Client can generate flow for multiple destinations (with different ip:port).

an example config file looks like this:

```
# cluster_config.yml
node2:
  ip: "192.168.1.2"
  mac: "9c:dc:71:4a:3c:a1"
  instances:
    - port: 1001
      type: "client"
      cpus: 5,6,7,8
      flow_config_name: "default_client"
      tas_cores: 1
      destinations:
        - node3_0
        - node4_0
node3:
  ip: "192.168.1.3"
  mac: "98:f2:b3:cc:83:81"
  instances:
    - port: 5678
      type: "server"
      cpus: 5,6
      tas_cores: 1
node4:
  ip: "192.168.1.4"
  mac: "98:f2:b3:cc:02:c1"
  instances:
    - port: 5678
      type: "server"
      cpus: 5,6
      tas_cores: 1

```

Each node has an ip address and an ethernet (mac) address. For each node
their can be multiple instances of application (tcp client or tcp server
running in a docker container). Instances can be addressed by their node name
and their index (counted from zero).
Each instance has the following values:
* port: port on which it is running
* type: either client or server
* cpus: a cpuset allocated to container
* flow\_config\_name: config from flow\_config file for applying to application.
* tas\_cores: number of fast-path cores of TAS engine running in the container.
* destinations: an array of the name of destination nodes followed by instance
index. For example second instance of node with name 'test' will be addressed
by 'test\_1'

## Flow Configuration
Some application can be defined here. They are linked to instances using
flow\_config\_name value in cluster\_config.yml.
 
an example of config file is given below:

```
{
  "default_client": {
    "message_per_sec": -1,
    "flow_duration": 0,
    "message_size": 2048,
    "flow_num_msg": 0,
    "flow_per_destination": 4,
    "count_thread": 1
  },
  "default_server": {
    "message_size": 10000,
    "max_flow": 8192,
    "count_thread": 1
  }
}
```

# setup\_node.py
```
usage: setup_node.py [-h] [-config CONFIG] [-flow_config FLOW_CONFIG]
                     [-bessonly] [-kill]

Setup a node for short-long-flow experiment

optional arguments:
  -h, --help            show this help message and exit
  -config CONFIG        path to cluster config file (default:
                        ./cluster_config.yml)
  -flow_config FLOW_CONFIG
                        path to flow config file (default: ./flow_config.json)
  -bessonly             only setup BESS pipeline
  -kill                 kill previous running instances and exit

```

# Experiments
1. Overlay multi-node TCP

## Overlay Multi-Node TCP
This experiment compares Backdraft performance with focus on overlay messages.
The setup containes 4 machines 2 of them configured as servers and the other as
client. Both of the servers are working with full power (no slow down).
One of the client machines have two instances of the app each sending messages
to one of the server.
Each container in which the applications are running has 3 cores.

Experiment was ran with the `run_exp.py` script on all machines with the help of
ansible. Warmup time was set to 180 sec and experiment duration was 60 sec.

# Note
For using `run_exp` make sure you have enable pfc for prio3

```
$ mlnx_qos -i eno50 --pfc 0,0,0,1,0,0,0,0
```

