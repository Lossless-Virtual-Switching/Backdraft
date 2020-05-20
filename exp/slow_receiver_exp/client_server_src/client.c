#include <assert.h>

#include <rte_cycles.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>

#include "exp.h"
#include "bkdrft.h"
#include "percentile.h"

#define BURST_SIZE (32)
#define MAX_EXPECTED_LATENCY (10000) // (us)

int do_client(struct context *cntx)
{
	int system_mode = cntx->system_mode;
	int port = cntx->port;
	struct rte_mempool *tx_mem_pool = cntx->tx_mem_pool;
	// struct rte_mempool *rx_mem_pool = cntx->rx_mem_pool;
	struct rte_mempool *ctrl_mem_pool = cntx->ctrl_mem_pool;
	struct rte_ether_addr my_eth = cntx->my_eth;
	uint32_t src_ip = cntx->src_ip;
	int src_port = cntx->src_port;
	uint32_t *dst_ips = cntx->dst_ips;
	int count_dst_ip = cntx->count_dst_ip;
	int dst_port = cntx->dst_port;
	int payload_length = cntx->payload_length;
	// int num_queues = cntx->num_queues;

	uint64_t start_time, end_time;
	const uint64_t duration = 10 * rte_get_timer_hz();

	struct rte_mbuf *bufs[BURST_SIZE];
	// struct rte_mbuf *ctrl_recv_bufs[BURST_SIZE];
	struct rte_mbuf *recv_bufs[BURST_SIZE];
	struct rte_mbuf *buf;
	char * buf_ptr;
	struct rte_ether_hdr *eth_hdr;
	struct rte_ipv4_hdr *ipv4_hdr;	
	struct rte_udp_hdr *udp_hdr;
	uint16_t nb_tx, nb_rx2; // nb_rx, 
	uint16_t i, j;
	uint32_t dst_ip;
	uint32_t recv_ip;
	uint64_t timestamp, latency, hz;
	char *ptr;

	struct rte_ether_addr server_eth = {{0x52, 0x54, 0x00, 0x12, 0x00, 0x00}};

	int flow = 0;

	struct p_hist **hist;
	int k;

	hz = rte_get_timer_hz();
	assert(count_dst_ip >= 1);

	// create a latency hist for each ip
	hist = malloc(count_dst_ip * sizeof(struct p_hist *));
	for (i = 0; i < count_dst_ip; i++) {
		hist[i] = new_p_hist_from_max_value(MAX_EXPECTED_LATENCY);
	}

	if (rte_eth_dev_socket_id(port) > 0 &&
		rte_eth_dev_socket_id(port) != (int)rte_socket_id()) {
		printf("Warning port is on remote NUMA node\n");
	}

	printf("Client...\n");


	start_time = rte_get_timer_cycles();
	for(;;) {
		end_time = rte_get_timer_cycles();
		if(end_time > start_time + duration) {
			break;
		}
	
		if (rte_pktmbuf_alloc_bulk(tx_mem_pool, bufs, BURST_SIZE)) {
			// allocating failed
			continue;
		}

		for (i = 0; i < BURST_SIZE; i++) {
			buf = bufs[i];
			// ether header
			buf_ptr = rte_pktmbuf_append(buf, RTE_ETHER_HDR_LEN);
			eth_hdr = (struct rte_ether_hdr *)buf_ptr;

			rte_ether_addr_copy(&my_eth, &eth_hdr->s_addr);
			rte_ether_addr_copy(&server_eth, &eth_hdr->d_addr);
			eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

			// ipv4 header
			dst_ip = dst_ips[flow];
			flow = (flow + 1) % count_dst_ip; // round robin

			buf_ptr = rte_pktmbuf_append(buf, sizeof(struct rte_ipv4_hdr));
			ipv4_hdr = (struct rte_ipv4_hdr *)buf_ptr;
			ipv4_hdr->version_ihl = 0x45;
			ipv4_hdr->type_of_service = 0;
			ipv4_hdr->total_length = rte_cpu_to_be_16(sizeof(struct rte_ipv4_hdr) +
				sizeof(struct rte_udp_hdr) + payload_length);
			ipv4_hdr->packet_id = 0;
			ipv4_hdr->fragment_offset = 0;
			ipv4_hdr->time_to_live = 64;
			ipv4_hdr->next_proto_id = IPPROTO_UDP;
			ipv4_hdr->hdr_checksum = 0;
			ipv4_hdr->src_addr = rte_cpu_to_be_32(src_ip);
			ipv4_hdr->dst_addr = rte_cpu_to_be_32(dst_ip);

			// upd header
			buf_ptr = rte_pktmbuf_append(buf,
					sizeof(struct rte_udp_hdr) + payload_length);
			udp_hdr = (struct rte_udp_hdr *) buf_ptr;
			udp_hdr->src_port = rte_cpu_to_be_16(src_port);
			udp_hdr->dst_port = rte_cpu_to_be_16(dst_port);
			udp_hdr->dgram_len = rte_cpu_to_be_16(sizeof(struct rte_udp_hdr)
									+ payload_length);
			udp_hdr->dgram_cksum = 0;

			// payload
			timestamp = rte_get_timer_cycles();
			// printf("before ts: %ld\n", timestamp);
			*(uint64_t *)(buf_ptr + (sizeof(struct rte_udp_hdr))) = timestamp;

			memset(buf_ptr + sizeof(struct rte_udp_hdr) + sizeof(timestamp),
				0xAB, payload_length - sizeof(timestamp));

			buf->l2_len = RTE_ETHER_HDR_LEN;
			buf->l3_len = sizeof(struct rte_ipv4_hdr);
			buf->ol_flags = PKT_TX_IP_CKSUM | PKT_TX_IPV4;
		}

		// send packets
		if (system_mode == 0) {
			nb_tx = send_pkt(port, 0, bufs, BURST_SIZE, false, ctrl_mem_pool);
		} else {
			nb_tx = send_pkt(port, 1, bufs, BURST_SIZE, true, ctrl_mem_pool);
		}

		// free packets failed to send
		// rte_pktmbuf_free_bulk(bufs + nb_tx, BURST_SIZE - nb_tx);
		for (i = nb_tx; i < BURST_SIZE; i++)
			rte_pktmbuf_free(bufs[i]);

		// recv packets
		while (1) {
			if (system_mode == 1) {
				//sysmod = bkdrft
				// read ctrl queue (non-blocking)
				nb_rx2 = poll_ctrl_queue(port, BKDRFT_CTRL_QUEUE, BURST_SIZE, recv_bufs, false);
			} else {
				// bess
				nb_rx2 = rte_eth_rx_burst(port, 0, recv_bufs, BURST_SIZE);	
			}

			if (nb_rx2 == 0)
				break;

			for (j = 0; j < nb_rx2; j++) {
				buf = recv_bufs[j];
				ptr = rte_pktmbuf_mtod(buf, char *);

				// check ip
				ptr = ptr + RTE_ETHER_HDR_LEN; 
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
					printf("k not found\n");
					continue;
				}

				// get timestamp
				ptr = ptr + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr);
				timestamp = (*(uint64_t *)ptr);
				latency = (rte_get_timer_cycles() - timestamp) * 1000 * 1000 / hz; // (us)
				add_number_to_p_hist(hist[k], (float)latency);
				rte_pktmbuf_free(buf); // free packet
			}
		}

		// wait
		// rte_delay_us_block(100000); // 1 sec
	}

	for (k = 0; k < count_dst_ip; k++) {
		float p = get_percentile(hist[k], 0.999);
		printf("%d latenct (99.9): %f\n", k, p);
	}
	printf("Client done\n");
	fflush(stdout);
	return 0;
}

