# Backdraft components

## Device under test environment setup

1. Instll bess deps + OFED for mellanox
2. Build repo
3. Goto /exp/performance\_exp
4. Update `performance_exp.py` to use the right Bess pipeline
4. Run `perofrmance_exp.py`
5. After the environment was setup successfully run `Mutilate` and `udp_app` on two seperate machines

> Ip addresses 192.168.1.50 and 192.168.1.51 are configured for `Memcached` and `UDP Server` apps.


## Installing mutilate

```
function install_mutilate {   . /etc/os-release;   tmp=`pwd`;   if [ ! -d ./mutilate/ ];   then     sudo apt-get update;     sudo apt-get install -y scons libevent-dev gengetopt libzmq3-dev;     git clone https://github.com/Lossless-Virtual-Switching/mutilate;   fi;   cd mutilate;   if [ ! -f ./mutilate ];   then
    if [ "$VERSION_ID" = "20.04" ] && [ -f SConstruct3 ];     then       mv SConstruct3 SConstruct;     else       echo "if your system does not have python2 then rename SConstruct3 as SConstruct";     fi;     scons;   fi;   cd $tmp; }

install_mutilate
```


## Running clients

**Mutilate:**

```
./mutilate -s 192.168.1.50 -t 10 -T 2 -c 5
```

**UDP App:**

```
sudo ./udp_app --lcores=10 --file-prefix=udp -w 41:00.0 --socket-mem=128 -n 4 \
-- bidi=true 192.168.1.1 1 bess client 1 192.168.1.51 50 15 8080 0 10000
```


## Pipeline

For Lossy:        `pipelines/simple_pipeline.bess`
For Backpressure: `pipelines/bp_pipeline.bess`
For DPFQ:         `pipelines/dpfq_pipeline.bess`


This is the outline of Bess pipeline
```
                                   +---------------------+
                                   |     queue_inc1      |
                                   |      QueueInc       |
                                   | ex_vhost1:0/PMDPort |
                                   +---------------------+
                                     |
                                     | :0 0 0:
                                     v
+---------------------+            +---------------------+            +---------------------+
|     queue_inc0      |            |       queue0        |            |     queue_out2      |
|      QueueInc       |  :0 0 0:   |        Queue        |  :0 0 0:   |      QueueOut       |
| ex_vhost0:0/PMDPort | ---------> |       0/1024        | ---------> | pmd_port0:0/PMDPort |
+---------------------+            +---------------------+            +---------------------+
                                     ^
                                     | :0 0 0:
                                     |
+---------------------+            +---------------------+            +---------------------+            +---------------------+
|     queue_inc2      |            |   arp_responder0    |            |     ip_lookup0      |            |     queue_out0      |
|      QueueInc       |  :0 0 0:   |    ArpResponder     |  :1 0 0:   |      IPLookup       |  :1 0 0:   |      QueueOut       |
| pmd_port0:0/PMDPort | ---------> |                     | ---------> |                     | ---------> | ex_vhost0:0/PMDPort |
+---------------------+            +---------------------+            +---------------------+            +---------------------+
                                                                        |
                                                                        | :2 0 0:
                                                                        v
                                                                      +---------------------+
                                                                      |     queue_out1      |
                                                                      |      QueueOut       |
                                                                      | ex_vhost1:0/PMDPort |
                                                                      +---------------------+
```

