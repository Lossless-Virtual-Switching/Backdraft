#include "include/bkdrft.h"
#include "include/exp.h"
#include "include/percentile.h"
#include <rte_cycles.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_mbuf.h>

#define BURST_SIZE (32)
#define MAX_DURATION (60)             // (sec)
#define MAX_EXPECTED_LATENCY (100000) // (us)

// void print_stats(uint64_t tp, struct p_hist *hist);
void print_mac(struct rte_ether_addr *addr);

// void print_stats(uint64_t tp, struct p_hist *hist) {
//   float percentile;
//   // printf("\033[2J");
//   printf("TP: %lu\n", tp);
//   return;
//   Print latency statistics
//   percentile = get_percentile(hist, 0.500);
//   printf("Latency [@50](us): %f\n", percentile);
//   percentile = get_percentile(hist, 0.900);
//   printf("Latency [@90](us): %f\n", percentile);
//   percentile = get_percentile(hist, 0.950);
//   printf("Latency [@95](us): %f\n", percentile);
//   percentile = get_percentile(hist, 0.990);
//   printf("Latency [@99](us): %f\n", percentile);
//   percentile = get_percentile(hist, 0.999);
//   printf("Latency [@99.9](us): %f\n", percentile);
//   percentile = get_percentile(hist, 0.9999);
//   printf("Latency [@99.99](us): %f\n", percentile);
//   printf("====================================\n");
// }

void print_mac(struct rte_ether_addr *addr) {
  uint8_t *bytes = addr->addr_bytes;
  printf("addr: %x:%x:%x:%x:%x:%x\n", bytes[0], bytes[1], bytes[2], bytes[3],
         bytes[4], bytes[5]);
}

int do_server(void *_cntx) {
  struct context *cntx = (struct context *)_cntx;
  uint32_t port = cntx->port;
  // uint16_t num_queues = cntx->num_queues;
  uint32_t delay_us = cntx->delay_us;
  int system_mode = cntx->system_mode;
  struct rte_mempool *tx_mem_pool = cntx->tx_mem_pool; // just for sending arp 
  struct rte_mempool *ctrl_mem_pool = cntx->ctrl_mem_pool;
  uint8_t qid = cntx->default_qid;
  uint32_t count_queues = cntx->count_queues;
  struct rte_ether_addr my_eth = cntx->my_eth;
  uint32_t my_ip = cntx->src_ip;
  uint8_t use_vlan = cntx->use_vlan;
  uint8_t bidi = cntx->bidi;
  uint32_t q_index = 0;

  struct rte_ether_hdr *eth_hdr;
  // struct rte_vlan_hdr *vlan_hdr;
  // struct rte_ipv4_hdr *ipv4_hdr;
  // struct rte_udp_hdr *udp_hdr;

  uint16_t nb_rx;
  uint16_t nb_tx;
  struct rte_mbuf *rx_bufs[BURST_SIZE];
  int first_pkt = 0; // has received first packet

  // int throughput[MAX_DURATION];
  uint64_t throughput = 0;
  uint64_t start_time;
  uint64_t current_time;
  uint64_t current_sec = 0;
  // uint64_t last_pkt_time = 0;
  // const uint64_t termination_threshold = 5 * rte_get_timer_hz();
  int run = 1;
  struct rte_ipv4_hdr *ipv4_hdr;

  char *ptr;
  // struct p_hist *hist;
  // const uint64_t ts_offset = RTE_ETHER_HDR_LEN + sizeof(struct rte_vlan_hdr)
  //   + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr);
  // uint64_t timestamp;
  // uint64_t latency;
  const uint64_t hz = rte_get_timer_hz();
  uint64_t i;
  struct rte_mbuf *tx_buf[64];
  struct rte_mbuf *buf;
  struct rte_ether_addr tmp_addr;
  uint32_t tmp_ip;
  uint64_t k;
  // float percentile;

  // hist = new_p_hist_from_max_value(MAX_EXPECTED_LATENCY);

  // for (i = 0; i < MAX_DURATION; i++)
  //   throughput[i] = 0;
  //

  uint8_t cdq = system_mode == system_bkdrft;

  if (rte_eth_dev_socket_id(port) > 0 &&
      rte_eth_dev_socket_id(port) != (int)rte_socket_id()) {
    printf("Warining port is on remote NUMA node\n");
  }

  printf("Running server\n");

  while (run) {
    // manage the next queue
    qid = cntx->managed_queues[q_index];
    q_index = (q_index + 1) % count_queues;

    if (system_mode == system_bess) {
      // bess
      nb_rx = rte_eth_rx_burst(port, qid, rx_bufs, BURST_SIZE);
    } else {
      // bkdrft
      nb_rx =
          poll_ctrl_queue(port, BKDRFT_CTRL_QUEUE, BURST_SIZE, rx_bufs, false);
    }

    if (nb_rx == 0) {
      // if (first_pkt) {
      //   // if client has started sending data
      //   // and some amount of time has passed since
      //   // last pkt then server is done
      //   current_time = rte_get_timer_cycles();
      //   if (current_time - last_pkt_time > termination_threshold) {
      //     run = 0;
      //     break;
      //   }
      // }
      continue;
    }
    
    // printf("recv: %d\n", nb_rx);

    if (!first_pkt) {
      printf("Client started!\n");
      // client started working
      first_pkt = 1;
      start_time = rte_get_timer_cycles();
    }

    // update throughput table
    current_time = rte_get_timer_cycles();
    // last_pkt_time = current_time;
    current_sec = (current_time - start_time) / hz;
    if (current_sec > 1) {
      // print_stats(throughput, hist);
      printf("TP: %lu\n", throughput);
      throughput = 0;
      start_time = current_time;
    }

    // spend some time for the whole batch
    if (delay_us > 0) {
      // rte_delay_us_block(delay_us);
      uint64_t now = rte_get_tsc_cycles();
      uint64_t end =
          rte_get_tsc_cycles() + (delay_us * (rte_get_tsc_hz() / 1000000l));
      while (now < end) {
        now = rte_get_tsc_cycles();
      }
    }

    /* echo packets */
    k = 0;
    for (i = 0; i < nb_rx; i++) {
      buf = rx_bufs[i];

      if (!check_eth_hdr(my_ip, &my_eth, buf, tx_mem_pool, cdq, port, qid)) {
        rte_pktmbuf_free(rx_bufs[i]); // free packet
        continue;
      }

      if (!bidi) {
        rte_pktmbuf_free(rx_bufs[i]); // free packet
        continue;
      }

      ptr = rte_pktmbuf_mtod_offset(buf, char *, 0);
      eth_hdr = (struct rte_ether_hdr *)ptr;

      uint16_t ether_type = rte_be_to_cpu_16(eth_hdr->ether_type);
      if (use_vlan) {
        if (ether_type != RTE_ETHER_TYPE_VLAN) {
          // discard the packet (not our packet)
          rte_pktmbuf_free(rx_bufs[i]); // free packet
          continue;
        }
      } else {
        if (ether_type != RTE_ETHER_TYPE_IPV4) {
          // discard the packet (not our packet)
          rte_pktmbuf_free(rx_bufs[i]); // free packet
          continue;
        }
      }

      if (use_vlan) {
        ipv4_hdr = (struct rte_ipv4_hdr *)(ptr + RTE_ETHER_HDR_LEN +
                                         sizeof(struct rte_vlan_hdr));
      } else {
        ipv4_hdr = (struct rte_ipv4_hdr *)(ptr + RTE_ETHER_HDR_LEN);
      }

      // update throughput
      throughput += 1;
      // throughput[current_sec] += 1;

      // swap mac address
      rte_ether_addr_copy(&eth_hdr->s_addr, &tmp_addr);
      rte_ether_addr_copy(&eth_hdr->d_addr, &eth_hdr->s_addr);
      rte_ether_addr_copy(&tmp_addr, &eth_hdr->d_addr);

      // swap ip address
      tmp_ip = ipv4_hdr->src_addr;
      ipv4_hdr->src_addr = ipv4_hdr->dst_addr;
      ipv4_hdr->dst_addr = tmp_ip;

      // swap port
      struct rte_udp_hdr *udp_hdr; 
      udp_hdr = (struct rte_udp_hdr *)(ipv4_hdr + 1);
      uint16_t tmp = udp_hdr->src_port;
      udp_hdr->src_port = udp_hdr->dst_port;
      udp_hdr->dst_port = tmp;

      // add to list of echo packets
      tx_buf[k] = buf;
      k++;

      // get time stamp
      // ptr = ptr + ts_offset;

      // timestamp = (*(uint64_t *)ptr);
      // latency = (rte_get_timer_cycles() - timestamp) * 1000 * 1000 / hz; //
      // (us) printf("%lu\n", latency); add_number_to_p_hist(hist,
      // (float)latency); rte_pktmbuf_free(rx_bufs[i]); // free packet
    }

    if (!bidi)
      continue;

    if (system_mode == system_bess) {
      nb_tx = send_pkt(port, qid, tx_buf, k, false, ctrl_mem_pool);
      // printf("send qid: %d, nb: %d\n", qid, nb_tx);
    } else {
      nb_tx = send_pkt(port, 1, tx_buf, k, true, ctrl_mem_pool);
    }
    // printf("server echo packet: %d", nb_tx);

    // free the packets failed to send
    // rte_pktmbuf_free_bulk(rx_bufs + nb_tx, nb_rx - nb_tx);
    for (i = nb_tx; i < k; i++)
      rte_pktmbuf_free(tx_buf[i]);
  }

  // Print throughput statistics
  // printf("Throughput (pps):\n");
  // for (i = 0; i < current_sec + 1; i++) {
  //   printf("%ld: %d\n", i, throughput[i]);
  // }
  printf("=================================\n");
  cntx->running = 0;
  return 0;
}
