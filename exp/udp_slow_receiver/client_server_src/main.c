#include <string.h>
#include <stdint.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_eal.h>

#include "include/exp.h"


#define RX_RING_SIZE (64)
#define TX_RING_SIZE (64)

#define NUM_MBUFS (8191)
#define MBUF_CACHE_SIZE (250)

static unsigned int dpdk_port = 0;


/*
 * Initialize an ethernet port 
 */
static inline int 
port_init(uint8_t port, struct rte_mempool *mbuf_pool, uint16_t queues, 
	struct rte_ether_addr *my_eth)
{

	struct rte_eth_conf port_conf = {
		.rxmode = {
			.max_rx_pkt_len = RTE_ETHER_MAX_LEN,
			.offloads = 0x0,
			.mq_mode = ETH_MQ_RX_NONE,
		},
		.rx_adv_conf = {
			.rss_conf ={
				.rss_key = NULL,
				.rss_hf = 0x0,
			},
		},
		.txmode = {
			.offloads = 0x0,
		}
	};


	int retval;
	const uint16_t rx_rings = queues, tx_rings = queues;

	uint16_t nb_rxd = RX_RING_SIZE, nb_txd = TX_RING_SIZE;  // number of descriptors
	uint16_t q;

	struct rte_eth_dev_info dev_info;
	struct rte_eth_txconf *txconf;

	printf("Connecting to port %u with %u queues\n", port, queues);
	
	if (!(rte_eth_dev_is_valid_port(port)))
		return -1;


	retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);	
	if (retval != 0)
		return -1;
	
	
	retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
	if (retval != 0)
		return -1;

	// rx allocate queues
	for (q = 0; q < rx_rings; q++) {
		retval = rte_eth_rx_queue_setup(port, q, nb_rxd,
			rte_eth_dev_socket_id(port), NULL, mbuf_pool);
		if (retval != 0)
			return -1;
	}

	//tx allocate queues
	rte_eth_dev_info_get(0, &dev_info);
	txconf = &dev_info.default_txconf;

	for (q = 0; q < tx_rings; q++) {
		retval = rte_eth_tx_queue_setup(port, q, nb_txd,		
			rte_eth_dev_socket_id(port), txconf);
		if (retval != 0)
			return -1;
	}

	// start ethernet port
	retval = rte_eth_dev_start(port);
	if (retval < 0)
		return retval;

	// display port mac address
	rte_eth_macaddr_get(port, my_eth);

	retval = rte_eth_promiscuous_enable(port);
	if (retval < 0)
		return retval;
	return 0;
}

static int dpdk_init(int argc, char *argv[],
	struct rte_mempool **rx_mbuf_pool,
	struct rte_mempool **tx_mbuf_pool,
	struct rte_mempool **ctrl_mbuf_pool)
{
	int args_parsed;

	args_parsed = rte_eal_init(argc, argv);
	if (args_parsed < 0)
		rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
	
	if (!rte_eth_dev_is_valid_port(0))
		rte_exit(EXIT_FAILURE, "Error no available port\n");
	
	// create mempools
	*rx_mbuf_pool = rte_pktmbuf_pool_create("MBUF_RX_POOL", NUM_MBUFS,
		MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
	
	if (*rx_mbuf_pool == NULL)
		rte_exit(EXIT_FAILURE, "Error can not create rx mbuf pool\n");

	*tx_mbuf_pool = rte_pktmbuf_pool_create("MBUF_TX_POOL", NUM_MBUFS,
		MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
	
	if (*tx_mbuf_pool == NULL)
		rte_exit(EXIT_FAILURE, "Error can not create tx mbuf pool\n");

	*ctrl_mbuf_pool = rte_pktmbuf_pool_create("MBUF_CTRL_POOL", NUM_MBUFS,
		MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
	
	if (*ctrl_mbuf_pool == NULL)
		rte_exit(EXIT_FAILURE, "Error can not create tx mbuf pool\n");

	return args_parsed;
}

int main (int argc, char *argv[])
{
	int args_parsed;
	struct rte_mempool *rx_mbuf_pool, *tx_mbuf_pool, *ctrl_mbuf_pool;
	struct rte_ether_addr my_eth;

	uint32_t client_ip;
	uint32_t server_ip;
	int client_port = 5058;
	int server_port = 5057;
	int payload_size = 100;
	int num_queues = 3;
	int mode = 0; // client: 0, server: 1
	int system_mode = 1; // bess: 0, bkdrft: 1
	int count_server_ips, i;
	uint32_t *server_ips;
	unsigned int server_delay = 0;
	struct context cntx;

	str_to_ip("192.168.2.55", &client_ip);
	str_to_ip("192.168.2.10", &server_ip);

	args_parsed = dpdk_init(argc, argv, &rx_mbuf_pool, &tx_mbuf_pool,
		&ctrl_mbuf_pool);

	printf("args parsed: %d\n", args_parsed);

	// parse program args
	argc -= args_parsed;
	argv += args_parsed;

	// check number of remaining args
	if (argc < 3) {
		rte_exit(EXIT_FAILURE, "Wrong number of arguments");
	}

	// system mode
	if (!strcmp(argv[1], "bess")) {
		system_mode = 0;
	} else if (!strcmp(argv[1], "bkdrft")) {
		system_mode = 1;
	} else {
		printf("Error first argument should be either `bess` or `bkdrft`\n");
		return -1;
	}
	
	// mode
	printf("system mode: %s    host: %s\n", argv[1], argv[2]);
	if (!strcmp(argv[2], "client")) {
		// client
		mode = 0;

		if (argc < 5) {
			rte_exit(EXIT_FAILURE, "Wrong number of arguments");
		}

		count_server_ips = atoi(argv[3]);
		if (argc < 4 + count_server_ips) {
			rte_exit(EXIT_FAILURE, "Wrong number of arguments");
		}
		
		server_ips = malloc(count_server_ips * sizeof(uint32_t));
		for (i = 0; i < count_server_ips; i++) {
			str_to_ip(argv[4 + i], server_ips + i);
		}
	} else if (!strcmp(argv[2], "server")) {
		// server
		mode = 1;

		if (argc < 2) {
			rte_exit(EXIT_FAILURE, "wrong number of arguments");
		}
		server_delay = atoi(argv[3]);
	} else {
		printf("Second argument should be `client` or `server`\n");
		return -1;
	}

	if (port_init(dpdk_port, rx_mbuf_pool, num_queues, &my_eth) != 0)
		rte_exit(EXIT_FAILURE, "Cannot init port %hhu\n", dpdk_port);
	
	cntx.mode = mode;
	cntx.system_mode = system_mode;
	cntx.rx_mem_pool = rx_mbuf_pool;
	cntx.tx_mem_pool = tx_mbuf_pool;
	cntx.ctrl_mem_pool = ctrl_mbuf_pool;
	cntx.port = dpdk_port;
	cntx.num_queues = num_queues;
	cntx.my_eth = my_eth;
	cntx.src_ip = client_ip;
	if (mode == 1) {
		cntx.src_ip = server_ip;
		cntx.src_port = server_port;
		cntx.delay_us = server_delay;
		printf("Server delay: %d\n", server_delay);
	} else {
		cntx.src_ip = client_ip;
		cntx.src_port = client_port;
		cntx.dst_ips = server_ips;
		cntx.count_dst_ip = count_server_ips;
		cntx.dst_port = server_port;
		cntx.payload_length = payload_size;
	}

	// free 
	for (int q = 0; q < num_queues; q++) {
		rte_eth_tx_done_cleanup(dpdk_port, q, 0);
	}

	if (mode) {
		do_server(&cntx);
	} else {
		do_client(&cntx);
	}

	// free 
	for (int q = 0; q < num_queues; q++) {
		rte_eth_tx_done_cleanup(dpdk_port, q, 0);
	}
	return 0;
}
