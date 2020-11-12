// libbkdrft.a
#include "bkdrft.h"
#include "bkdrft_const.h"
#include "bkdrft_vport.h"
#include "packet.h"
#include "bkdrft_msg.pb-c.h"
// libbd_vport.a
#include "vport.h"
// include directory
#include "exp.h"

#include <rte_cycles.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_mbuf.h>

#define BURST_SIZE (32)
#define MAX_DURATION (60)             // (sec)
#define MAX_EXPECTED_LATENCY (100000) // (us)

static int _my_select_queue(const int32_t queue_status[], uint64_t *q_ts,
                         int count_queue, uint64_t current_time,
                         uint16_t *selected_q, uint32_t *cnt_pkt)
{
  static int i = 0;
  // register int i = 0;
  register int begin = i;
  do {
    if (queue_status[i] > 0 && q_ts[i] < current_time) {
      // there are some packets
      *selected_q = i;
      *cnt_pkt = queue_status[i];
      return 1;
    }
    // next queue
    i += 1;
    i %= count_queue;
  } while(i != begin);
  return 0;
}

static int _my_vport_poll_ctrl_queue(struct vport *port, uint16_t ctrl_qid,
                                     uint16_t burst,
                                     struct rte_mbuf **recv_bufs,
                                     __attribute__((unused)) int blocking,
                                     uint16_t *data_qid, uint64_t *q_ts,
                                     int count_queues, uint64_t current_time)
{
  /* This function is borrowed from the bkdrft library `bkdrft_vport.c`
   * because we want to override the queue selection logic.
   * At the time of writing this code bkdrft interface does not support
   * overriding internal (simple) queue selection logic
   * */

  // TODO: implement ffs priority queue
  // TODO: move queue_status to the vport struct (maybe)
  static int32_t queue_status[MAX_QUEUES_PER_DIR] = {}; // from vport.h
  const int ctrl_burst = 1; // 512;
  uint16_t nb_ctrl_rx;
  uint16_t nb_data_rx;
  struct rte_mbuf *ctrl_rx_bufs[ctrl_burst];
  struct rte_mbuf *buf;
  uint16_t dqid = -1;
  uint32_t dq_burst;
  // uint8_t msg[BKDRFT_MAX_MESSAGE_SIZE];
  uint8_t *msg;
  char msg_type;
  void *data;
  size_t size;
  int i;
  int queue_found;

poll_doorbell:
  // queue_found = _select_queue(queue_status, MAX_QUEUES_PER_DIR,
  //                             &dqid, &dq_burst);
  queue_found = 0;

  // read a ctrl packet
  nb_ctrl_rx =
      recv_packets_vport(port, ctrl_qid, (void **)ctrl_rx_bufs, ctrl_burst);

  for (i = 0; i < nb_ctrl_rx; i++) {
    buf = ctrl_rx_bufs[i];
    size = get_payload(buf, &data);
    if (unlikely(data == NULL)) {
      // printf("bkdrft.c: payload is null\n");
      // assume it was a corrupt packet
      rte_pktmbuf_free(buf); // free ctrl_pkt
      continue;
    }

    msg = (uint8_t *)data;

    // first byte defines the bkdrft message type
    msg_type = msg[0];
    if (unlikely(msg_type != BKDRFT_CTRL_MSG_TYPE)) {
      // not a ctrl message
      // printf("poll_ctrl_queue...: not a bkdrft ctrl packet!\n");
      rte_pktmbuf_free(buf); // free ctrl_pkt
      continue;
    }
    // unpacking protobuf
    DpdkNetPerf__Ctrl *ctrl_msg =
        dpdk_net_perf__ctrl__unpack(NULL, size - 2, msg + 1);
    if (unlikely(ctrl_msg == NULL)) {
      // printf("Failed to parse ctrl message\n");
      // assume it was a corrupt packet
      // printf("poll_ctrl_queue...: corrupt ctrl message!\n");
      rte_pktmbuf_free(buf); // free ctrl_pkt
      continue;
    }
    dqid = (uint8_t)ctrl_msg->qid;
    dq_burst = (uint16_t)ctrl_msg->nb_pkts;
    // TODO: msg->total_bytes is not used yet!
    // printf("data qid: %d\n", (int)dqid);
    // fflush(stdout);
    dpdk_net_perf__ctrl__free_unpacked(ctrl_msg, NULL);
    rte_pktmbuf_free(buf); // free ctrl_pkt

    if (q_ts[dqid] > current_time) {
      // keep track of packets on each queue
      queue_status[dqid] += dq_burst;
    } else {
      queue_status[dqid] += dq_burst;
      queue_found = 1;
    }
  }

  if (queue_found == 0) {
      queue_found = _my_select_queue(queue_status, q_ts, count_queues,
                                     current_time, &dqid, &dq_burst);
  }

  if (queue_found == 0) return 0;

  // for (int i = 0; i < count_queues; i++)
  //   printf("%d %d\n", i, queue_status[i]);

  if (dq_burst > burst)
    dq_burst = burst; // recv_bufs are limited

  // read data queue
  nb_data_rx = recv_packets_vport(port, dqid, (void **)recv_bufs, burst);
  if (unlikely(nb_data_rx == 0)) {
    // printf("Read data queue %d but no data\n", 1);
    // Warning: chance of infinite loop if corupted ctrl packet notify
    // wrong number of packets in a queue.
    goto poll_doorbell;
  }

  queue_status[dqid] -= nb_data_rx;

  if (data_qid != NULL) {
    *data_qid = dqid;
  }

  return nb_data_rx; // result is in recv_bufs
}


void print_mac(struct rte_ether_addr *addr);

void print_mac(struct rte_ether_addr *addr) {
  uint8_t *bytes = addr->addr_bytes;
  printf("addr: %x:%x:%x:%x:%x:%x\n", bytes[0], bytes[1], bytes[2], bytes[3],
         bytes[4], bytes[5]);
}

int do_server(void *_cntx) {
  struct context *cntx = (struct context *)_cntx;
  port_type_t port_type = cntx->ptype;
  uint32_t dpdk_port = cntx->dpdk_port_id;
  struct vport *virt_port = cntx->virt_port;
  // uint16_t num_queues = cntx->num_queues;
  uint32_t delay_cycles = cntx->delay_cycles;
  double cycles_error = 0; // EWMA
  int cdq = cntx->cdq;
  // struct rte_mempool *tx_mem_pool = cntx->tx_mem_pool;
  struct rte_mempool *ctrl_mem_pool = cntx->ctrl_mem_pool;
  uint8_t qid = cntx->default_qid;
  uint32_t count_queues = cntx->count_queues;
  printf("count queues %d\n", count_queues);
  uint32_t q_index = 0;

  uint32_t *dst_ips = cntx->dst_ips;
  int count_dst_ip = cntx->count_dst_ip;
  int ip_index;

  uint16_t ether_type;
  uint32_t ip_addr;
  struct rte_ether_hdr *eth_hdr;
  // struct rte_vlan_hdr *vlan_hdr;
  struct rte_ipv4_hdr *ipv4_hdr;
  // struct rte_udp_hdr *udp_hdr;

  uint16_t nb_rx;
  uint16_t nb_tx;
  struct rte_mbuf *rx_bufs[BURST_SIZE];

  uint64_t throughput = 0;
  uint64_t start_time;
  uint64_t exp_begin;
  uint64_t current_time;
  int run = 1;

  uint64_t failed_to_push = 0;

  char *ptr;

  uint64_t i;
  struct rte_mbuf *tx_buf[64];
  struct rte_mbuf *buf;
  uint64_t k; // index of packet added to tx_bufs

  // used to calculate the availability timestamp in queue_status
  uint32_t nb_pkts_process;
  // can not read queue until this time stamp
  uint64_t queue_status[count_queues];

  // to which file output should be writen
  FILE *fp = stdout;

  for (i = 0; i < count_queues; i++)
    queue_status[i] = 0;

  /* check if running on the correct numa node */
  if (port_type == dpdk && rte_eth_dev_socket_id(dpdk_port) > 0 &&
      rte_eth_dev_socket_id(dpdk_port) != (int)rte_socket_id()) {
    printf("Warining port is on remote NUMA node\n");
  }

  /* print the id of managed queues */
  fprintf(fp, "managed queues: ");
  for (i = 0; i < count_queues; i++){
    fprintf(fp, "%d ", cntx->managed_queues[i]);
  }
  fprintf(fp, "\n");

  exp_begin = rte_get_tsc_cycles();
  start_time = exp_begin;
  /* main worker loop */
  while (run && cntx->running) {
    /* manage the next queue */
    qid = cntx->managed_queues[q_index];
    q_index = (q_index + 1) % count_queues;

    /* get this iteration's timestamp */
    current_time = rte_get_tsc_cycles();

    /* update throughput */
    if (current_time - start_time > rte_get_tsc_hz()) {
      // printf("tp: %ld\n", throughput);
      throughput = 0;
      start_time = current_time;
      // current_sec = 0;
      // printf("failed to push: %ld\n", failed_to_push);
      // printf("dpdk port: %d\n", dpdk_port);
    }

    // check if this queue is ready to be serviced
    // the condition for CDQ mode is checked while selecting the queue
    // if (!cdq && queue_status[qid] > current_time)
    //   continue;

    // receive some packets
    if (port_type == dpdk) {
      if (cdq) {
        /* recv control message on doorbell queue */
        nb_rx =
            poll_ctrl_queue(dpdk_port, BKDRFT_CTRL_QUEUE,
                BURST_SIZE, rx_bufs, 0);
      } else {
        nb_rx = rte_eth_rx_burst(dpdk_port, qid, rx_bufs, BURST_SIZE);
      }
    } else {
      if (cdq) {
        nb_rx = _my_vport_poll_ctrl_queue(
            virt_port, BKDRFT_CTRL_QUEUE, BURST_SIZE, rx_bufs, 0, NULL,
            queue_status, count_queues, current_time);
      } else {
        nb_rx = recv_packets_vport(virt_port, qid, (void **)rx_bufs,
                                   BURST_SIZE);
      }
    }

    if (nb_rx == 0) {
      // printf("nb_rx: %d, cdq: %d, ptype dpdk: %d\n", nb_rx, cdq, port_type==dpdk);
      continue;
    }

    // nb_tx = send_pkt(dpdk_port, qid, rx_bufs, nb_rx, cdq, ctrl_mem_pool);
    // free ...
    // continue;

    // set the counter to zero, update the time stamp according to the number
    // of target packets found
    nb_pkts_process = 0;

    k = 0;
    for (i = 0; i < nb_rx; i++) {
      buf = rx_bufs[i];

      ptr = rte_pktmbuf_mtod_offset(buf, char *, 0);
      eth_hdr = (struct rte_ether_hdr *)ptr;
      ether_type = rte_be_to_cpu_16(eth_hdr->ether_type);
      if (ether_type == RTE_ETHER_TYPE_IPV4) {
        ipv4_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);

        // look if recognize the ip
        ip_addr = rte_be_to_cpu_32(ipv4_hdr->src_addr);
        for (ip_index = 0; ip_index < count_dst_ip; ip_index++) {
          if (ip_addr == dst_ips[ip_index])
            nb_pkts_process++;
        }
      }

      // update throughput
      throughput += 1;

      // rte_pktmbuf_free(buf);
      // continue;

      // udp_hdr = (struct rte_udp_hdr *)(ipv4_hdr + 1);
      // uint16_t src_port = rte_be_to_cpu_16(udp_hdr->src_port);
      // uint16_t dst_port = rte_be_to_cpu_16(udp_hdr->dst_port);

      // add to list of echo packets
      tx_buf[k] = buf;
      k++;
    }

    // this queue can not be serviced until the deadline is reached
    // queue_status[qid] = rte_get_tsc_cycles() + (nb_pkts_process * delay_cycles);

    while (k > 0) {
      if (port_type == dpdk) {
        nb_tx = send_pkt(dpdk_port, qid, tx_buf, k, cdq, ctrl_mem_pool);
      } else {
        nb_tx = vport_send_pkt(virt_port, qid, tx_buf, k, cdq,
                               BKDRFT_CTRL_QUEUE, ctrl_mem_pool);
      }

      /* move packets failed to send to the front of the queue */
      // TODO: maybe use a ring queue
      for (i = nb_tx; i < k; i++) {
        // tx_buf[i - nb_tx] = tx_buf[i];
        rte_pktmbuf_free(tx_buf[i]);
      }
      // printf("server sent: %d\n", nb_tx);
      // failed_to_push += k - nb_tx;
      k = 0;
      // k -= nb_tx;
      break;
      // if (k!=0)printf("not sending\n");
    }
  }

  // Print throughput statistics
  // printf("Throughput (pps):\n");
  // for (i = 0; i < current_sec + 1; i++) {
  //   printf("%ld: %d\n", i, throughput[i]);
  // }

  fprintf(fp, "failed to push %ld\n", failed_to_push);
  fprintf(fp, "average cycles error %f\n", cycles_error);
  fprintf(fp, "=================================\n");
  fflush(fp);

  cntx->running = 0;
  return 0;
}
