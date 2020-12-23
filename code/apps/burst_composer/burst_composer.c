#include <stdio.h>
#include <assert.h>

#include <rte_eal.h>
#include <rte_mbuf.h>
#include <rte_ethdev.h>
#include <rte_malloc.h>
#include <rte_cycles.h>

#include "vport.h"

#define ROUND_TO_64(x) ((x + 64) & (~0x3f))
#define MAX_PAUSE_DURATION 500000000
#define SECOND_NS 1000000000UL

typedef struct {
  struct vport *port;
  struct llring *shared_ring;
  int cdq;
  int delay;
} worker_arg_t;

int rx_worker(void *);
int tx_worker(void *);

static inline uint64_t ns_to_cycles(uint64_t ns)
{
  return (ns * rte_get_timer_hz()) / SECOND_NS;
}

static inline uint64_t cycles_to_ns(uint64_t cycles)
{
  uint64_t ns;
  ns = (cycles * SECOND_NS) / rte_get_timer_hz();
  // printf("cycles: %ld, ns: %ld hz: %ld\n", cycles, ns, rte_get_timer_hz());
  return ns;
}

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
  uint16_t count_queues[count_ports];
  uint16_t count_discriptors[count_ports];
  uint32_t port_pool_size;
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

  int shared_buffer_size = 1024;

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

  // count_discriptors[0] = 1024;  // transmit
  count_discriptors[0] = 256;  // transmit
  count_discriptors[1] = 64;  // receive

  // create a vport
  for (i = 0; i < count_ports; i++) {
    port_pool_size = (count_queues[i] + count_queues[i]) * 3 / 2 + 64;
    snprintf(port_name, PORT_NAME_LEN, "vport_%d", i);
    ports[i] = _new_vport(port_name, count_queues[i], count_queues[i],
                          port_pool_size, count_discriptors[i]);
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

// Pause global variable
uint64_t pause_start_ts = 0;
uint64_t pause_end_ts = 0;

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
  uint64_t now;

  printf("Rx | port name: %s\n", port->bar->name);
  while (run) {

    now = rte_get_timer_cycles();
    // check if we should wait
    if (now >= pause_start_ts && now <= pause_end_ts) {
      continue;
    }
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
  int backpressure = 1;

  int run = 1;
  int count_queues = port->bar->num_inc_q;
  int burst = 1024;
  struct rte_mbuf *batch[burst];
  int num_tx;
  int num_dequeue;
  int qid = 0;
  int sent;
  uint64_t now;
  uint64_t last_log_ts = 0;

  uint8_t is_paused = 0;
  uint64_t pause_duration = 0;
  uint64_t pause_per_sec = 0;
  uint64_t pause_duration_out_of_sec = 0;
  uint64_t cycles;

  uint8_t has_update_pause = 0;

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
    do {
      now = rte_get_timer_cycles();
      if (now - last_log_ts > rte_get_timer_hz()) {
        printf("pause per sec: %ld  pause duration out of one sec: %ld\n",
               pause_per_sec, pause_duration_out_of_sec);
        pause_per_sec = 0;
        pause_duration_out_of_sec = 0;
        last_log_ts = now;
      }

      assert( pause_end_ts >= pause_start_ts);

      if (now >= pause_start_ts && now <= pause_end_ts) {
        if (has_update_pause == 0) {
          set_queue_pause_state(port, 1, qid, 1);
          has_update_pause = 1;
        }
        continue;
      } else if (now > pause_end_ts) {
        is_paused = 0;
        if (has_update_pause == 1) {
          has_update_pause = 0;
          set_queue_pause_state(port, 1, qid, 0);
        }
      }

      num_tx = send_packets_vport_with_bp(port, qid, (void **)(batch + sent),
                                          num_dequeue - sent, &pause_duration);
      sent += num_tx;

      if (backpressure && pause_duration > 0 && !is_paused) {
        // if (pause_duration > MAX_PAUSE_DURATION) {
        //   pause_duration = MAX_PAUSE_DURATION;
        // }
        pause_per_sec += 1;
        cycles = ns_to_cycles(pause_duration); //lossy conversion
        pause_duration_out_of_sec += cycles_to_ns(cycles); //lossless conversion
        is_paused = 1;
        pause_start_ts = rte_get_timer_cycles() + ns_to_cycles(delay * 500);
        pause_end_ts = pause_start_ts + cycles;
        assert(now < pause_start_ts);
        // printf("pause duration %ld\n", pause_duration);
      }
    } while(sent < num_dequeue);

    qid++;
    qid %= count_queues;
  }
  return 0;
}
