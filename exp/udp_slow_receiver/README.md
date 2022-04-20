# UDP Slow Receiver

This is a single node experiment. For testing Backdraft.

> Make sure udp client/server app is built (`/code/apps/udp_client_server/`).

## Usage

```
usage: run_udp_test.py [-h] [--count_flow COUNT_FLOW] [--cdq] [--bp] [--pfq] [--buffering] [--duration DURATION] [--bessonly] [--port_type {pmd,bdvport}]
                       count_queue mode delay

positional arguments:
  count_queue
  mode                  define whether bess or bkdrft system should be used
  delay                 processing cost for each packet in cpu cycles (for both client and server)

optional arguments:
  -h, --help            show this help message and exit
  --count_flow COUNT_FLOW
                        number of flows, per each core. used for client app
  --cdq                 enable command data queueing
  --bp                  have pause call (backpresure) enabled
  --pfq                 enable per flow queueing
  --buffering           buffer packets (no drop)
  --duration DURATION   experiment duration
  --bessonly
  --port_type {pmd,bdvport}
                        type of port used in pipeline configuration
```
