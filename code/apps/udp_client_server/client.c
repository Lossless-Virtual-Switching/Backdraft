#include <assert.h>
#include <unistd.h>

#include <rte_cycles.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_udp.h>

#include "include/bkdrft.h"
#include "include/exp.h"
#include "include/percentile.h"
#include "include/arp.h"

#define BURST_SIZE (32)
#define MAX_EXPECTED_LATENCY (10000) // (us)

inline uint16_t get_tci(uint16_t prio, uint16_t dei, uint16_t vlan_id) {
  return prio << 13 | dei << 12 | vlan_id;
}

// int do_client(struct context *cntx) {
int do_client(void *_cntx) {
  struct context *cntx = (struct context *)_cntx;
  int system_mode = cntx->system_mode;
  int port = cntx->port;
  uint8_t qid = cntx->default_qid;
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
  uint32_t count_flow = cntx->count_flow;
  uint32_t base_port_number = cntx->base_port_number;
  uint8_t use_vlan = cntx->use_vlan;
  uint8_t bidi = cntx->bidi;
  // int num_queues = cntx->num_queues;
  assert(count_dst_ip >= 1);

  uint64_t start_time, end_time;
  uint64_t duration = cntx->duration * rte_get_timer_hz();
  uint64_t ignore_result_duration = 0;

  struct rte_mbuf *bufs[BURST_SIZE];
  // struct rte_mbuf *ctrl_recv_bufs[BURST_SIZE];
  struct rte_mbuf *recv_bufs[BURST_SIZE];
  struct rte_mbuf *buf;
  char *buf_ptr;
  struct rte_ether_hdr *eth_hdr;
  struct rte_vlan_hdr *vlan_hdr;
  struct rte_ipv4_hdr *ipv4_hdr;
  struct rte_udp_hdr *udp_hdr;
  uint16_t nb_tx;
  uint16_t nb_rx2; // nb_rx,
  uint16_t i;
  uint16_t j;
  uint32_t dst_ip;
  uint32_t recv_ip;
  uint64_t timestamp;
  uint64_t latency;
  uint64_t hz;
  int can_send = 1;
  char *ptr;
  uint16_t flow_q[count_dst_ip * count_flow];
  uint16_t selected_q = 0;
  uint16_t rx_q = qid;

  // struct rte_ether_addr _server_eth[2] = {
  //     {{0x98, 0xf2, 0xb3, 0xcc, 0x83, 0x81}},
  //     {{0x98, 0xf2, 0xb3, 0xcc, 0x02, 0xc1}}};
  struct rte_ether_addr _server_eth[count_dst_ip];
  struct rte_ether_addr server_eth;

  int selected_dst = 0;
  int flow = 0;

  // TODO: this will fail for more destinations
  uint16_t prio[count_dst_ip];
  uint16_t _prio = 3;
  uint16_t dei = 0;
  uint16_t vlan_id = 100;
  uint16_t tci;

  struct p_hist **hist;
  int k;
  float percentile;

  // TODO: take rate limit option from config or args, currently it is off
  int rate_limit = 0;
  uint64_t throughput = 0;
  const uint64_t tp_limit = 50000;
  uint64_t tp_start_ts = 0;

  // throughput
  uint64_t total_sent_pkts[count_dst_ip];
  uint64_t total_received_pkts[count_dst_ip];
  uint64_t failed_to_push[count_dst_ip];
  memset(total_sent_pkts, 0, sizeof(uint64_t) * count_dst_ip);
  memset(total_received_pkts, 0, sizeof(uint64_t) * count_dst_ip);
  memset(failed_to_push, 0, sizeof(uint64_t) * count_dst_ip);

  uint8_t cdq = system_mode == system_bkdrft;

  hz = rte_get_timer_hz();

  // create a latency hist for each ip
  hist = malloc(count_dst_ip * sizeof(struct p_hist *));
  for (i = 0; i < count_dst_ip; i++) {
    hist[i] = new_p_hist_from_max_value(MAX_EXPECTED_LATENCY);
  }

  printf("sending on queues: ");
  for (i = 0; i < count_dst_ip * count_flow; i++) {
    // sending each destinations packets on different queue
    // in CDQ mode the queue zero is reserved for later use
    if (cdq)
      flow_q[i] = qid + (i % count_queues);
    else
      flow_q[i] = qid + (i % count_queues);
    printf("%d ", flow_q[i]);
  }
  printf("\n");

  if (rte_eth_dev_socket_id(port) > 0 &&
      rte_eth_dev_socket_id(port) != (int)rte_socket_id()) {
    printf("Warning port is on remote NUMA node\n");
  }

  printf("Client...\n");

  // get dst mac address
  if (bidi) {
    printf("sending ARP requests\n");
    for (int i = 0; i < count_dst_ip; i++) {
      struct rte_ether_addr dst_mac = get_dst_mac(port, qid, src_ip, my_eth,
          dst_ips[i], broadcast_mac, tx_mem_pool, cdq, count_queues);
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
    for (int i = 0; i < count_dst_ip; i++)
      _server_eth[i] = (struct rte_ether_addr) {{0x98, 0xf2, 0xb3, 0xcc, 0x83, 0x81}};
  }
  server_eth = _server_eth[0];

  for (int i = 0; i < count_dst_ip; i++)
    prio[i] = _prio; // TODO: each destination can have a different prio

  dst_port = base_port_number;
  start_time = rte_get_timer_cycles();
  tp_start_ts = start_time;
  for (;;) {
    end_time = rte_get_timer_cycles();
    if (duration > 0 && end_time > start_time + duration) {
      if (can_send) {
        can_send = false;
        start_time = rte_get_timer_cycles();
        duration = 5 * rte_get_timer_hz();
        // wait 10 sec for packets in the returning path
      } else {
        break;
      }
    }

    if (can_send) {

      // rate limit
      uint64_t ts = end_time;
      if (ts - tp_start_ts > hz) {
        throughput = 0;
        tp_start_ts = ts;
      }

      if (throughput > tp_limit) {
        if (rate_limit) {
          goto recv;
        }
      }

      // allocate some packets ! notice they should either be sent or freed
      if (rte_pktmbuf_alloc_bulk(tx_mem_pool, bufs, BURST_SIZE)) {
        // allocating failed
        continue;
      }

      dst_ip = dst_ips[selected_dst];
      selected_q = flow_q[flow];

      server_eth = _server_eth[selected_dst];
      // printf("%x:%x:%x:%x:%x:%x\n",
      //   server_eth.addr_bytes[0],server_eth.addr_bytes[1],server_eth.addr_bytes[2],
      //   server_eth.addr_bytes[3],server_eth.addr_bytes[4],server_eth.addr_bytes[5]);

      tci = get_tci(prio[selected_dst], dei, vlan_id);
      dst_port = dst_port + 1;
      if (dst_port >= base_port_number + count_flow) {
        dst_port = base_port_number;
      }
      k = selected_dst;
      // printf("dst_port: %u\n", dst_port);
      // switching flows per-batch in a round robin fashion
      // TODO: maybe use a stocastic measure for changing the flows
      selected_dst = (selected_dst + 1) % count_dst_ip; // round robin
      flow = (flow + 1) % (count_dst_ip * count_flow); // round robin

      // create a burst for selected flow
      for (int i = 0; i < BURST_SIZE; i++) {
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
        udp_hdr->src_port = rte_cpu_to_be_16(src_port);
        udp_hdr->dst_port = rte_cpu_to_be_16(dst_port);
        udp_hdr->dgram_len =
            rte_cpu_to_be_16(sizeof(struct rte_udp_hdr) + payload_length);
        udp_hdr->dgram_cksum = 0;

        // payload
        // add timestamp
        // TODO: This timestamp is only valid on this machine, not time sycn.
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

      // send packets
      if (system_mode == system_bess) {
        nb_tx = send_pkt(port, selected_q, bufs, BURST_SIZE, false, ctrl_mem_pool);
      } else {
        if (selected_q == 0)
          printf("warning: sending data pkt on queue zero\n");
        nb_tx =
            send_pkt(port, selected_q, bufs, BURST_SIZE, true, ctrl_mem_pool);
      }

      if (end_time > start_time + ignore_result_duration * hz) {
        total_sent_pkts[k] += nb_tx;
        failed_to_push[k] += BURST_SIZE - nb_tx;
      }

      // free packets failed to send
      for (i = nb_tx; i < BURST_SIZE; i++)
        rte_pktmbuf_free(bufs[i]);

      throughput += nb_tx;
    } // if (can_send)

    if (!bidi)
      continue;

    // recv packets
recv:
   for (rx_q = qid; rx_q < qid + count_queues; rx_q++) {
      while (1) {
        if (system_mode == system_bkdrft) {
          // sysmod = bkdrft
          // read ctrl queue (non-blocking)
          nb_rx2 = poll_ctrl_queue(port, BKDRFT_CTRL_QUEUE, BURST_SIZE, recv_bufs,
                                   false);
        } else {
          // bess
          nb_rx2 = rte_eth_rx_burst(port, rx_q, recv_bufs, BURST_SIZE);
          // for (int q = 0; q < 3; q++) {
          //   nb_rx2 = rte_eth_rx_burst(port, q, recv_bufs, BURST_SIZE);
          //   if (nb_rx2 > 0) {
          //    printf("qid: %d\n", q);
          //    break;
          //   }
          // }
        }

        if (nb_rx2 == 0)
          break;

        for (j = 0; j < nb_rx2; j++) {
          buf = recv_bufs[j];
          if (!check_eth_hdr(src_ip, &my_eth, buf, tx_mem_pool, cdq, port, qid)) {
            rte_pktmbuf_free(buf); // free packet
            continue;
          }
          ptr = rte_pktmbuf_mtod(buf, char *);

          eth_hdr = (struct rte_ether_hdr *)ptr;
          if (use_vlan) {
            if (rte_be_to_cpu_16(eth_hdr->ether_type) != RTE_ETHER_TYPE_VLAN) {
              rte_pktmbuf_free(buf); // free packet
              continue;
            }
          } else {
            if (rte_be_to_cpu_16(eth_hdr->ether_type) != RTE_ETHER_TYPE_IPV4) {
              rte_pktmbuf_free(buf); // free packet
              continue;
            }
          }

          // skip the first 20 seconds of the experiment, no percentile info.
          if (end_time < start_time + ignore_result_duration * hz) {
            rte_pktmbuf_free(buf); // free packet
            continue;
          }

          // check ip
        if (use_vlan) {
          ptr = ptr + RTE_ETHER_HDR_LEN + sizeof(struct rte_vlan_hdr);
        } else  {
            ptr = ptr + RTE_ETHER_HDR_LEN;
        }
          ipv4_hdr = (struct rte_ipv4_hdr *)ptr;
          recv_ip = rte_be_to_cpu_32(ipv4_hdr->src_addr);

          // find ip index;
          int found = 0;
          for (k = 0; k < count_dst_ip; k++) {
            if (recv_ip == dst_ips[k]) {
              found = 1;
              break;
            }
          }
          if (found == 0) {
            uint32_t ip = rte_be_to_cpu_32(recv_ip);
            uint8_t *bytes = (uint8_t *)(&ip);
            printf("Ip: %u.%u.%u.%u\n", bytes[0], bytes[1], bytes[2], bytes[3]);
            printf("k not found: qid=%d\n", qid);
            rte_pktmbuf_free(buf); // free packet
            continue;
          }

          // get timestamp
          ptr = ptr + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr);
          timestamp = (*(uint64_t *)ptr);
          latency =
              (rte_get_timer_cycles() - timestamp) * 1000 * 1000 / hz; // (us)
          add_number_to_p_hist(hist[k], (float)latency);
          total_received_pkts[k] += 1;
          rte_pktmbuf_free(buf); // free packet
        }
      }
    }

    // wait
    // rte_delay_us_block(1000); // 1 msec
    // int  t = -1;
    // for (int i = 0; i< 1000000; i++) {
    //   t += 1;
    // }
    // if (t == 0) {
    //   printf("here\n");
    // }
  }

  // write to the output buffer, (it may or may not be stdout)
  fprintf(fp, "=========================\n");
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

  for (k = 0; k < count_dst_ip; k++) {
    uint32_t ip = rte_be_to_cpu_32(dst_ips[k]);
    uint8_t *bytes = (uint8_t *)(&ip);
    fprintf(fp, "Ip: %u.%u.%u.%u\n", bytes[0], bytes[1], bytes[2], bytes[3]);
    fprintf(fp, "Tx: %ld\n", total_sent_pkts[k]);
    fprintf(fp, "Rx: %ld\n", total_received_pkts[k]);
    fprintf(fp, "failed to push: %ld\n", failed_to_push[k]);
  }
  fprintf(fp, "Client done\n");
  fflush(fp);

  cntx->running = 0;
  return 0;
}
