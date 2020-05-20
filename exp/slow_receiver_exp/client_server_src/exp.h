#ifndef __SLOW_RECEIVER_EXP_
#define __SLOW_RECEIVER_EXP_

#include <stdint.h>
#include <rte_mbuf.h>
#include <rte_ether.h>


#define MAKE_IP_ADDR(a, b, c, d)			\
	(((uint32_t) a << 24) | ((uint32_t) b << 16) |	\
	 ((uint32_t) c << 8) | (uint32_t) d)

struct context {
	int mode; // client or server
	int system_mode; // bess or bkdrft

	struct rte_mempool *rx_mem_pool;
	struct rte_mempool *tx_mem_pool;
	struct rte_mempool *ctrl_mem_pool;

	uint32_t port;
	uint16_t num_queues;

	struct rte_ether_addr my_eth;
	uint32_t src_ip;
	uint16_t src_port;

	// server
	uint32_t delay_us;

	// client
	uint32_t *dst_ips;
	int count_dst_ip;
	uint16_t dst_port;
	int payload_length;
};

int str_to_ip(const char *str, uint32_t *addr);

int do_server(struct context *cntx);

int do_client(struct context *cntx);
#endif // __SLOW_RECEIVER_EXP_
