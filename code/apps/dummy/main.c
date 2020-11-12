#include <unistd.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "exp.h"
#include "vport.h"

#define RX_RING_SIZE (512)
#define TX_RING_SIZE (512)

#define NUM_MBUFS (16384)
#define MBUF_CACHE_SIZE (512)
#define PRIV_SIZE 256

#define SHIFT_ARGS {argc--; argv[1] = argv[0]; argv++;};

static unsigned int dpdk_ports[2] = {0,1};

static void print_usage(void)
{
  printf("Notice: This program needs 2 connected ports\n"
         "usage: ./dummy [DPDK EAL arguments] -- [Client arguments]\n"
         "arguments:\n"
         "    * (optional) vport=<name of port>\n"
         "    * count queues\n"
         "    * enable doorbell queue\n"
         "    * cycles per packet\n"
         "    * number of target ips\n"
         "    * [ips+]\n");
}

/*
 * Initialize an ethernet port
 */
static inline int dpdk_port_init(uint8_t port, struct rte_mempool *mbuf_pool,
                            uint16_t queues) {

  struct rte_eth_conf port_conf = {
      .rxmode =
          {
              .max_rx_pkt_len = RTE_ETHER_MAX_LEN,
              .offloads = 0x0,
              .mq_mode = ETH_MQ_RX_NONE,
          },
      .rx_adv_conf =
          {
              .rss_conf =
                  {
                      .rss_key_len = 0,
                      .rss_key = NULL,
                      .rss_hf = 0x0, // ETH_RSS_NONFRAG_IPV4_UDP,
                  },
          },
      .txmode = {
          .offloads = 0x0,
      }};

  int retval;
  const uint16_t rx_rings = queues, tx_rings = queues;

  uint16_t nb_rxd = RX_RING_SIZE,
           nb_txd = TX_RING_SIZE; // number of descriptors
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
    retval = rte_eth_rx_queue_setup(
        port, q, nb_rxd, rte_eth_dev_socket_id(port), NULL, mbuf_pool);
    if (retval != 0)
      return -1;
  }

  // tx allocate queues
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

  // retval = rte_eth_promiscuous_enable(port);
  // if (retval < 0)
  //   return retval;
  return 0;
}

static int vport_init(char *port_name, struct vport **virt_port)
{
  *virt_port = from_vport_name(port_name);
  return *virt_port == NULL;
}

static int dpdk_init(int argc, char *argv[]) {
  int args_parsed;

  args_parsed = rte_eal_init(argc, argv);
  if (args_parsed < 0) {
    print_usage();
    rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
  }

  return args_parsed;
}

static int create_pools(port_type_t port_type,
                        struct rte_mempool **rx_mbuf_pool_1,
                        struct rte_mempool **rx_mbuf_pool_2,
                        struct rte_mempool **tx_mbuf_pool,
                        struct rte_mempool **ctrl_mbuf_pool)
{
  const int name_size = 64;
  long long pid = getpid();
  char pool_name[name_size];

  if (port_type == dpdk) {
    // create mempools
    snprintf(pool_name, name_size, "MUBF_RX_POOL_1_%lld", pid);
    *rx_mbuf_pool_1 =
      rte_pktmbuf_pool_create(pool_name, NUM_MBUFS, MBUF_CACHE_SIZE, PRIV_SIZE,
                                RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (*rx_mbuf_pool_1 == NULL)
      rte_exit(EXIT_FAILURE, "Error can not create rx mbuf pool 1\n");

    // create mempools
    // snprintf(pool_name, name_size, "MUBF_RX_POOL_2_%lld", pid);
    // *rx_mbuf_pool_2 =
    //   rte_pktmbuf_pool_create(pool_name, NUM_MBUFS, MBUF_CACHE_SIZE, PRIV_SIZE,
    //                             RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    // if (*rx_mbuf_pool_2 == NULL)
    //   rte_exit(EXIT_FAILURE, "Error can not create rx mbuf pool 2\n");
  }

  snprintf(pool_name, name_size, "MBUF_TX_POOL_%lld", pid);
  *tx_mbuf_pool =
      rte_pktmbuf_pool_create(pool_name, NUM_MBUFS, MBUF_CACHE_SIZE, PRIV_SIZE,
                              RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

  if (*tx_mbuf_pool == NULL)
    rte_exit(EXIT_FAILURE, "Error can not create tx mbuf pool\n");

  snprintf(pool_name, name_size, "MBUF_CTRL_POOL_%lld", pid);
  *ctrl_mbuf_pool =
      rte_pktmbuf_pool_create(pool_name, NUM_MBUFS, MBUF_CACHE_SIZE, PRIV_SIZE,
                              RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

  if (*ctrl_mbuf_pool == NULL)
    rte_exit(EXIT_FAILURE, "Error can not create tx mbuf pool\n");
}

/*
 * Main function for running a server or client applications
 * the client is a udp flow generator and the server is a
 * echo server.
 * Client calculates the latency of a round trip and reports percentiles.
 * */
int main(int argc, char *argv[]) {
  // number of arguments used by dpdk arg parse
  int args_parsed;

  struct rte_mempool *rx_mbuf_pool_1 = NULL;
  struct rte_mempool *rx_mbuf_pool_2 = NULL;
  struct rte_mempool *tx_mbuf_pool = NULL;
  struct rte_mempool *ctrl_mbuf_pool = NULL;

  // how many queues does the connected Nic/Vhost have
  uint16_t num_queues;
  uint8_t en_cdq = 0; // is doorbell queue enabled

  // list of target ips
  int count_server_ips = 0;
  uint32_t *server_ips = NULL;

  // cycle per target packet
  uint64_t delay_cycles = 0;

  // contex pointers pass to functions
  const int count_cntxs = 1;
  struct context cntxs[count_cntxs];

  /* multi-core */
  // how many cores is allocated to the program (dpdk)
  // default value should be 1.
  // int count_core = rte_lcore_count();
  int count_core = 1;
  printf("Count cores: %d\n", count_core);
  assert(count_core == 1);

  port_type_t port_type = dpdk;
  struct vport *virt_port = NULL; // only used in vport mode
  char port_name[PORT_NAME_LEN] = {}; // name of vport

  int i;

  // let dpdk parse its own arguments
  args_parsed =
      dpdk_init(argc, argv);
  argc -= args_parsed;
  argv += args_parsed;

  // parse program args ======================================
  if (argc < 5) {
    printf("expecting at least 5 arguments\n");
    print_usage();
    rte_exit(EXIT_FAILURE, "Wrong number of arguments");
  }

  // check if vport should be initialized
  if (strncmp(argv[1], "vport=", 6) == 0) {
    // if starts with `vport=`
    strncpy(port_name, argv[1] + 6, PORT_NAME_LEN);
    port_type = vport;
    SHIFT_ARGS;
  }

  // read number of queues
  num_queues = atoi(argv[1]);
  if (num_queues < 1)
    rte_exit(EXIT_FAILURE, "At least one queue is needed");
  printf("Number of queues: %d\n", num_queues);

  // check if cdq is enabled
  if (strncmp(argv[2], "cdq", 3) == 0)
    en_cdq = 1;
  printf("cdq: %d\n", en_cdq);

  // get cycles per packet
  delay_cycles = atol(argv[3]);
  // if (delay_cycles < 0) {
  //   printf("warning: cycles spent per packet is set to negative value."
  //       "It will be ignored\n");
  //   delay_cycles = 0;
  // } else
  if (delay_cycles > 3000) {
    printf("notice: Cycles spent per packet is more than 3000, it is just"
        "a notice!\n");
  }
  printf("Processing cost per packet: %ld cycles\n", delay_cycles);

  count_server_ips = atoi(argv[4]);
  if (count_server_ips < 0) {
    printf("warning: count target server ips are set to negative value."
        "It will be ignored\n");
    count_server_ips = 0;
  }

  printf("Number of target ips: %d\n", count_server_ips);

  if (argc < 5 + count_server_ips) {
    printf("error: Number of server ips does not match with what was given\n");
    print_usage();
    rte_exit(EXIT_FAILURE, "Wrong number of arguments");
  }

  server_ips = malloc(count_server_ips * sizeof(uint32_t));
  for (i = 0; i < count_server_ips; i++) {
    str_to_ip(argv[5 + i], server_ips + i);
  }

  if (en_cdq && num_queues < 2) {
    printf("error: In cdq mode there should be at least 2 queue available!\n");
    rte_exit(EXIT_FAILURE, "Wrong number of queues");
  }

  // check if dpdk port exists
  if (port_type == dpdk && !rte_eth_dev_is_valid_port(0))
    rte_exit(EXIT_FAILURE, "Error no available port\n");

  create_pools(port_type, &rx_mbuf_pool_1, &rx_mbuf_pool_2,
               &tx_mbuf_pool, &ctrl_mbuf_pool);

  if (port_type == dpdk) {
    if (dpdk_port_init(dpdk_ports[0], rx_mbuf_pool_1, num_queues) != 0)
      rte_exit(EXIT_FAILURE, "Cannot init port %hhu\n", dpdk_ports[0]);
    // if (dpdk_port_init(dpdk_ports[1], rx_mbuf_pool_2, num_queues) != 0)
    //   rte_exit(EXIT_FAILURE, "Cannot init port %hhu\n", dpdk_ports[1]);
  } else {
    if (vport_init(port_name, &virt_port)) {
      rte_exit(EXIT_FAILURE, "Failed to init vport\n");
    }
    // rte_exit(EXIT_FAILURE, "Setting up second vport as not been implemented\n");
  }

  // fill context objects ===========================================
  for (int i = 0; i < count_core; i++) {
    cntxs[i].cdq = en_cdq;
    cntxs[i].ptype = port_type;
    cntxs[i].rx_mem_pool = rx_mbuf_pool_1; // is it really needed?
    cntxs[i].tx_mem_pool = tx_mbuf_pool;
    cntxs[i].ctrl_mem_pool = ctrl_mbuf_pool;
    cntxs[i].worker_id = i;
    cntxs[i].dpdk_port_id = dpdk_ports[i];
    cntxs[i].virt_port = virt_port;

    cntxs[i].default_qid = 0; // poll this queue

    cntxs[i].running = 1;     // this job of this cntx has not finished yet
    cntxs[i].num_queues = num_queues; // how many queue port has

    /* how many queue the contex is responsible for */
    // en_cdq = 0;
    cntxs[i].count_queues = num_queues - en_cdq;

    cntxs[i].managed_queues = malloc(num_queues * sizeof(uint32_t));
    for (int q = en_cdq; q < num_queues; q++) {
      cntxs[i].managed_queues[q - en_cdq] = q;
    }

    cntxs[i].delay_cycles = delay_cycles;
    printf("Cycles / Target Packet: %ld\n", delay_cycles);

    cntxs[i].dst_ips = NULL;

    // TODO: fractions are not considered for this division
    int ips = count_server_ips;
    cntxs[i].dst_ips = malloc(sizeof(int) * ips);
    {
      char ip_str[20];
      for (int j = 0; j < ips; j++) {
        cntxs[i].dst_ips[j] = server_ips[j];
        ip_to_str(cntxs[i].dst_ips[j], ip_str, 20);
        printf("ips: %s\n", ip_str);
      }
    }
    cntxs[i].count_dst_ip = ips;
  }

  do_server(&cntxs[0]);
  // do_server(&cntxs[1]);

  // free
  for (int i = 0; i < count_core; i++) {
    free(cntxs[i].managed_queues);
    free(cntxs[i].dst_ips);
  }
  return 0;
}
