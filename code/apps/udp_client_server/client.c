#include <assert.h>
#include <unistd.h>
#include <pthread.h>
// dpdk
#include <rte_cycles.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_udp.h>
// bkdrft
#include "bkdrft.h"
#include "bkdrft_const.h"
#include "bkdrft_vport.h"
#include "vport.h"
// current project
#include "exp.h"
#include "percentile.h"
#include "arp.h"
#include "zipf.h"

#define BURST_SIZE (32)
#define MAX_EXPECTED_LATENCY (10000) // (us)

// function declarations
void *_run_receiver_thread(void *_arg);

typedef struct {
  int running;
  struct context *cntx;
  uint64_t start_time;
  uint64_t ignore_result_duration;
  struct p_hist **hist;
  uint64_t *total_received_pkts;
} recv_thread_arg_t;

static inline uint16_t get_tci(uint16_t prio, uint16_t dei, uint16_t vlan_id) {
  return prio << 13 | dei << 12 | vlan_id;
}

int do_client(void *_cntx) {
  struct context *cntx = (struct context *)_cntx;
  int system_mode = cntx->system_mode;
  port_type_t port_type = cntx->ptype;
  int dpdk_port = cntx->dpdk_port_id;
  struct vport *virt_port = cntx->virt_port;

  // Testing BD_VPORT:
  // assert(virt_port->bar != NULL);
  // assert(virt_port->bar->pool != NULL);
  // assert(virt_port->bar->queues[1][0].writer_head != NULL);
  // printf("port name %s (%d, %d)\n", virt_port->bar->name, virt_port->bar->num_inc_q, virt_port->bar->num_out_q);
  // printf("%x:%x:%x:%x:%x:%x\n",
  //   virt_port->mac_addr[0],virt_port->mac_addr[1],virt_port->mac_addr[2],
  //   virt_port->mac_addr[3],virt_port->mac_addr[4],virt_port->mac_addr[5]);
  // printf("llsize %d\n", llring_free_count(&virt_port->bar->queues[1][0].writer_head->ring));
  // assert(llring_free_count(&virt_port->bar->queues[1][0].writer_head->ring) > 0);

  uint16_t qid = cntx->default_qid;
  uint16_t count_queues = cntx->count_queues;
  struct rte_mempool *tx_mem_pool = cntx->tx_mem_pool;
  // struct rte_mempool *rx_mem_pool = cntx->rx_mem_pool;
  struct rte_mempool *ctrl_mem_pool = cntx->ctrl_mem_pool;
  struct rte_ether_addr my_eth = cntx->my_eth;
  uint32_t src_ip = cntx->src_ip;
  int src_port = cntx->src_port;
  uint32_t *dst_ips = cntx->dst_ips;
  int count_dst_ip = cntx->count_dst_ip;
  unsigned int dst_port; // = cntx->dst_port;
  int payload_length = cntx->payload_length;
  FILE *fp = cntx->fp;
  // FILE *fp = stdout;
  uint32_t count_flow = cntx->count_flow;
  uint32_t base_port_number = cntx->base_port_number;
  uint8_t use_vlan = cntx->use_vlan;
  uint8_t bidi = cntx->bidi;
  uint64_t delay_cycles = cntx->delay_cycles;
  // int num_queues = cntx->num_queues;
  assert(count_dst_ip >= 1);

  uint64_t start_time, end_time;
  uint64_t duration = (cntx->duration < 0) ? 0 : ((unsigned int)cntx->duration) * rte_get_timer_hz();
  uint64_t ignore_result_duration = 0;

  struct rte_mbuf *bufs[BURST_SIZE];
  // struct rte_mbuf *ctrl_recv_bufs[BURST_SIZE];
  struct rte_mbuf *buf;
  char *buf_ptr;
  struct rte_ether_hdr *eth_hdr;
  struct rte_vlan_hdr *vlan_hdr;
  struct rte_ipv4_hdr *ipv4_hdr;
  struct rte_udp_hdr *udp_hdr;
  uint16_t nb_tx = 0;
  uint16_t i;
  uint32_t dst_ip;
  uint64_t timestamp;
  uint64_t hz;
  int can_send = 1;
  uint16_t flow_q[count_dst_ip * count_flow];
  uint16_t selected_q = 0;

  struct rte_ether_addr _server_eth[count_dst_ip];
  struct rte_ether_addr server_eth;

  int selected_dst = 0;
  int flow = 0;
  int cur_flow = 0;

  uint16_t prio[count_dst_ip];
  uint16_t _prio = 3;
  uint16_t dei = 0;
  uint16_t vlan_id = 100;
  uint16_t tci;

  struct p_hist **hist;
  int k;
  float percentile;

  // TODO: take rate limit option from config or args, currently it is off
  int rate_limit = cntx->rate_limit;
  uint64_t throughput[count_dst_ip * count_flow];
  uint64_t tp_limit = cntx->rate;
  uint64_t tp_start_ts = 0;
  double delta_time;
  double limit_window;

  // throughput
  uint64_t total_sent_pkts[count_dst_ip];
  uint64_t total_received_pkts[count_dst_ip];
  uint64_t failed_to_push[count_dst_ip];
  memset(total_sent_pkts, 0, sizeof(uint64_t) * count_dst_ip);
  memset(total_received_pkts, 0, sizeof(uint64_t) * count_dst_ip);
  memset(failed_to_push, 0, sizeof(uint64_t) * count_dst_ip);

  uint8_t cdq = system_mode == system_bkdrft;

  // hardcoded burst size TODO: get from args
  uint16_t burst_sizes[count_dst_ip];
  uint16_t burst;

  struct zipfgen *dst_zipf;
  struct zipfgen *queue_zipf;

  pthread_t recv_thread;

  hz = rte_get_timer_hz();

  // create a latency hist for each ip
  hist = malloc(count_dst_ip * sizeof(struct p_hist *));
  for (i = 0; i < count_dst_ip; i++) {
    hist[i] = new_p_hist_from_max_value(MAX_EXPECTED_LATENCY);
    burst_sizes[i] = 32;
  }
  burst = burst_sizes[0];

  fprintf(fp, "sending on queues: [%d, %d]\n", qid, qid + count_queues - 1);
  for (i = 0; i < count_dst_ip * count_flow; i++) {
    /* Try to sending each destinations packets on a different queue.
     * In CDQ mode the queue zero is reserved for later use.
     * This mode is only used for uniform queue selection distribution.
     * */
    if (cdq)
      flow_q[i] = qid + (i % count_queues);
    else
      flow_q[i] = qid + (i % count_queues);

    throughput[i] = 0;
  }

  if (port_type == dpdk && rte_eth_dev_socket_id(dpdk_port) > 0 &&
      rte_eth_dev_socket_id(dpdk_port) != (int)rte_socket_id()) {
    printf("Warning port is on remote NUMA node\n");
  }

  // Zipf initialization
  dst_zipf = new_zipfgen(count_dst_ip, 2); // values in range [1, count_dst_ip]
  queue_zipf = new_zipfgen(count_queues, 2); // values in range [1, 2]

  fprintf(fp, "Client src port %d\n", src_port);

  // get dst mac address
  if (bidi) {
    printf("sending ARP requests\n");
    for (int i = 0; i < count_dst_ip; i++) {
      struct rte_ether_addr dst_mac;
      if (port_type == dpdk) {
         dst_mac = get_dst_mac(dpdk_port, qid, src_ip, my_eth,
            dst_ips[i], broadcast_mac, tx_mem_pool, cdq, count_queues);
      } else {
        dst_mac =
            get_dst_mac_vport(virt_port, qid, src_ip, my_eth, dst_ips[i],
                              broadcast_mac, tx_mem_pool, cdq, count_queues);
      }
      _server_eth[i] = dst_mac;
      char ip[20];
      ip_to_str(dst_ips[i], ip, 20);
      printf("mac address for server %s received: ", ip);
      printf("%x:%x:%x:%x:%x:%x\n",
          dst_mac.addr_bytes[0],dst_mac.addr_bytes[1],dst_mac.addr_bytes[2],
          dst_mac.addr_bytes[3],dst_mac.addr_bytes[4],dst_mac.addr_bytes[5]);
    }
    printf("ARP requests finished\n");
  } else {
    // set fake destination mac address
    for (int i = 0; i < count_dst_ip; i++)
      /* 1c:34:da:41:c6:fc */
      _server_eth[i] = (struct rte_ether_addr)
                              {{0x1c, 0x34, 0xda, 0x41, 0xc6, 0xfc}};
  }
  server_eth = _server_eth[0];


  for (int i = 0; i < count_dst_ip; i++)
    prio[i] = _prio; // TODO: each destination can have a different prio

  dst_port = base_port_number;
  start_time = rte_get_timer_cycles();
  tp_start_ts = start_time;

  // start receiver thread
  recv_thread_arg_t recv_arg = {
    .running = 1,
    .cntx = cntx,
    .start_time = start_time,
    .ignore_result_duration = ignore_result_duration,
    .hist = hist,
    .total_received_pkts = total_received_pkts
  };
  if (bidi) {
    pthread_create(&recv_thread, NULL, _run_receiver_thread, &recv_arg);
  }

  // main tx worker loop
  for (;;) {
    end_time = rte_get_timer_cycles();

    // TODO: this is just for testing the switch system
    // if (total_sent_pkts[0] > 1024) break;

    if (duration > 0 && end_time > start_time + duration) {
      if (can_send) {
        can_send = 0;
        start_time = rte_get_timer_cycles();
        duration = 5 * rte_get_timer_hz();
        /* wait 10 sec for packets in the returning path */
      } else {
        break;
      }
    }

    if (can_send) {

      // select destination ip, port and ... =================================
      dst_ip = dst_ips[selected_dst];
      if (cntx->queue_selection_distribution == DIST_ZIPF)
        selected_q = queue_zipf->gen(queue_zipf) - 1;
      else
        selected_q = flow_q[flow];

      server_eth = _server_eth[selected_dst];
      tci = get_tci(prio[selected_dst], dei, vlan_id);
      dst_port = dst_port + 1;
      if (dst_port >= base_port_number + count_flow) {
        dst_port = base_port_number;
      }
      k = selected_dst;
      burst = burst_sizes[selected_dst];

      if (cntx->destination_distribution == DIST_ZIPF) {
        selected_dst = dst_zipf->gen(dst_zipf) - 1;
      } else if (cntx->destination_distribution == DIST_UNIFORM) {
        selected_dst = (selected_dst + 1) % count_dst_ip; // round robin
      }

      // flow = (flow + 1) % (count_dst_ip * count_flow); // round robin
      cur_flow = flow;
      flow = (selected_dst * count_flow) + (dst_port - base_port_number);
      // ===================================================================

      // rate limit
      uint64_t ts = end_time;
      delta_time = ts - tp_start_ts;
      if (delta_time > rte_get_timer_hz()) {
        // printf("tp: %ld\n", throughput);
        for (uint32_t i = 0; i < count_dst_ip * count_flow; i++)
          throughput[i] = 0;
        // throughput = 0;
        tp_start_ts = ts;
      }

      limit_window = tp_limit * (delta_time / (double)(rte_get_timer_hz()));
      if (rate_limit && throughput[cur_flow] > limit_window) {
        continue;
      }

      /* allocate some packets ! notice they should either be sent or freed */
      if (rte_pktmbuf_alloc_bulk(tx_mem_pool, bufs, BURST_SIZE)) {
        /* allocating failed */
        continue;
      }

      // create a burst for selected flow
      for (int i = 0; i < burst; i++) {
        buf = bufs[i];
        // ether header
        buf_ptr = rte_pktmbuf_append(buf, RTE_ETHER_HDR_LEN);
        eth_hdr = (struct rte_ether_hdr *)buf_ptr;

        rte_ether_addr_copy(&my_eth, &eth_hdr->s_addr);
        rte_ether_addr_copy(&server_eth, &eth_hdr->d_addr);
        if (use_vlan) {
          eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_VLAN);
        } else {
          eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
        }

        // vlan header
        if (use_vlan) {
          buf_ptr = rte_pktmbuf_append(buf, sizeof(struct rte_vlan_hdr));
          vlan_hdr = (struct rte_vlan_hdr *)buf_ptr;
          vlan_hdr->vlan_tci = rte_cpu_to_be_16(tci);
          vlan_hdr->eth_proto = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
        }

        // ipv4 header
        buf_ptr = rte_pktmbuf_append(buf, sizeof(struct rte_ipv4_hdr));
        ipv4_hdr = (struct rte_ipv4_hdr *)buf_ptr;
        ipv4_hdr->version_ihl = 0x45;
        ipv4_hdr->type_of_service = 0;
        ipv4_hdr->total_length =
            rte_cpu_to_be_16(sizeof(struct rte_ipv4_hdr) +
                             sizeof(struct rte_udp_hdr) + payload_length);
        ipv4_hdr->packet_id = 0;
        ipv4_hdr->fragment_offset = 0;
        ipv4_hdr->time_to_live = 64;
        ipv4_hdr->next_proto_id = IPPROTO_UDP;
        ipv4_hdr->hdr_checksum = 0;
        ipv4_hdr->src_addr = rte_cpu_to_be_32(src_ip);
        ipv4_hdr->dst_addr = rte_cpu_to_be_32(dst_ip);

        // upd header
        buf_ptr = rte_pktmbuf_append(buf, sizeof(struct rte_udp_hdr) +
                                              payload_length);
        udp_hdr = (struct rte_udp_hdr *)buf_ptr;
        // udp_hdr = (struct rte_udp_hdr *)(ipv4_hdr + 1);

        /* Just for testing */
        /*
        if (src_ip == 0xC0A8010B && cntx->worker_id == 0) {
          // TODO: this is just for experimenting
          uint16_t off = queue_zipf->gen(queue_zipf) - 1;
          udp_hdr->src_port = rte_cpu_to_be_16(1001 + off);
          udp_hdr->dst_port = rte_cpu_to_be_16(60900);
        } else {
          udp_hdr->src_port = rte_cpu_to_be_16(src_port);
          udp_hdr->dst_port = rte_cpu_to_be_16(dst_port);
        } */

        udp_hdr->src_port = rte_cpu_to_be_16(src_port);
        udp_hdr->dst_port = rte_cpu_to_be_16(dst_port);
        udp_hdr->dgram_len =
            rte_cpu_to_be_16(sizeof(struct rte_udp_hdr) + payload_length);
        udp_hdr->dgram_cksum = 0;

        /* payload */
        /* add timestamp */
        // TODO: This timestamp is only valid on this machine, not time sycn.
        // maybe ntp or something similar should be implemented
        // or just send the base time stamp at the begining.
        timestamp = rte_get_timer_cycles();
        *(uint64_t *)(buf_ptr + (sizeof(struct rte_udp_hdr))) = timestamp;

        memset(buf_ptr + sizeof(struct rte_udp_hdr) + sizeof(timestamp), 0xAB,
               payload_length - sizeof(timestamp));

        if (use_vlan) {
          buf->l2_len = RTE_ETHER_HDR_LEN + sizeof(struct rte_vlan_hdr);
        } else {
          buf->l2_len = RTE_ETHER_HDR_LEN;
        }
        buf->l3_len = sizeof(struct rte_ipv4_hdr);
        buf->ol_flags = PKT_TX_IP_CKSUM | PKT_TX_IPV4;
      }

      /* send packets */
      if (port_type == dpdk) {
        nb_tx = send_pkt(dpdk_port, selected_q, bufs, burst, cdq,
                               ctrl_mem_pool);
      } else {
        nb_tx = vport_send_pkt(virt_port, selected_q, bufs, burst, cdq,
                               BKDRFT_CTRL_QUEUE, ctrl_mem_pool);
      }

      if (likely(end_time > start_time + ignore_result_duration * hz)) {
        total_sent_pkts[k] += nb_tx;
        failed_to_push[k] += burst - nb_tx;
      }

      // free packets failed to send
      for (i = nb_tx; i < burst; i++)
        rte_pktmbuf_free(bufs[i]);

      throughput[cur_flow] += nb_tx;

      /* delay between sending each batch */
      if (delay_cycles > 0) {
        // rte_delay_us_block(delay_us);
        uint64_t now = rte_get_tsc_cycles();
        double multiply = ((double)burst / (double)count_flow);
        uint64_t end =
            rte_get_tsc_cycles() + (uint64_t)(delay_cycles * multiply);
        while (now < end) {
          now = rte_get_tsc_cycles();
        }
      }

    } // end if (can_send)
  } // end of tx worker

  if (bidi) {
    // wait for  receive thread to stop
    recv_arg.running = 0;
    pthread_join(recv_thread, NULL);
  }

  /* write to the output buffer, (it may or may not be stdout) */
  fprintf(fp, "=========================\n");
  if (bidi) {
    /* latencies are measured by client only in bidi mode */
    for (k = 0; k < count_dst_ip; k++) {
      uint32_t ip = rte_be_to_cpu_32(dst_ips[k]);
      uint8_t *bytes = (uint8_t *)(&ip);
      fprintf(fp, "Ip: %u.%u.%u.%u\n", bytes[0], bytes[1], bytes[2], bytes[3]);
      percentile = get_percentile(hist[k], 0.01);
      fprintf(fp, "%d latency (1.0): %f\n", k, percentile);
      percentile = get_percentile(hist[k], 0.50);
      fprintf(fp, "%d latency (50.0): %f\n", k, percentile);
      percentile = get_percentile(hist[k], 0.90);
      fprintf(fp, "%d latency (90.0): %f\n", k, percentile);
      percentile = get_percentile(hist[k], 0.95);
      fprintf(fp, "%d latency (95.0): %f\n", k, percentile);
      percentile = get_percentile(hist[k], 0.99);
      fprintf(fp, "%d latency (99.0): %f\n", k, percentile);
      percentile = get_percentile(hist[k], 0.999);
      fprintf(fp, "%d latency (99.9): %f\n", k, percentile);
      percentile = get_percentile(hist[k], 0.9999);
      fprintf(fp, "%d latency (99.99): %f\n", k, percentile);
    }
  }

  for (k = 0; k < count_dst_ip; k++) {
    uint32_t ip = rte_be_to_cpu_32(dst_ips[k]);
    uint8_t *bytes = (uint8_t *)(&ip);
    fprintf(fp, "Ip: %u.%u.%u.%u\n", bytes[0], bytes[1], bytes[2], bytes[3]);
    fprintf(fp, "Tx: %ld\n", total_sent_pkts[k]);
    fprintf(fp, "Rx: %ld\n", total_received_pkts[k]);
    fprintf(fp, "failed to push: %ld\n", failed_to_push[k]);
    fprintf(fp, "warmup time: %ld\n", ignore_result_duration);
  }
  fprintf(fp, "Client done\n");
  fflush(fp);

  /* free allocated memory*/
  for (k = 0; k < count_dst_ip; k++) {
    free_p_hist(hist[k]);
  }
  free(hist);
  free_zipfgen(dst_zipf);
  free_zipfgen(queue_zipf);
  cntx->running = 0;
  return 0;
}

void *_run_receiver_thread(void *_arg)
{
  printf("Receiver thread started\n");
  recv_thread_arg_t *arg = (recv_thread_arg_t *) _arg;
  struct context *cntx = arg->cntx;
  uint64_t start_time = arg->start_time;
  uint64_t ignore_result_duration = arg->ignore_result_duration;
  struct p_hist **hist = arg->hist;
  uint64_t *total_received_pkts = arg->total_received_pkts;

  // context values
  port_type_t port_type = cntx->ptype;
  int system_mode = cntx->system_mode;
  int dpdk_port = cntx->dpdk_port_id;
  int qid = cntx->default_qid;
  int count_queues = cntx->count_queues;
  uint32_t src_ip = cntx->src_ip;
  struct rte_ether_addr my_eth = cntx->my_eth;
  struct vport *virt_port = cntx->virt_port;
  struct rte_mempool *tx_mem_pool = cntx->tx_mem_pool;
  uint8_t use_vlan = cntx->use_vlan;
  int count_dst_ip = cntx->count_dst_ip;
  uint32_t *dst_ips = cntx->dst_ips;
  uint32_t base_port_number = cntx->base_port_number;

  struct rte_mbuf *recv_bufs[BURST_SIZE];
  struct rte_mbuf *buf;
  int nb_rx;
  int rx_q;
  int valid_pkt;
  uint8_t cdq = system_mode == system_bkdrft;
  char *ptr;
  struct rte_ether_hdr *eth_hdr;
  // struct rte_vlan_hdr *vlan_hdr;
  struct rte_ipv4_hdr *ipv4_hdr;
  struct rte_udp_hdr *udp_hdr;
  uint32_t recv_ip;
  int found;
  uint64_t end_time;
  uint64_t timestamp;
  uint64_t latency;
  int k, j;

  while (arg->running) {
    end_time = rte_get_timer_cycles();
    if (system_mode == system_bkdrft) {
      if (port_type == dpdk) {
        /* read ctrl queue and fetch packets from data queue */
        nb_rx = poll_ctrl_queue(dpdk_port, BKDRFT_CTRL_QUEUE, BURST_SIZE,
                                recv_bufs, 0);
      } else {
        nb_rx = vport_poll_ctrl_queue(virt_port, BKDRFT_CTRL_QUEUE,
                                      BURST_SIZE, recv_bufs, 0);
      }
      if (nb_rx == 0)
        continue;
      printf("vport %d\n", nb_rx);

    } else {
      nb_rx = 0;
      for (rx_q = qid; rx_q < qid + count_queues; rx_q++) {
        if (port_type == dpdk) {
          nb_rx = rte_eth_rx_burst(dpdk_port, rx_q, recv_bufs, BURST_SIZE);
        } else {
          nb_rx = recv_packets_vport(virt_port, rx_q, (void **)recv_bufs,
                                      BURST_SIZE);
        }
        if (nb_rx != 0)
          break;
      }
      if (nb_rx == 0) continue;
    }

    for (j = 0; j < nb_rx; j++) {
      buf = recv_bufs[j];
      // rte_pktmbuf_free(buf); // free packet
      // continue;

      if (port_type == dpdk) {
        valid_pkt = check_eth_hdr(src_ip, &my_eth, buf, tx_mem_pool, cdq,
                                  dpdk_port, qid);
      } else {
        valid_pkt = check_eth_hdr_vport(src_ip, &my_eth, buf, tx_mem_pool,
                                        cdq, virt_port, qid);
      }
      if (unlikely(!valid_pkt)) {
        // printf("invalid packet\n");
        rte_pktmbuf_free(buf);
        continue;
      }

      ptr = rte_pktmbuf_mtod(buf, char *);

      eth_hdr = (struct rte_ether_hdr *)ptr;
      if (use_vlan) {
        if (rte_be_to_cpu_16(eth_hdr->ether_type) != RTE_ETHER_TYPE_VLAN) {
          rte_pktmbuf_free(buf);
          continue;
        }
      } else {
        if (rte_be_to_cpu_16(eth_hdr->ether_type) != RTE_ETHER_TYPE_IPV4) {
          rte_pktmbuf_free(buf);
          continue;
        }
      }

      /* skip some seconds of the experiment, and do not record results */
      if (unlikely(end_time <
                   start_time + ignore_result_duration * rte_get_timer_hz())) {
        rte_pktmbuf_free(buf); // free packet
        continue;
      }

      if (use_vlan) {
        ptr = ptr + RTE_ETHER_HDR_LEN + sizeof(struct rte_vlan_hdr);
      } else  {
        ptr = ptr + RTE_ETHER_HDR_LEN;
      }
      ipv4_hdr = (struct rte_ipv4_hdr *)ptr;
      recv_ip = rte_be_to_cpu_32(ipv4_hdr->src_addr);

      /* find ip index */
      found = 0;
      for (k = 0; k < count_dst_ip; k++) {
        if (recv_ip == dst_ips[k]) {
          found = 1;
          break;
        }
      }
      if (unlikely(found == 0)) {
        uint32_t ip = rte_be_to_cpu_32(recv_ip);
        uint8_t *bytes = (uint8_t *)(&ip);
        printf("Ip: %u.%u.%u.%u\n", bytes[0], bytes[1], bytes[2], bytes[3]);
        printf("k not found: qid=%d\n", qid);
        rte_pktmbuf_free(buf); // free packet
        continue;
      }

      /* get timestamp */
      ptr = ptr + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr);
      timestamp = (*(uint64_t *)ptr);
      latency = (rte_get_timer_cycles() - timestamp) * 1000 * 1000 /
                rte_get_timer_hz(); // (us)
      // only measure one flow latency for destination (not aggregate)
      udp_hdr = (struct rte_udp_hdr *)(ipv4_hdr + 1);
      uint16_t src_port = rte_be_to_cpu_16(udp_hdr->src_port);
      // printf("recv dst_port: %d base_port: %d\n", src_port, base_port_number);
      if (src_port == base_port_number)
        add_number_to_p_hist(hist[k], (float)latency);
      total_received_pkts[k] += 1;

      rte_pktmbuf_free(buf); // free packet
    }
  }
}
