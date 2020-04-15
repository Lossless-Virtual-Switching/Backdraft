#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <rte_arp.h>
#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_ip.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_udp.h>
#include <rte_ether.h>

#include <time.h>
#include "utils/bkdrft.h"
#include "utils/percentile.h"
#include "data_structure/f_linklist.h"

#define RX_RING_SIZE 128
#define TX_RING_SIZE 128

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32
#define MAX_CORES 64
#define UDP_MAX_PAYLOAD 1472

// #define nnper

static const struct rte_eth_conf port_conf_default = {
	.rxmode = {
		.max_rx_pkt_len = RTE_ETHER_MAX_LEN,
		// .offloads = DEV_RX_OFFLOAD_IPV4_CKSUM,
		.offloads = 0x0,
		.mq_mode = ETH_MQ_RX_NONE,
	},
	.rx_adv_conf = {
		.rss_conf = {
			.rss_key = NULL,
			// .rss_hf = ETH_RSS_UDP,
			.rss_hf = 0x0,
		},
	},
	.txmode = {
		// .offloads = DEV_TX_OFFLOAD_IPV4_CKSUM | DEV_TX_OFFLOAD_UDP_CKSUM,
		.offloads = 0x0,
	},
};

uint32_t kMagic = 0x6e626368; // 'nbch'

struct nbench_req {
  uint32_t magic;
  int nports;
};

struct nbench_resp {
  uint32_t magic;
  int nports;
  uint16_t ports[];
};

enum {
	MODE_UDP_CLIENT = 0,
	MODE_UDP_SERVER,
};

#define MAKE_IP_ADDR(a, b, c, d)			\
	(((uint32_t) a << 24) | ((uint32_t) b << 16) |	\
	 ((uint32_t) c << 8) | (uint32_t) d)

static unsigned int dpdk_port = 0;
static uint8_t mode;
struct rte_mempool *rx_mbuf_pool;
struct rte_mempool *tx_mbuf_pool;
static struct rte_ether_addr my_eth;
static uint32_t my_ip;
static uint32_t server_ip;
static int seconds;
static size_t payload_len;
static unsigned int client_port;
static unsigned int server_port;
static unsigned int num_queues = 3;
struct rte_ether_addr zero_mac = {
		.addr_bytes = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0}
};
struct rte_ether_addr broadcast_mac = {
		.addr_bytes = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
};
uint16_t next_port = 50000;

/* dpdk_netperf.c: simple implementation of netperf on DPDK */

static int str_to_ip(const char *str, uint32_t *addr)
{
	uint8_t a, b, c, d;
	if(sscanf(str, "%hhu.%hhu.%hhu.%hhu", &a, &b, &c, &d) != 4) {
		return -EINVAL;
	}

	*addr = MAKE_IP_ADDR(a, b, c, d);
	return 0;
}

static int str_to_long(const char *str, long *val)
{
	char *endptr;

	*val = strtol(str, &endptr, 10);
	if (endptr == str || (*endptr != '\0' && *endptr != '\n') ||
			((*val == LONG_MIN || *val == LONG_MAX) && errno == ERANGE))
		return -EINVAL;
	return 0;
}

/*
 * Initializes a given port using global settings and with the RX buffers
 * coming from the mbuf_pool passed as a parameter.
 */
static inline int
port_init(uint8_t port, struct rte_mempool *mbuf_pool, unsigned int n_queues)
{
	struct rte_eth_conf port_conf = port_conf_default;
	const uint16_t rx_rings = n_queues, tx_rings = n_queues;
	uint16_t nb_rxd = RX_RING_SIZE;
	uint16_t nb_txd = TX_RING_SIZE;
	int retval;
	uint16_t q;
	struct rte_eth_dev_info dev_info;
	struct rte_eth_txconf *txconf;

	printf("initializing with %u queues\n", n_queues);

	if (!rte_eth_dev_is_valid_port(port))
		return -1;

	/* Configure the Ethernet device. */
	retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
	if (retval != 0)
		return retval;

	retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
	if (retval != 0)
		return retval;

	/* Allocate and set up 1 RX queue per Ethernet port. Alireza: Not true 
	 * anymore
	 * */
	for (q = 0; q < rx_rings; q++) {
		retval = rte_eth_rx_queue_setup(port, q, nb_rxd,
                                        rte_eth_dev_socket_id(port), NULL,
                                        mbuf_pool);
		if (retval < 0)
			return retval;
	}

	/* Enable TX offloading */
	rte_eth_dev_info_get(0, &dev_info);
	txconf = &dev_info.default_txconf;

	/* Allocate and set up 1 TX queue per Ethernet port. Alireza: Not true
	 * anymore
	 * */
	for (q = 0; q < tx_rings; q++) {
		retval = rte_eth_tx_queue_setup(port, q, nb_txd,
                                        rte_eth_dev_socket_id(port), txconf);
		if (retval < 0)
			return retval;
	}

	/* Start the Ethernet port. */
	retval = rte_eth_dev_start(port);
	if (retval < 0)
		return retval;

	/* Display the port MAC address. */
	rte_eth_macaddr_get(port, &my_eth);
	printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
				 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
			(unsigned)port,
			my_eth.addr_bytes[0], my_eth.addr_bytes[1],
			my_eth.addr_bytes[2], my_eth.addr_bytes[3],
			my_eth.addr_bytes[4], my_eth.addr_bytes[5]);

	/* Enable RX in promiscuous mode for the Ethernet device. */
	rte_eth_promiscuous_enable(port);

	return 0;
}

/*
 * Send out an arp.
 */
static void send_arp(uint16_t op, struct rte_ether_addr dst_eth, uint32_t dst_ip)
{
	struct rte_mbuf *buf;
	char *buf_ptr;
	struct rte_ether_hdr *eth_hdr;
	struct rte_arp_hdr *a_hdr;
	int nb_tx;

	buf = rte_pktmbuf_alloc(tx_mbuf_pool);
	if (buf == NULL)
		printf("error allocating arp mbuf\n");

	/* ethernet header */
	buf_ptr = rte_pktmbuf_append(buf, RTE_ETHER_HDR_LEN);
	eth_hdr = (struct rte_ether_hdr *) buf_ptr;

	rte_ether_addr_copy(&my_eth, &eth_hdr->s_addr);
	rte_ether_addr_copy(&dst_eth, &eth_hdr->d_addr);
	eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_ARP);

	/* arp header */
	buf_ptr = rte_pktmbuf_append(buf, sizeof(struct rte_arp_hdr));
	a_hdr = (struct rte_arp_hdr *) buf_ptr;
	a_hdr->arp_hardware = rte_cpu_to_be_16(RTE_ARP_HRD_ETHER);
	a_hdr->arp_protocol = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
	a_hdr->arp_hlen = RTE_ETHER_ADDR_LEN;
	a_hdr->arp_plen = 4;
	a_hdr->arp_opcode = rte_cpu_to_be_16(op);

	rte_ether_addr_copy(&my_eth, &a_hdr->arp_data.arp_sha);
	a_hdr->arp_data.arp_sip = rte_cpu_to_be_32(my_ip);
	rte_ether_addr_copy(&dst_eth, &a_hdr->arp_data.arp_tha);
	a_hdr->arp_data.arp_tip = rte_cpu_to_be_32(dst_ip);

	nb_tx = rte_eth_tx_burst(dpdk_port, 0, &buf, 1);
	if (unlikely(nb_tx != 1)) {
		printf("error: could not send arp packet\n");
	}
}

/*
 * Validate this ethernet header. Return true if this packet is for higher
 * layers, false otherwise.
 */
// static bool check_eth_hdr(struct rte_mbuf *buf)
// {
// 	struct rte_ether_hdr *ptr_mac_hdr;
// 	struct rte_arp_hdr *a_hdr;
// 
// 	ptr_mac_hdr = rte_pktmbuf_mtod(buf, struct rte_ether_hdr *);
// 	if (!rte_is_same_ether_addr(&ptr_mac_hdr->d_addr, &my_eth) &&
// 			!rte_is_broadcast_ether_addr(&ptr_mac_hdr->d_addr)) {
// 		/* packet not to our ethernet addr */
// 		return false;
// 	}
// 
// 	if (ptr_mac_hdr->ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_ARP)) {
// 		/* reply to ARP if necessary */
// 		a_hdr = rte_pktmbuf_mtod_offset(buf, struct rte_arp_hdr *,
// 				sizeof(struct rte_ether_hdr));
// 		if (a_hdr->arp_opcode == rte_cpu_to_be_16(RTE_ARP_OP_REQUEST)
// 				&& a_hdr->arp_data.arp_tip == rte_cpu_to_be_32(my_ip))
// 			send_arp(RTE_ARP_OP_REPLY, a_hdr->arp_data.arp_sha,
// 					rte_be_to_cpu_32(a_hdr->arp_data.arp_sip));
// 		return false;
// 	}
// 
// 	if (ptr_mac_hdr->ether_type != rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4))
// 		/* packet not IPv4 */
// 		return false;
// 
// 	return true;
// }

/*
 * Return true if this IP packet is to us and contains a UDP packet,
 * false otherwise.
 */
// static bool check_ip_hdr(struct rte_mbuf *buf)
// {
// 	struct rte_ipv4_hdr *ipv4_hdr;
// 
// 	ipv4_hdr = rte_pktmbuf_mtod_offset(buf, struct rte_ipv4_hdr *,
// 			RTE_ETHER_HDR_LEN);
// 	if (ipv4_hdr->dst_addr != rte_cpu_to_be_32(my_ip)
// 			|| ipv4_hdr->next_proto_id != IPPROTO_UDP)
// 		return false;
// 
// 	return true;
// }

/*
 * Run a netperf client
 */
static void do_client(uint8_t port)
{
	uint64_t start_time, end_time;
	uint64_t send_failure = 0;
	struct rte_mbuf *bufs[BURST_SIZE];
	struct rte_mbuf *buf;
// 	struct rte_mbuf *batch[BURST_SIZE];
	struct rte_ether_hdr *ptr_mac_hdr;
	struct rte_arp_hdr *a_hdr;
	char *buf_ptr;
	struct rte_ether_hdr *eth_hdr;
	struct rte_ipv4_hdr *ipv4_hdr;
	struct rte_udp_hdr *udp_hdr;
	uint16_t nb_tx, nb_rx, i;
	uint64_t reqs = 0;
	struct rte_ether_addr server_eth = {{0x52, 0x54, 0x00, 0x12, 0x00, 0x00}};
	struct nbench_req *control_req;
	bool setup_port = false;
	bool ctrlQ = false;
	/* used for testing things */
// 	uint8_t burst = 1;
// 	int count_pkts_to_send = 65024;
// 	struct timespec delay = {
// 		.tv_sec = 0,
// 		.tv_nsec = 10
// 	};
	/* changing ports */
	int current_queue = 1;
	int change_after_pkts = 1000000; // 1 Mpkt
	fList *next_queue;
	uint64_t next_check_point = change_after_pkts;
	/* 99.9 latency calculation variables */
#ifdef nnper
	uint64_t pkt_start_t, pkt_end_t;
	struct p_hist *hist;
	float pkt_latency;
	float pkt_clatency = 0;
	hist = new_p_hist(60);
#endif

	next_queue = new_flist(32); 
	for (i = 1; i < 8; i++)
		append_flist(next_queue, (float)1);
	
	/*
	 * Check that the port is on the same NUMA node as the polling thread
	 * for best performance.
	 */
	if (rte_eth_dev_socket_id(port) > 0 &&
        rte_eth_dev_socket_id(port) != (int)rte_socket_id())
        printf("WARNING, port %u is on remote NUMA node to polling thread.\n\t"
               "Performance will not be optimal.\n", port);

	printf("\nCore %u running in client mode. [Ctrl+C to quit]\n",
			rte_lcore_id());

	// Alireza
	// server_eth.addr_bytes = {0x52, 0x54, 0x00, 0x12, 0x00, 0x00};
	goto got_mac;

	/* get the mac address of the server via ARP */
	while (true) {
		send_arp(RTE_ARP_OP_REQUEST, broadcast_mac, server_ip);
		sleep(1);

		nb_rx = rte_eth_rx_burst(port, 0, bufs, BURST_SIZE);
		if (nb_rx == 0)
			continue;

		for (i = 0; i < nb_rx; i++) {
			buf = bufs[i];

			ptr_mac_hdr = rte_pktmbuf_mtod(buf, struct rte_ether_hdr *);
			if (!rte_is_same_ether_addr(&ptr_mac_hdr->d_addr, &my_eth)) {
					/* packet not to our ethernet addr */
					continue;
			}

			if (ptr_mac_hdr->ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_ARP)) {
				/* this is an ARP */
				a_hdr = rte_pktmbuf_mtod_offset(buf, struct rte_arp_hdr *,
						sizeof(struct rte_ether_hdr));
				if (a_hdr->arp_opcode == rte_cpu_to_be_16(RTE_ARP_OP_REPLY) &&
						rte_is_same_ether_addr(&a_hdr->arp_data.arp_tha, &my_eth) &&
						a_hdr->arp_data.arp_tip == rte_cpu_to_be_32(my_ip)) {
					/* got a response from server! */
					rte_ether_addr_copy(&a_hdr->arp_data.arp_sha, &server_eth);
					goto got_mac;
				}
			}
		}
	}
got_mac:
	/* run for specified amount of time */
	start_time = rte_get_timer_cycles();
	while (rte_get_timer_cycles() <
			start_time + seconds * rte_get_timer_hz()) {
		/* t1 */
#ifdef nnper
		pkt_start_t = rte_get_timer_cycles();
#endif

		buf = rte_pktmbuf_alloc(tx_mbuf_pool);
		if (buf == NULL) {
			//printf("error allocating batch tx mbuf\n");
			continue;
		}

		/* ethernet header */
		buf_ptr = rte_pktmbuf_append(buf, RTE_ETHER_HDR_LEN);
		eth_hdr = (struct rte_ether_hdr *) buf_ptr;

		rte_ether_addr_copy(&my_eth, &eth_hdr->s_addr);
		rte_ether_addr_copy(&server_eth, &eth_hdr->d_addr);
		eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

		/* IPv4 header */
		buf_ptr = rte_pktmbuf_append(buf, sizeof(struct rte_ipv4_hdr));
		ipv4_hdr = (struct rte_ipv4_hdr *) buf_ptr;
		ipv4_hdr->version_ihl = 0x45;
		ipv4_hdr->type_of_service = 0;
		ipv4_hdr->total_length = rte_cpu_to_be_16(sizeof(struct rte_ipv4_hdr) +
										sizeof(struct rte_udp_hdr) + payload_len);
		ipv4_hdr->packet_id = 0;
		ipv4_hdr->fragment_offset = 0;
		ipv4_hdr->time_to_live = 64;
		ipv4_hdr->next_proto_id = IPPROTO_UDP;
		ipv4_hdr->hdr_checksum = 0;
		ipv4_hdr->src_addr = rte_cpu_to_be_32(my_ip);
		ipv4_hdr->dst_addr = rte_cpu_to_be_32(server_ip);

		/* UDP header + data */
		buf_ptr = rte_pktmbuf_append(buf,
										sizeof(struct rte_udp_hdr) + payload_len);
		udp_hdr = (struct rte_udp_hdr *) buf_ptr;
		udp_hdr->src_port = rte_cpu_to_be_16(client_port);
		udp_hdr->dst_port = rte_cpu_to_be_16(server_port);
		udp_hdr->dgram_len = rte_cpu_to_be_16(sizeof(struct rte_udp_hdr)
										+ payload_len);
		udp_hdr->dgram_cksum = 0;
		memset(buf_ptr + sizeof(struct rte_udp_hdr), 0xAB, payload_len);

		/* control data in case our server is running netbench_udp */
		control_req = (struct nbench_req *) (buf_ptr + sizeof(struct rte_udp_hdr));
		// control_req->magic = kMagic;
		control_req->nports = 1;

		buf->l2_len = RTE_ETHER_HDR_LEN;
		buf->l3_len = sizeof(struct rte_ipv4_hdr);
		buf->ol_flags = PKT_TX_IP_CKSUM | PKT_TX_IPV4;

		/* send packet */
		nb_tx = send_pkt(port, current_queue, &buf, 1, !ctrlQ, tx_mbuf_pool);
// 		nb_tx = rte_eth_tx_burst(port, 2, &buf, 1);
// 		nanosleep(&delay, NULL);

		ctrlQ = true;
		if (nb_tx == 0)
			send_failure++;

		reqs += nb_tx;
// 		if (reqs >= next_check_point) {
// 			current_queue = remove_flist(next_queue, 0);
// 			append_flist(next_queue, current_queue);
// 			ctrlQ = false;
// 		}
		/* t2 */
// 		printf("%ld\r", reqs);
// 		fflush(stdout);
//		if (reqs == count_pkts_to_send) {
//			break;
//		}
#ifdef nnper
		pkt_end_t = rte_get_timer_cycles();
		pkt_latency = ((float)(pkt_end_t - pkt_start_t) * 1000 * 1000) / (float)rte_get_timer_hz();
		pkt_clatency += pkt_latency;
		add_number_to_p_hist(hist, pkt_latency * 1000);
#endif
	}
	end_time = rte_get_timer_cycles();
	if (setup_port)
		reqs--;

	printf("ran for %f seconds, sent %"PRIu64" packets\n",
			(float) (end_time - start_time) / rte_get_timer_hz(), reqs);
	printf("client reqs/s: %f\n",
			(float) (reqs * rte_get_timer_hz()) / (end_time - start_time));
#ifdef nnper
	printf("mean latency (us): %f\n", pkt_clatency / reqs);
#else
  printf("mean latency (us): %f\n", (float) (end_time - start_time) *
  		1000 * 1000 / (reqs * rte_get_timer_hz()));
#endif
	printf("send failures: %"PRIu64" packets\n", send_failure);

	/* find 99.9 percentile */
#ifdef nnper
	float p;
	p = get_percentile(hist, 0.999);
	printf("99.9 latency (ns): %f\n", p);
#endif
}

/*
 * Run a netperf server
 */
static int
do_server(void *arg)
{
	uint8_t port = dpdk_port;
	uint8_t queue = (uint64_t) arg;
	struct rte_mbuf *rx_bufs[BURST_SIZE];
	struct rte_mbuf *buf;
	uint16_t nb_rx, i, q;
	struct rte_ether_hdr *ptr_mac_hdr;

	printf("on server core with lcore_id: %d, queue: %d", rte_lcore_id(),
			queue);

	/*
	 * Check that the port is on the same NUMA node as the polling thread
	 * for best performance.
	 */
	if (rte_eth_dev_socket_id(port) > 0 &&
        rte_eth_dev_socket_id(port) != (int)rte_socket_id())
        printf("WARNING, port %u is on remote NUMA node to polling thread.\n\t"
               "Performance will not be optimal.\n", port);

	printf("\nCore %u running in server mode. [Ctrl+C to quit]\n",
			rte_lcore_id());

	// sleep(10); // wait before starting
	/* Run until the application is quit or killed. */
	for (;;) {
		for (q = 0; q < num_queues; q++) {

			/* receive packets */
			nb_rx = rte_eth_rx_burst(port, q, rx_bufs, BURST_SIZE);

			if (nb_rx == 0)
				continue;

			for (i = 0; i < nb_rx; i++) {
				buf = rx_bufs[i];
				rte_pktmbuf_free(buf);
			}
		}
	}
	return 0;
}

/*
 * Initialize dpdk.
 */
static int dpdk_init(int argc, char *argv[])
{
	int args_parsed;

	/* Initialize the Environment Abstraction Layer (EAL). */
	args_parsed = rte_eal_init(argc, argv);
	if (args_parsed < 0)
		rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

	/* Check that there is a port to send/receive on. */
	if (!rte_eth_dev_is_valid_port(0))
		rte_exit(EXIT_FAILURE, "Error: no available ports\n");

	/* Creates a new mempool in memory to hold the mbufs. */
	rx_mbuf_pool = rte_pktmbuf_pool_create("MBUF_RX_POOL", NUM_MBUFS,
		MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

	if (rx_mbuf_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create rx mbuf pool\n");

	/* Creates a new mempool in memory to hold the mbufs. */
	tx_mbuf_pool = rte_pktmbuf_pool_create("MBUF_TX_POOL", NUM_MBUFS,
		MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

	if (tx_mbuf_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create tx mbuf pool\n");

	return args_parsed;
}

static int parse_netperf_args(int argc, char *argv[])
{
	long tmp;

	/* argv[0] is still the program name */
	if (argc < 3) {
		printf("not enough arguments left: %d\n", argc);
		return -EINVAL;
	}

	str_to_ip(argv[2], &my_ip);

	if (!strcmp(argv[1], "UDP_CLIENT")) {
		mode = MODE_UDP_CLIENT;
		argc -= 3;
		if (argc < 5) {
			printf("not enough arguments left: %d\n", argc);
			return -EINVAL;
		}
		str_to_ip(argv[3], &server_ip);
		if (sscanf(argv[4], "%u", &client_port) != 1)
			return -EINVAL;
		if (sscanf(argv[5], "%u", &server_port) != 1)
			return -EINVAL;
		str_to_long(argv[6], &tmp);
		seconds = tmp;
		str_to_long(argv[7], &tmp);
		payload_len = tmp;
	} else if (!strcmp(argv[1], "UDP_SERVER")) {
		mode = MODE_UDP_SERVER;
		argc -= 3;
		if (argc >= 1) {
			if (sscanf(argv[3], "%u", &num_queues) != 1)
				return -EINVAL;
		}
	} else {
		printf("invalid mode '%s'\n", argv[1]);
		return -EINVAL;
	}

	return 0;
}

/*
 * The main function, which does initialization and calls the per-lcore
 * functions.
 */
int
main(int argc, char *argv[])
{
	int args_parsed, res, lcore_id;
	uint64_t i;

	/* Initialize dpdk. */
	args_parsed = dpdk_init(argc, argv);

	/* initialize our arguments */
	argc -= args_parsed;
	argv += args_parsed;
	res = parse_netperf_args(argc, argv);
	if (res < 0)
		return 0;

	/* initialize port */
	if (mode == MODE_UDP_CLIENT && rte_lcore_count() > 1)
		printf("\nWARNING: Too many lcores enabled. Only 1 used.\n");
	if (port_init(dpdk_port, rx_mbuf_pool, num_queues) != 0)
		rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu8 "\n", dpdk_port);

	if (mode == MODE_UDP_CLIENT)
		do_client(dpdk_port);
	else {
		i = 0;
		RTE_LCORE_FOREACH_SLAVE(lcore_id)
			rte_eal_remote_launch(do_server, (void *) i++, lcore_id);
		do_server((void *) i);
	}

	return 0;
}
