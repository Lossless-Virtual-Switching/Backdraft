#include "bkdrft.h"
#include "bkdrft_const.h"
#include "bkdrft_vport.h"
#include "vport.h"
#include "exp.h"
#include "percentile.h"
#include <rte_cycles.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_mbuf.h>

#define BURST_SIZE (32)
#define MAX_DURATION (60)             // (sec)
#define MAX_EXPECTED_LATENCY (100000) // (us)

void print_stats(FILE *fp, uint64_t tp, struct p_hist *hist);
void print_mac(struct rte_ether_addr *addr);

void print_stats(FILE *fp, __attribute__((unused)) uint64_t tp, struct p_hist *hist) {
  float percentile;
  // printf("\033[2J");
  // printf("TP: %lu\n", tp);
  // return;
  // Print latency statistics
  percentile = get_percentile(hist, 0.010);
  fprintf(fp, "Latency [@01](us): %f\n", percentile);
  percentile = get_percentile(hist, 0.500);
  fprintf(fp, "Latency [@50](us): %f\n", percentile);
  percentile = get_percentile(hist, 0.900);
  fprintf(fp, "Latency [@90](us): %f\n", percentile);
  percentile = get_percentile(hist, 0.950);
  fprintf(fp, "Latency [@95](us): %f\n", percentile);
  percentile = get_percentile(hist, 0.990);
  fprintf(fp, "Latency [@99](us): %f\n", percentile);
  percentile = get_percentile(hist, 0.999);
  fprintf(fp, "Latency [@99.9](us): %f\n", percentile);
  percentile = get_percentile(hist, 0.9999);
  fprintf(fp, "Latency [@99.99](us): %f\n", percentile);
  fprintf(fp, "====================================\n");
}

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
  // uint32_t delay_us = cntx->delay_us;
  uint32_t delay_cycles = cntx->delay_cycles;
  double cycles_error = 0; // EWMA
  int system_mode = cntx->system_mode;
  struct rte_mempool *tx_mem_pool = cntx->tx_mem_pool; // just for sending arp
  struct rte_mempool *ctrl_mem_pool = cntx->ctrl_mem_pool;
  uint8_t qid = cntx->default_qid;
  uint32_t count_queues = cntx->count_queues;
  struct rte_ether_addr my_eth = cntx->my_eth;
  uint32_t my_ip = cntx->src_ip;
  uint8_t use_vlan = cntx->use_vlan;
  uint8_t bidi = cntx->bidi;
  FILE *fp = cntx->fp;
  uint32_t q_index = 0;

  struct rte_ether_hdr *eth_hdr;
  /* struct rte_vlan_hdr *vlan_hdr; */
  struct rte_ipv4_hdr *ipv4_hdr;
  struct rte_udp_hdr *udp_hdr;

  uint16_t nb_rx;
  uint16_t nb_tx;
  struct rte_mbuf *rx_bufs[BURST_SIZE];
  int first_pkt = 0; // flag indicating if has received the first packet

  // int throughput[MAX_DURATION];
  const uint64_t hz = rte_get_tsc_hz();
  uint64_t throughput = 0;
  uint64_t start_time;
  uint64_t exp_begin;
  uint64_t current_time;
  // uint64_t current_sec;
  uint64_t last_pkt_time = 0;
  const uint64_t wait_until_exp_begins = 20 * hz; /* cycles */
  const uint64_t termination_threshold = 10 * hz;
  int run = 1;

  uint64_t failed_to_push = 0;

  char *ptr;

  struct p_hist *hist;
  uint64_t ts_offset;
  uint64_t timestamp;
  uint64_t latency;

  uint64_t i;
  struct rte_mbuf *tx_buf[64];
  struct rte_mbuf *buf;
  struct rte_ether_addr tmp_addr;
  uint32_t tmp_ip;
  uint64_t k;

  // TODO: take these parameters from command line
  uint64_t token_limit = 200000;
  uint8_t rate_limit = 0;

  uint8_t cdq = system_mode == system_bkdrft;
  int valid_pkt;

  hist = new_p_hist_from_max_value(MAX_EXPECTED_LATENCY);

  /* check if running on the correct numa node */
  if (port_type == dpdk && rte_eth_dev_socket_id(dpdk_port) > 0 &&
      rte_eth_dev_socket_id(dpdk_port) != (int)rte_socket_id()) {
    printf("Warining port is on remote NUMA node\n");
  }

  /* print the id of managed queues */
  fprintf(fp, "managed queues: ");
  for (uint16_t i = 0; i < count_queues; i++){
    fprintf(fp, "%d ", cntx->managed_queues[i]);
  }
  fprintf(fp, "\n");

  /* timestamp offset from the data section of the packet */
  if (use_vlan) {
    ts_offset = RTE_ETHER_HDR_LEN + sizeof(struct rte_vlan_hdr)
                    + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr);
  } else {
    ts_offset = RTE_ETHER_HDR_LEN + sizeof(struct rte_ipv4_hdr)
                    + sizeof(struct rte_udp_hdr);
  }

  fprintf(fp, "Running server\n");

  exp_begin = rte_get_tsc_cycles();
  start_time = exp_begin;
  /* main worker loop */
  while (run && cntx->running) {
    /* manage the next queue */
    qid = cntx->managed_queues[q_index];
    q_index = (q_index + 1) % count_queues;

    /* get this iteration's timestamp */
    current_time = rte_get_tsc_cycles();

    /* if experiment time has passed kill the server
     * (not all experiments need this)
     * */
    if (!first_pkt && current_time - exp_begin > wait_until_exp_begins) {
      run = 0;
      break;
    }

    /* update throughput */
    if (current_time - start_time > rte_get_tsc_hz()) {
      // print_stats(throughput, hist);
      // if (my_ip == 0xC0A80115)
      //   printf("TP: %lu\n", throughput);
      throughput = 0;
      start_time = current_time;
      // current_sec = 0;
      // printf("failed to push: %ld\n", failed_to_push);
    }

    /* Increase rate limit (TODO: just for a specific experiment) */
    // if (current_time - exp_begin > 20 * rte_get_tsc_hz()) {
    //   token_limit = 400000;
    // }

    if (rate_limit && throughput >= token_limit) {
      continue;
    }

    if (port_type == dpdk) {
      if (system_mode == system_bess) {
        nb_rx = rte_eth_rx_burst(dpdk_port, qid, rx_bufs, BURST_SIZE);
      } else {
        /* send control message on doorbell queue */
        nb_rx =
            poll_ctrl_queue(dpdk_port, BKDRFT_CTRL_QUEUE, BURST_SIZE, rx_bufs, 0);
      }
    } else {
      if (system_mode == system_bkdrft) {
        nb_rx = vport_poll_ctrl_queue(virt_port, BKDRFT_CTRL_QUEUE,
            BURST_SIZE, rx_bufs, 0);
      } else {
        nb_rx = recv_packets_vport(virt_port, qid, (void **)rx_bufs,
                                   BURST_SIZE);
      }
    }

    if (nb_rx == 0) {
      if (first_pkt) {
        /* if client has started sending data
         * and some amount of time has passed since
         * last pkt then server is done
         * */
        if (current_time - last_pkt_time > termination_threshold) {
          // TODO: commented just for experiment
          run = 0;
          break;
        }
      }
      continue;
    }

    /*
    spend some time for the whole batch
    if (delay_us > 0) {
      // rte_delay_us_block(delay_us);
      uint64_t now = rte_get_tsc_cycles();
      uint64_t end =
          rte_get_tsc_cycles() + (delay_us * (rte_get_tsc_hz() / 1000000l));
      while (now < end) {
        now = rte_get_tsc_cycles();
      }
    }
    */

    /* echo packets */
    k = 0;
    for (i = 0; i < nb_rx; i++) {
      buf = rx_bufs[i];

      /* in unidirectional mode the arp is not send so destination mac addr
       * is incorrect
       * */
      if (bidi) {
        if (port_type == dpdk) {
          valid_pkt = check_eth_hdr(my_ip, &my_eth, buf, tx_mem_pool, cdq,
                                    dpdk_port, qid);
        } else {
          valid_pkt = check_eth_hdr_vport(my_ip, &my_eth, buf, tx_mem_pool, cdq,
                                          virt_port, qid);
        }
        if (!valid_pkt) {
          rte_pktmbuf_free(rx_bufs[i]); // free packet
          continue;
        }
      }

      ptr = rte_pktmbuf_mtod_offset(buf, char *, 0);
      eth_hdr = (struct rte_ether_hdr *)ptr;
      uint16_t ether_type = rte_be_to_cpu_16(eth_hdr->ether_type);
      if (use_vlan) {
        if (ether_type != RTE_ETHER_TYPE_VLAN) {
          /* discard the packet (not our packet) */
          rte_pktmbuf_free(rx_bufs[i]);
          continue;
        }
      } else {
        if (ether_type != RTE_ETHER_TYPE_IPV4) {
          /* discard the packet (not our packet) */
          rte_pktmbuf_free(rx_bufs[i]);
          continue;
        }
      }

      if (use_vlan) {
        ipv4_hdr = (struct rte_ipv4_hdr *)(ptr + RTE_ETHER_HDR_LEN +
                                         sizeof(struct rte_vlan_hdr));
      } else {
        ipv4_hdr = (struct rte_ipv4_hdr *)(ptr + RTE_ETHER_HDR_LEN);
      }

      /* if ip address does not match discard the packet */
      if (rte_be_to_cpu_32(ipv4_hdr->dst_addr) != my_ip) {
        rte_pktmbuf_free(rx_bufs[i]);
        continue;
      }

      /* received a valid packet, update the time a valid packet was seen */
      last_pkt_time = current_time;
      if (!first_pkt) {
        first_pkt = 1;
        start_time = current_time;
      }

      /* update throughput */
      throughput += 1;
      // throughput[current_sec] += 1;

      /* apply processing cost */
      if (delay_cycles > 0) {
        // rte_delay_us_block(delay_us);
        uint64_t now = rte_get_tsc_cycles();
        uint64_t end =
            rte_get_tsc_cycles() + delay_cycles;
        while (now < end) {
          now = rte_get_tsc_cycles();
        }
        cycles_error =(now - end) * 0.5 + 0.5 * (cycles_error);
      }

      udp_hdr = (struct rte_udp_hdr *)(ipv4_hdr + 1);

      /* apply procesing cost for a specific flow (TODO: just for experiment) */
      /* uint16_t src_port = rte_be_to_cpu_16(udp_hdr->src_port);
      uint16_t dst_port = rte_be_to_cpu_16(udp_hdr->dst_port);
      if (rte_be_to_cpu_32(ipv4_hdr->dst_addr) == 0x0A0A0104) {
        uint64_t now = rte_get_tsc_cycles();
        uint64_t end = rte_get_tsc_cycles() + 5000;
        while (now < end) {
          now = rte_get_tsc_cycles();
        }
      }*/

      /* counting packet from a source port (just for testing) */
      /*if (src_port > 1000 && src_port < 1032) {
        cntx->tmp_array[0][src_port - 1001]++;
      }*/


      /* get time stamp */
      ptr = ptr + ts_offset;
      timestamp = (*(uint64_t *)ptr);
      /* TODO: the following line only works on single node */
      latency = (rte_get_timer_cycles() - timestamp) * 1000 * 1000 / hz; //(us)
      add_number_to_p_hist(hist, (float)latency);

      if (!bidi) {
        rte_pktmbuf_free(rx_bufs[i]);
        continue;
      }

      /*  swap mac address */
      rte_ether_addr_copy(&eth_hdr->s_addr, &tmp_addr);
      rte_ether_addr_copy(&eth_hdr->d_addr, &eth_hdr->s_addr);
      rte_ether_addr_copy(&tmp_addr, &eth_hdr->d_addr);

      /* swap ip address */
      tmp_ip = ipv4_hdr->src_addr;
      ipv4_hdr->src_addr = ipv4_hdr->dst_addr;
      ipv4_hdr->dst_addr = tmp_ip;

      /* swap port */
      uint16_t tmp = udp_hdr->src_port;
      udp_hdr->src_port = udp_hdr->dst_port;
      udp_hdr->dst_port = tmp;

      /* add to list of echo packets */
      tx_buf[k] = buf;
      k++;
    }

    if (!bidi)
      continue;

    while (k > 0) {
      if (port_type == dpdk) {
        if (system_mode == system_bess) {
          nb_tx = send_pkt(dpdk_port, qid, tx_buf, k, 0, ctrl_mem_pool);
        } else {
          nb_tx = send_pkt(dpdk_port, 1, tx_buf, k, 1, ctrl_mem_pool);
        }
      } else {
        int cdq = system_mode == system_bkdrft;
        nb_tx = vport_send_pkt(virt_port, qid, tx_buf, k, cdq,
                               BKDRFT_CTRL_QUEUE, ctrl_mem_pool);
      }

      /* move packets failed to send to the front of the queue */
      for (i = nb_tx; i < k; i++) {
        tx_buf[i - nb_tx] = tx_buf[i];
        // rte_pktmbuf_free(tx_buf[i]);
      }
      k -= nb_tx;
    }

    failed_to_push += 0; // nothing is failed
  }

  // Print throughput statistics
  // printf("Throughput (pps):\n");
  // for (i = 0; i < current_sec + 1; i++) {
  //   printf("%ld: %d\n", i, throughput[i]);
  // }

  print_stats(fp, 0, hist);
  fprintf(fp, "average cycles error %f\n", cycles_error);
  fprintf(fp, "=================================\n");
  fflush(fp);

  /* free allocated memory */
  free_p_hist(hist);
  cntx->running = 0;
  return 0;
}
