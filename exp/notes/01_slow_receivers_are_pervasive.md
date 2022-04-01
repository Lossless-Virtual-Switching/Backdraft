# Slow receivers are pervasiv

OS: ubuntu-20

## Iperf3

**Setup environment:**

install iperf3 on both node1 and node2.

```
sudo apt install iperf3
```

use `/exp/motivation/slow_receivers_are_pervasive/run_iperf_*.sh` scripts for
running clients and serveres. These scripts take an argument indicating the
number of the cpu cores.

**Running:**

> n: number of cores

On node2:

```
bash run_iperf_server.sh <n>
```

On node1:

```
export IPERF_SERVER_IP=node2
bash run_iperf_client.sh <n>
```

> You need to sum the output for each core to get the total throughput


## Memcached

Script provided at
`/exp/motivation/slow_receivers_are_pervasive/run_memcached_exp.sh`
will prepare test nodes and server node. The machine running the script
should be able to `ssh` to other machines.

> Each machine should use `bash` as the default shell

> The script clones `mutilate` repository in the home direcotry of each agent node. But it might fail to compile it.

1. generate ssh key
2. copy `$HOME/.ssh/id_rsa.pub` to `$HOME/.ssh/authorized_keys` on other machines

```
ssh-keygen
cat $HOME/.ssh/id_rsa.pub
# copy the key and paste in other machines to allow access
ssh-keyscan node2 node3 node4 node5 >> ~/.ssh/known_hosts
# edit file `run_memcached_exp.sh` and set
server_ip=node2
server_cores=<n>
agent_machines=(node3 node4 node5) # list of nodes to be used as mutilate agents
mutilate_valuesize=4800 # value size of the experiment in this case 4.8KB
```

> n: number of cores

> After script finishes the memcached server is not terminated


## Nginx

The script should prepare the worker nodes.

On the server configure nginx:

```
sudo apt install -y nginx
sudo systemctl disable nginx.service
sudo cp ./nginx.conf /etc/nginx/nginx.conf
# edit run_nginx_exp and set variables
server_ip=node2
remote_machines=(node3 node4 node5)
```

To change the response size you need to change the index.html file

## Memcached DPDK

to be completed
