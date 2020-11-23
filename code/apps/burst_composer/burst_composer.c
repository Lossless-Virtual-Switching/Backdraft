#include <stdio.h>
#include <assert.h>

#include <rte_eal.h>
#include <rte_mbuf.h>
#include <rte_ethdev.h>
#include <rte_malloc.h>
#include <rte_cycles.h>

#include "vport.h"

#define ROUND_TO_64(x) ((x + 32) & (~0x3f))

typedef struct {
  struct vport *port;
  struct llring *shared_ring;
  int cdq;
  int delay;
} worker_arg_t;

int rx_worker(void *);
int tx_worker(void *);

static int dpdk_init(int argc, char *argv[])
{
  int arg_parsed;
  arg_parsed = rte_eal_init(argc, argv);
  if (arg_parsed < 0)
    rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
  return arg_parsed;
}

int main(int argc, char *argv[])
{
  int arg_dpdk;
  const int count_ports = 2;
  int count_queues[count_ports];
  int count_discriptors[count_ports];
  struct vport *ports[count_ports];
  int i;
  char port_name[PORT_NAME_LEN];
  struct llring *shared_ring = NULL;
  int count_lcores;
  int lcore_id;
  const int expected_workers = 2;
  worker_arg_t wargs[expected_workers];
  int bytes_per_llring;

  // parameters
  int count_queues_param = -1;
  int delay = 0;

  int shared_buffer_size = 8192;

  arg_dpdk = dpdk_init(argc, argv);
  argc -= arg_dpdk;
  argv += arg_dpdk;

  if (argc < 3) {
    // TODO: print usage
    printf("usage problem\n");
    return 1;
  }
  count_queues_param = atoi(argv[1]);
  delay = atoi(argv[2]);

  count_lcores = rte_lcore_count();
  assert(count_lcores == expected_workers);

  for (i = 0; i < count_ports; i++)
    count_queues[i] = count_queues_param;

  count_discriptors[0] = 1024; // transmit
  count_discriptors[1] = 64;  // receive

  // create a vport
  for (i = 0; i < count_ports; i++) {
    snprintf(port_name, PORT_NAME_LEN, "vport_%d", i);
    ports[i] = _new_vport(port_name, count_queues[i], count_queues[i],
                         count_discriptors[i]);
    printf("\nCreate new port: \n");
    printf("* port name: %s\n", port_name);
    printf("* count queue: %d\n", count_queues[i]);
    printf("* count discriptor: %d\n", count_discriptors[i]);
  }

  // prepare shared buffer
  bytes_per_llring = llring_bytes_with_slots(shared_buffer_size);
  bytes_per_llring = ROUND_TO_64(bytes_per_llring);
  shared_ring = rte_zmalloc(NULL, bytes_per_llring, 64);
  llring_init(shared_ring, shared_buffer_size, 1, 1);
  llring_set_water_mark(shared_ring, shared_buffer_size);

  // prepare worker args
  for (i = 0; i < expected_workers; i++) {
    wargs[i].port = ports[i];
    wargs[i].shared_ring = shared_ring;
    wargs[i].cdq = 0;
    wargs[i].delay = delay;
  }

  i = 0;
  RTE_LCORE_FOREACH_SLAVE(lcore_id) {
    rte_eal_remote_launch(tx_worker, (void *)&wargs[i], lcore_id);
    i++;
  }
  rx_worker(&wargs[i]);
  rte_eal_mp_wait_lcore();

  // free vport
  free(shared_ring);
  for (i = 0; i < count_ports; i++) {
    free_vport(ports[i]);
  }
}

int rx_worker(void *args)
{
  worker_arg_t *wargs = args;
  struct vport *port = wargs->port;
  struct llring *shared_ring = wargs->shared_ring;
  int cdq = wargs->cdq;

  int run = 1;
  int count_queues = port->bar->num_inc_q;
  int burst = 32;
  int num_rx = 0;
  int num_enqueue;
  int qid = 0;
  struct rte_mbuf *batch[burst];

  printf("Rx | port name: %s\n", port->bar->name);
  while (run) {
    // recv a batch
    if (cdq) {

    } else {
      do {
        num_rx = recv_packets_vport(port, qid, (void **)batch, burst);
        qid++;
        qid %= count_queues;
      } while (num_rx == 0);
    }

    // place packets in shared ring
    do {
      num_enqueue = llring_enqueue_bulk(shared_ring, (void **)batch, num_rx);
    } while(num_enqueue == -LLRING_ERR_NOBUF);
    // printf("read and enqueue packest\n");
  }
  return 0;
}

int tx_worker(void *args)
{
  worker_arg_t *wargs = args;
  struct vport *port = wargs->port;
  struct llring *shared_ring = wargs->shared_ring;
  // int cdq = wargs->cdq;
  int delay = wargs->delay;

  int run = 1;
  int count_queues = port->bar->num_inc_q;
  int burst = 1024;
  struct rte_mbuf *batch[burst];
  int num_tx;
  int num_dequeue;
  int qid = 0;
  int sent;

  printf("Tx | port name: %s\n", port->bar->name);
  while (run) {
    if (delay > 0)
      rte_delay_us_block(delay);

    // deqeue some packets
    do {
      num_dequeue = llring_dequeue_burst(shared_ring, (void **)batch, burst);
    } while (num_dequeue == 0);

    // transmit packets
    sent = 0;
    // if (num_dequeue > 100)
    //   printf("Should sent large burst %d\n", num_dequeue);
    do {
      num_tx = send_packets_vport(port, qid, (void **)(batch + sent),
                                  num_dequeue - sent);
      sent += num_tx;
      // if (num_tx > 100)
      //   printf("Send burst %d\n", num_tx);
    } while(sent < num_dequeue);

    qid++;
    qid %= count_queues;
  }
  return 0;
}
