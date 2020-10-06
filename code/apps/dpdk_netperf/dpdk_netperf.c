#include <inttypes.h>
#include <rte_arp.h>
#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_udp.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

#include <time.h>
#include "data_structure/f_linklist.h"
#include "utils/include/bkdrft.h"
#include "utils/include/percentile.h"
#include "utils/include/zipf.h"
#include "utils/include/vport.h"

#define RX_RING_SIZE 128
#define TX_RING_SIZE 128

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32
#define MAX_CORES 64
#define UDP_MAX_PAYLOAD 1472

enum port_type_t {
  dpdk,
  vport
};
typedef enum port_type_t port_type_t;

// #define nnper

static const struct rte_eth_conf port_conf_default = {
    .rxmode =
        {
            .max_rx_pkt_len = RTE_ETHER_MAX_LEN,
            // .offloads = DEV_RX_OFFLOAD_IPV4_CKSUM,
            .offloads = 0x0,
            .mq_mode = ETH_MQ_RX_NONE,
        },
    .rx_adv_conf =
        {
            .rss_conf =
                {
                    .rss_key = NULL,
                    .rss_key_len = 0,
                    // .rss_hf = ETH_RSS_UDP,
                    .rss_hf = 0x0,
                },
        },
    .txmode =
        {
            // .offloads = DEV_TX_OFFLOAD_IPV4_CKSUM | DEV_TX_OFFLOAD_UDP_CKSUM,
            .offloads = 0x0,
            .mq_mode = ETH_MQ_TX_NONE,
        },
};

enum {
  MODE_UDP_CLIENT = 0,
  MODE_UDP_SERVER,
};

#define MAKE_IP_ADDR(a, b, c, d) \
  (((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d)

static unsigned int dpdk_port = 0;
static struct vport *virt_port;
static char port_name[PORT_NAME_LEN];
static port_type_t port_type;

static uint8_t mode;
struct rte_mempool *rx_mbuf_pool;
struct rte_mempool *tx_mbuf_pool;
struct rte_mempool *ctrl_mbuf_pool;
static struct rte_ether_addr my_eth;
static uint32_t my_ip;
static uint32_t server_ip;
static int seconds;
static size_t payload_len;
static unsigned int client_port;
static unsigned int server_port;
static unsigned int num_queues = 8;
struct rte_ether_addr zero_mac = {.addr_bytes = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0}};
struct rte_ether_addr broadcast_mac = {
    .addr_bytes = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};
uint16_t next_port = 50000;
bool bkdrft;
static uint64_t server_delay = 0;

/* dpdk_netperf.c: simple implementation of netperf on DPDK */

static int str_to_ip(const char *str, uint32_t *addr) {
  uint8_t a, b, c, d;
  if (sscanf(str, "%hhu.%hhu.%hhu.%hhu", &a, &b, &c, &d) != 4) {
    return -EINVAL;
  }

  *addr = MAKE_IP_ADDR(a, b, c, d);
  return 0;
}

static int str_to_long(const char *str, long *val) {
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
static int port_init_dpdk(void) {
  uint8_t port = dpdk_port;
  struct rte_mempool *mbuf_pool = rx_mbuf_pool;
  unsigned int n_queues = num_queues;

  struct rte_eth_conf port_conf = port_conf_default;
  const uint16_t rx_rings = n_queues, tx_rings = n_queues;
  uint16_t nb_rxd = RX_RING_SIZE;
  uint16_t nb_txd = TX_RING_SIZE;
  int retval;
  uint16_t q;
  struct rte_eth_dev_info dev_info;
  struct rte_eth_txconf *txconf;

  printf("initializing with %u queues\n", n_queues);

  if (!rte_eth_dev_is_valid_port(port)) return -1;

  /* Configure the Ethernet device. */
  retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
  if (retval != 0) return retval;

  retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
  if (retval != 0) return retval;

  /* Allocate and set up RX queues */
  for (q = 0; q < rx_rings; q++) {
    retval = rte_eth_rx_queue_setup(
        port, q, nb_rxd, rte_eth_dev_socket_id(port), NULL, mbuf_pool);
    if (retval < 0) return retval;
  }

  /* Enable TX offloading */
  rte_eth_dev_info_get(0, &dev_info);
  txconf = &dev_info.default_txconf;

  /* Allocate and set up TX queues */
  for (q = 0; q < tx_rings; q++) {
    retval = rte_eth_tx_queue_setup(port, q, nb_txd,
                                    rte_eth_dev_socket_id(port), txconf);
    if (retval < 0) return retval;
  }

  /* Start the Ethernet port. */
  retval = rte_eth_dev_start(port);
  if (retval < 0) return retval;

  /* Display the port MAC address. */
  rte_eth_macaddr_get(port, &my_eth);
  printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8
         " %02" PRIx8 " %02" PRIx8 "\n",
         (unsigned)port, my_eth.addr_bytes[0], my_eth.addr_bytes[1],
         my_eth.addr_bytes[2], my_eth.addr_bytes[3], my_eth.addr_bytes[4],
         my_eth.addr_bytes[5]);

  /* Enable RX in promiscuous mode for the Ethernet device. */
  rte_eth_promiscuous_enable(port);

  return 0;
}

static int port_init_vport(void)
{
  size_t bar_address;
  FILE *fp;
  char port_path[PORT_DIR_LEN];
  snprintf(port_path, PORT_DIR_LEN, "%s/%s/%s", TMP_DIR,
           VPORT_DIR_PREFIX, port_name);
  fp = fopen(port_path, "r");
  if(fread(&bar_address, 8, 1, fp) == 0) {
    // failed to read vbar address
    return -1;
  }
  fclose(fp);
  virt_port = from_vbar_addr(bar_address);
  return 0;
}

static int port_init(void)
{
  if (port_type == dpdk) {
    return port_init_dpdk();
  } else if (port_type == vport) {
    return port_init_vport();
  }
  return -1;
}

/*
 * Run a client
 */
static void do_client(uint8_t port) {
  uint64_t start_time, end_time;
  uint64_t send_failure = 0;
  uint8_t burst = 32;  // BURST_SIZE;
  struct rte_mbuf *buf;
  struct rte_mbuf *batch[burst];
  char *buf_ptr;
  struct rte_ether_hdr *eth_hdr;
  struct rte_ipv4_hdr *ipv4_hdr;
  // struct bkdrft_ipv4_opt *opt;
  struct rte_udp_hdr *udp_hdr;
  uint16_t nb_tx, i;
  uint64_t reqs = 0;
  struct rte_ether_addr server_eth = {{0x52, 0x54, 0x00, 0x12, 0x00, 0x00}};
  bool setup_port = false;
  /* changing ports */
  int current_queue = 2;
  struct zipfgen *zipf;
  /* 99.9 latency calculation variables */
#ifdef nnper
  uint64_t pkt_start_t, pkt_end_t;
  struct p_hist *hist;
  float pkt_latency;
  float pkt_clatency = 0;
  hist = new_p_hist(60);
#endif
  zipf = new_zipfgen(num_queues - 1, 1);

  /*
   * Check that the port is on the same NUMA node as the polling thread
   * for best performance.
   */
  if (rte_eth_dev_socket_id(port) > 0 &&
      rte_eth_dev_socket_id(port) != (int)rte_socket_id())
    printf(
        "WARNING, port %u is on remote NUMA node to polling thread.\n\t"
        "Performance will not be optimal.\n",
        port);

  printf("\nCore %u running in client mode. [Ctrl+C to quit]\n",
         rte_lcore_id());

  /* run for specified amount of time */
  start_time = rte_get_timer_cycles();
  while (rte_get_timer_cycles() < start_time + seconds * rte_get_timer_hz()) {
    /* t1 */
#ifdef nnper
    pkt_start_t = rte_get_timer_cycles();
#endif

    if (rte_pktmbuf_alloc_bulk(tx_mbuf_pool, batch, burst)) {
      // failed to allocate packet
      continue;
    }

    for (i = 0; i < burst; i++) {
      buf = batch[i];
      /* ethernet header */
      buf_ptr = rte_pktmbuf_append(buf, RTE_ETHER_HDR_LEN);
      assert(buf_ptr);
      eth_hdr = (struct rte_ether_hdr *)buf_ptr;

      rte_ether_addr_copy(&my_eth, &eth_hdr->s_addr);
      rte_ether_addr_copy(&server_eth, &eth_hdr->d_addr);
      eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

      /* vlan header */
      // buf_ptr = rte_pktmbuf_append(buf, sizeof(struct rte_vlan_hdr));
      // assert(buf_ptr);
      // vlan_hdr = (struct rte_vlan_hdr *)buf_ptr;
      // vlan_hdr->vlan_tci = RTE_BE16(tci);
      // vlan_hdr->eth_proto = RTE_BE16(RTE_ETHER_TYPE_IPV4);

      // buf_ptr = rte_pktmbuf_append(buf, sizeof(struct rte_vlan_hdr));
      // vlan_hdr = (struct rte_vlan_hdr *)buf_ptr;
      // vlan_hdr->vlan_tci = tci;
      // vlan_hdr->eth_proto = vlan_ether_proto_type;

      /* IPv4 header */
      buf_ptr = rte_pktmbuf_append(buf, sizeof(struct rte_ipv4_hdr));
      assert(buf_ptr);
      ipv4_hdr = (struct rte_ipv4_hdr *)buf_ptr;
      ipv4_hdr->version_ihl = 0x45;
      // ipv4_hdr->version_ihl = 0x46;
      ipv4_hdr->type_of_service = 3 << 2; // place on queue 3
      ipv4_hdr->total_length =
          rte_cpu_to_be_16(sizeof(struct rte_ipv4_hdr) +
                           sizeof(struct rte_udp_hdr) + payload_len);
      ipv4_hdr->packet_id = 0;
      ipv4_hdr->fragment_offset = 0;
      ipv4_hdr->time_to_live = 64;
      ipv4_hdr->next_proto_id = IPPROTO_UDP;
      ipv4_hdr->hdr_checksum = 1000 + 3;
      ipv4_hdr->src_addr = rte_cpu_to_be_32(my_ip);
      ipv4_hdr->dst_addr = rte_cpu_to_be_32(server_ip);
      // add bkdrft queue option
      // buf_ptr = rte_pktmbuf_append(buf, sizeof(struct bkdrft_ipv4_opt));
      // assert(buf_ptr);
      // opt = (struct bkdrft_ipv4_opt *)buf_ptr;
      // *opt = init_bkdrft_ipv4_opt; // set some default values
      // opt->queue_number = 3;

      /* UDP header + data */
      buf_ptr =
          rte_pktmbuf_append(buf, sizeof(struct rte_udp_hdr) + payload_len);
      assert(buf_ptr);
      udp_hdr = (struct rte_udp_hdr *)buf_ptr;
      udp_hdr->src_port = rte_cpu_to_be_16(client_port);
      udp_hdr->dst_port = rte_cpu_to_be_16(server_port);
      udp_hdr->dgram_len =
          rte_cpu_to_be_16(sizeof(struct rte_udp_hdr) + payload_len);
      udp_hdr->dgram_cksum = 0;
      memset(buf_ptr + sizeof(struct rte_udp_hdr), 0xAB, payload_len);

      buf->l2_len = RTE_ETHER_HDR_LEN;
      buf->l3_len = sizeof(struct rte_ipv4_hdr);
      buf->ol_flags = PKT_TX_IP_CKSUM | PKT_TX_IPV4;
    }

    /* send packet */
    if (port_type == dpdk) {
      if (bkdrft) {
        // current_queue = zipf->gen(zipf);
        current_queue = 1;
        // printf("q: %d\n", current_queue);
        // fflush(stdout);
        ipv4_hdr->type_of_service = current_queue << 2;
        nb_tx = send_pkt(port, current_queue, batch, burst,
                         true, ctrl_mbuf_pool);
      } else {
        // BESS
        nb_tx = send_pkt(port, 0, batch, burst, false, ctrl_mbuf_pool);
      }
    } else {
      // VPort
      // current_queue = zipf->gen(zipf);
      current_queue = 1;
      nb_tx = send_packets_vport(virt_port, current_queue,
                                 (void **)batch, burst);
    }

    reqs += nb_tx;
    send_failure += burst - nb_tx;
    for (i = nb_tx; i < burst; i++) {
      // free failed pkts
      rte_pktmbuf_free(batch[i]);
    }

    /* t2 */
#ifdef nnper
    pkt_end_t = rte_get_timer_cycles();
    pkt_latency = ((float)(pkt_end_t - pkt_start_t) * 1000 * 1000) /
                  (float)rte_get_timer_hz();
    pkt_clatency += pkt_latency;
    add_number_to_p_hist(hist, pkt_latency * 1000);
#endif
  }
  end_time = rte_get_timer_cycles();
  if (setup_port) reqs--;

  free_zipfgen(zipf);

  printf("ran for %f seconds, sent %" PRIu64 " packets\n",
         (float)(end_time - start_time) / rte_get_timer_hz(), reqs);
  printf("client reqs/s: %f\n",
         (float)(reqs * rte_get_timer_hz()) / (end_time - start_time));
#ifdef nnper
  printf("mean latency (us): %f\n", pkt_clatency / reqs);
#else
  printf("mean latency (us): %f\n", (float)(end_time - start_time) * 1000 *
                                        1000 / (reqs * rte_get_timer_hz()));
#endif
  printf("send failures: %" PRIu64 " packets\n", send_failure);

  /* find 99.9 percentile */
#ifdef nnper
  float p;
  p = get_percentile(hist, 0.999);
  printf("99.9 latency (ns): %f\n", p);
#endif
}

/*
 * Run a server
 */
static int do_server(void *arg) {
  uint8_t port = dpdk_port;
  uint8_t queue = (uint64_t)arg;
  struct rte_mbuf *rx_bufs[BURST_SIZE];
  struct rte_mbuf *buf;
  uint16_t nb_rx, i, q;
  uint64_t tput[num_queues];
  uint64_t timestamp, curts;
  uint64_t hz = rte_get_timer_hz();

  printf("on server core with lcore_id: %d, queue: %d", rte_lcore_id(), queue);

  /*
   * Check that the port is on the same NUMA node as the polling thread
   * for best performance.
   */
  if (rte_eth_dev_socket_id(port) > 0 &&
      rte_eth_dev_socket_id(port) != (int)rte_socket_id())
    printf(
        "WARNING, port %u is on remote NUMA node to polling thread.\n\t"
        "Performance will not be optimal.\n",
        port);

  printf("\nCore %u running in server mode. [Ctrl+C to quit]\n",
         rte_lcore_id());

  /* Run until the application is quit or killed. */
  timestamp = 0;
  memset(tput, 0, num_queues * sizeof(*tput));
  q = 0;
  for (;;) {
    curts = rte_get_timer_cycles();
    if (curts - timestamp > hz) {
      for (i = 0; i < num_queues; i++) {
        // printf("Queue: %d, Throughput: %lu\n", i, tput[i]);
        tput[i] = 0;
      }
      // printf("=============\n");
      timestamp = curts;
    }

    /* receive packets */
    if (port_type == dpdk) {
      if (bkdrft) {
        nb_rx = poll_ctrl_queue_expose_qid(port, BKDRFT_CTRL_QUEUE, 32, rx_bufs, true, &q);
      } else {
        nb_rx = rte_eth_rx_burst(port, q, rx_bufs, BURST_SIZE);
        q = (q + 1) % num_queues;
      }
    } else {
      // vport
      nb_rx = recv_packets_vport(virt_port, q, (void**)rx_bufs, BURST_SIZE);
      q = (q + 1) % num_queues;
    }

    if (timestamp == 0) {
      timestamp = curts;
    }
    tput[q] += nb_rx;

    for (i = 0; i < nb_rx; i++) {
      buf = rx_bufs[i];
      rte_pktmbuf_free(buf);
    }
    if (server_delay && nb_rx > 0 ) {
      uint64_t start = rte_get_tsc_cycles();
      uint64_t end = start + (server_delay * rte_get_tsc_hz() / 1000000L);
      uint64_t now = start;
      while (now < end) {
        now = rte_get_tsc_cycles();
      }
    }
  }
  return 0;
}

/*
 * Initialize dpdk.
 */
static int dpdk_init(int argc, char *argv[]) {
  int args_parsed;

  /* Initialize the Environment Abstraction Layer (EAL). */
  args_parsed = rte_eal_init(argc, argv);
  if (args_parsed < 0)
    rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

  /* Check that there is a port to send/receive on. */
  if (!rte_eth_dev_is_valid_port(0))
    rte_exit(EXIT_FAILURE, "Error: no available ports\n");

  /* Creates a new mempool in memory to hold the mbufs. */
  rx_mbuf_pool =
      rte_pktmbuf_pool_create("MBUF_RX_POOL", NUM_MBUFS, MBUF_CACHE_SIZE, 0,
                              RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

  if (rx_mbuf_pool == NULL)
    rte_exit(EXIT_FAILURE, "Cannot create rx mbuf pool\n");

  /* Creates a new mempool in memory to hold the mbufs. */
  tx_mbuf_pool =
      rte_pktmbuf_pool_create("MBUF_TX_POOL", NUM_MBUFS, MBUF_CACHE_SIZE, 0,
                              RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

  if (tx_mbuf_pool == NULL)
    rte_exit(EXIT_FAILURE, "Cannot create tx mbuf pool\n");

  /* Create a new mempool in memory to hold the mbufs. */
  ctrl_mbuf_pool =
      rte_pktmbuf_pool_create("MBUF_CTRL_POOL", NUM_MBUFS, MBUF_CACHE_SIZE, 0,
                              RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

  if (ctrl_mbuf_pool == NULL)
    rte_exit(EXIT_FAILURE, "Cannot crate ctrl mbuf pool\n");

  return args_parsed;
}

static int parse_args(int argc, char *argv[]) {
  long tmp;

  /* argv[0] is still the program name */
  if (argc < 3) {
    printf("not enough arguments left: %d\n", argc);
    return -EINVAL;
  }

  if (strncmp(argv[1], "vport=", 6)) {
    // if starts with vport= then we should use vport
    strncpy(port_name, argv[1] + 6, PORT_NAME_LEN);
    port_type = vport;
    argc--;
    argv++;
  }

  str_to_ip(argv[2], &my_ip);

  if (!strcmp(argv[1], "UDP_CLIENT")) {
    mode = MODE_UDP_CLIENT;
    argc -= 3;
    if (argc < 7) {
      printf("not enough arguments left: %d\n", argc);
      return -EINVAL;
    }
    str_to_ip(argv[3], &server_ip);
    if (sscanf(argv[4], "%u", &client_port) != 1) return -EINVAL;
    if (sscanf(argv[5], "%u", &server_port) != 1) return -EINVAL;
    str_to_long(argv[6], &tmp);
    seconds = tmp;
    str_to_long(argv[7], &tmp);
    payload_len = tmp;
    if (argc > 5 && !strcmp(argv[8], "bkdrft")) {
      bkdrft = true;
    } else {
      bkdrft = false;
    }
    if (sscanf(argv[9], "%u", &num_queues) != 1) return -EINVAL;
  } else if (!strcmp(argv[1], "UDP_SERVER")) {
    mode = MODE_UDP_SERVER;
    argc -= 3;
    if (argc >= 1) {
      if (sscanf(argv[3], "%u", &num_queues) != 1) return -EINVAL;
    }
    if (argc >= 2) {
      bkdrft = !strcmp(argv[4], "bkdrft");
    }
    if (argc >= 3) {
      server_delay = atoi(argv[5]);
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
int main(int argc, char *argv[]) {
  int args_parsed, res, lcore_id;
  uint64_t i;

  /* Initialize dpdk. */
  args_parsed = dpdk_init(argc, argv);

  /* initialize our arguments */
  argc -= args_parsed;
  argv += args_parsed;
  res = parse_args(argc, argv);
  if (res < 0) return res;

  printf("=======================\n");
  printf("is bkdrft: %d\n", bkdrft);
  if (port_type == vport)
    printf("using vport: %s\n", port_name);
  printf("=======================\n");

  /* initialize port */
  if (mode == MODE_UDP_CLIENT && rte_lcore_count() > 1)
    printf("\nWARNING: Too many lcores enabled. Only 1 used.\n");
  if (port_init() != 0)
    rte_exit(EXIT_FAILURE, "Cannot init port %" PRIu8 "\n", dpdk_port);

  if (mode == MODE_UDP_CLIENT)
    do_client(dpdk_port);
  else {
    i = 0;
    RTE_LCORE_FOREACH_SLAVE(lcore_id)
    rte_eal_remote_launch(do_server, (void *)i++, lcore_id);
    do_server((void *)i);
  }

  if (port_type == vport) {
    free_vport(virt_port);
  }

  return 0;
}
