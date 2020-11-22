#include <stdio.h>
#include <signal.h>
#include <string.h>

#include <rte_mbuf.h>
#include <rte_cycles.h>

#include "vport.h"
#include "app.h"

#define SHIFT_ARGS {argc--;if(argc > 0)argv[1]=argv[0]; argv++;}

static volatile int run = 1;

static void sig_int_handler(__attribute__((unused))int dummy)
{
  run = 0;
}

static void worker(struct vport *port)
{
  int count_queue = port->bar->num_out_q;
  int burst = 32;
  int num_rx;
  uint64_t received_pkts = 0;
  uint64_t q_recv[count_queue];
  uint64_t throughput = 0;
  struct rte_mbuf *rx_buf[burst];

  int16_t qid;

  uint64_t last_update_ts;
  uint64_t current_ts;

  uint8_t doorbell_q = 1;

  if (doorbell_q)
    assert(count_queue > 2);

  // initialize q_recv
  for (int i = 0; i < count_queue; i++)
    q_recv[i] = 0;

  last_update_ts = rte_get_tsc_cycles();
  while (run) {
    for (int i = 0; i < count_queue; i++) {
      current_ts = rte_get_tsc_cycles();

      if (doorbell_q) {
        do {
          num_rx = recv_packets_vport(port, 0, (void**)rx_buf, 1);
        } while (run && num_rx == 0);
        if (!run)
          break;

        // read ctrl packet
        uint16_t *ptr = rte_pktmbuf_mtod(rx_buf[0], uint16_t *);
        qid = *ptr;
        if (qid > count_queue) printf("wow: %d\n", qid);
        // printf("qid: %d\n", qid);
        rte_pktmbuf_free(rx_buf[0]);
      } else {
        qid = i;
      }

      // poll receive queue
      num_rx = recv_packets_vport(port, qid, (void**)rx_buf, burst);
      if (num_rx > 0) {
        received_pkts += num_rx;
        throughput += num_rx;
        q_recv[i] += num_rx;

        for (int j = 0; j < num_rx; j++)
          rte_pktmbuf_free(rx_buf[j]);
      }

      // update stats on screen
      if (current_ts > last_update_ts + rte_get_tsc_hz()) {
        printf("TP: %ld  total packets: %ld\n", throughput, received_pkts);
        throughput = 0;
        last_update_ts = current_ts;
      }

      for (int j = 0; j < 10; j++) {
        for (int k = 0; k < 100; k++) {
           __asm__ volatile("" : "+g" (j) : :);
        }
      }

    }
  }

  // printf("\nqueue stats\n");
  // for (int i = 0; i < count_queue; i++) {
  //   printf("queue %d: %ld\n", i, q_recv[i]);
  // }
}

static void print_usage(void)
{
  printf("server usage: [count queue]\n"
         "* count queue: int, [1, %d]\n", MAX_QUEUES_PER_DIR);
}

/* Server main function
 * */
int server_main(int argc, char* argv[])
{
  int count_queue = 10000; // default: 8 queues
  struct vport *port = NULL;
  int port_connected = 0;
  char port_name[PORT_NAME_LEN];
  char port_path[PORT_DIR_LEN];
  size_t bar_address;
  FILE *vport_fp;

  if (argc > 1) {
    // check if can connect to a port
    if (strncmp(argv[1], "vport=", 6) == 0) {
      strncpy(port_name, argv[1]+6, PORT_NAME_LEN);
      snprintf(port_path, PORT_DIR_LEN, "%s/%s/%s",
               TMP_DIR, VPORT_DIR_PREFIX, port_name);
      printf("port file: %s\n", port_path);
      vport_fp = fopen(port_path, "r");
      if(!fread(&bar_address, 8, 1, vport_fp)) {
        printf("file is empty (or another error)\n");
        rte_exit(EXIT_FAILURE, "Error port file has an issue\n");
      }
      fclose(vport_fp);
      port = from_vbar_addr(bar_address);
      port_connected = 1;

      printf("Server connected to vport with %d queues\n",
             port->bar->num_out_q);
      SHIFT_ARGS;
    }
  }

  if (argc > 1) {
    count_queue = atoi(argv[1]);
    if (count_queue == 0) {
      print_usage();
      return -1;
    }
  }

  if (port_connected == 0) {
    // create a vport
    printf("Createing a vport with %d queues\n", count_queue);
    port = new_vport("server_vport", count_queue, count_queue);
    port_connected = 1;
  }

  // add a listener for SIGINT intrupt
  signal(SIGINT, sig_int_handler);

  // start worker
  worker(port);

  free_vport(port);

  return 0;
}
