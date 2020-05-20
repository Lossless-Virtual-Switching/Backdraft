#include <rte_cycles.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include "exp.h"
#include "bkdrft.h"

#define BURST_SIZE (32)
#define MAX_DURATION (60) // (sec)


int do_server(struct context *cntx)
{
	uint32_t port = cntx->port;
	// uint16_t num_queues = cntx->num_queues;
	uint32_t delay_us = cntx->delay_us;
	int system_mode = cntx->system_mode; struct rte_mempool *ctrl_mem_pool = cntx->ctrl_mem_pool;

	uint16_t i;
	uint16_t nb_rx;
	__attribute__((unused)) uint16_t nb_tx;
	struct rte_mbuf *rx_bufs[BURST_SIZE];
	int first_pkt = 0; // has received first packet

	int throughput[MAX_DURATION];
	uint64_t start_time, current_time, current_sec = 0, last_pkt_time = 0;
	const uint64_t termination_threshold = 10 * rte_get_timer_hz();
	int run = 1;
	struct rte_ipv4_hdr *ipv4_hdr;	

	for (int i = 0; i < MAX_DURATION; i++)
		throughput[i] = 0;

	if (rte_eth_dev_socket_id(port) > 0 &&
		rte_eth_dev_socket_id(port) != (int)rte_socket_id()) {
			printf("Warining port is on remote NUMA node\n");
	}

	printf("Running server\n");

	while(run) {
		if (system_mode == 0) {
			//bess
			nb_rx = rte_eth_rx_burst(port, 0, rx_bufs, BURST_SIZE);
		} else {
			//bkdrft
			nb_rx = poll_ctrl_queue(port, BKDRFT_CTRL_QUEUE, BURST_SIZE, rx_bufs, false);
		}

		if (nb_rx == 0) {
			if (first_pkt) {
				// if client has started sending data
				// and some amount of time has passed since
				// last pkt then server is done
				current_time = rte_get_timer_cycles();
				if (current_time - last_pkt_time > termination_threshold) {
					run = 0;
					break;
				}
			}
			continue;
		}

		if (!first_pkt) {
			// client started working
			first_pkt = 1;
			start_time = rte_get_timer_cycles();
		}

		current_time = rte_get_timer_cycles();
		last_pkt_time = current_time;
		current_sec = (current_time - start_time) / rte_get_timer_hz();
		throughput[current_sec] += nb_rx;

		if (delay_us > 0)
			rte_delay_us_block(delay_us);

		/* echo packets */
		// swap src_ip and dst_ip
		for (i = 0; i <nb_rx; i++) {
			ipv4_hdr = rte_pktmbuf_mtod_offset(rx_bufs[i], struct rte_ipv4_hdr*, RTE_ETHER_HDR_LEN);
			uint32_t tmp_ip = ipv4_hdr->src_addr;
			ipv4_hdr->src_addr = ipv4_hdr->dst_addr;
			ipv4_hdr->dst_addr = tmp_ip;
		}

		if (system_mode == 0) {
			nb_tx = send_pkt(port, 0, rx_bufs, nb_rx, false, ctrl_mem_pool);
		} else {
			nb_tx = send_pkt(port, 1, rx_bufs, nb_rx, true, ctrl_mem_pool);
		}

		// free the packets failed to send
		// rte_pktmbuf_free_bulk(rx_bufs + nb_tx, nb_rx - nb_tx);
		for (i = nb_tx; i < nb_rx; i++)
			rte_pktmbuf_free(rx_bufs[i]);
	}

	printf("Throughput:\n");
	for (uint64_t i=0; i < current_sec + 1; i++) {
		printf("%ld: %d\n", i, throughput[i]);
	}
	printf("=========\n");

	return 0;	
}
