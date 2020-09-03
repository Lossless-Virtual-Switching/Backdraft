# Overlay Multi-Node Experiment
Multi node udp configuration
# Usage
```
usage: run_exp.py [-h] [--server_batch_delay SERVER_BATCH_DELAY]
                  [--count_queue COUNT_QUEUE] [--queue_size QUEUE_SIZE]
                  [--cdq] [--pfq] [--bp] [--buffering] [--overlay]
                  [--cpuset CPUSET] [--output OUTPUT] [--dst_ips DST_IPS]
                  [--duration DURATION]
                  {client,server} source_ip

positional arguments:
  {client,server}       run a client or a server
  source_ip             current node ip address

optional arguments:
  -h, --help            show this help message and exit
  --server_batch_delay SERVER_BATCH_DELAY
                        amount of work server does for each batch of packets
                        (in us)
  --count_queue COUNT_QUEUE
                        number of queues available to the app
  --queue_size QUEUE_SIZE
                        size of each queue
  --cdq                 use command queue and datat queue
  --pfq                 enable per-flow queueing
  --bp                  enable backpressure
  --buffering           use buffering mechanism
  --overlay             enable overlay protocol
  --cpuset CPUSET       allocated cpus for the application
  --output OUTPUT       results will be writen in the given file
  --dst_ips DST_IPS     client destination ip adresses (e.g. "192.168.1.3
                        192.168.1.4")
  --duration DURATION   experiment duration
```
# OLD
## Notes:
> These configuration should not be needed any more
1. Config switch for vlan 100
2. Config switch with static address
`mac-address-table static <mac> vlan <1/100> interface ethernet <if>`
