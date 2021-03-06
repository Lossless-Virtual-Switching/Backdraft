#include <unistd.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "exp.h"
#include "flow_rules.h"
#include "vport.h"

#define RX_RING_SIZE (512)
#define TX_RING_SIZE (512)

#define NUM_MBUFS (32768)
#define MBUF_CACHE_SIZE (512)
#define PRIV_SIZE 256

#define SHIFT_ARGS {argc--;if(argc>0)argv[1]=argv[0];argv++;};

static unsigned int dpdk_port = 0;


static void print_usage(void)
{
  printf("usage: ./udp_app [DPDK EAL arguments] -- [Client arguments]\n"
  "arguments:\n"
  "    * (optional) bidi=<true/false>\n"
  "    * (optional) vport=<name of port>\n"
  "    * source_ip: ip of host on which app is running (useful for arp)\n"
  "    * number of queue\n"
  "    * system mode: bess or bkdrft\n"
  "    * mode: client or server\n"
  "[client]\n"
  "    * count destination ips\n"
  "    * valid ip values (as many as defined in prev. param)\n"
  "    * count flow\n"
  "    * experiment duration (zero means run with out a limit)\n"
  "    * client port\n"
  "    * client delay cycles\n"
  "    * rate value (pps)\n"
  "[server]\n"
  "    * server delay for each batch\n");
}

/*
 * Initialize an ethernet port
 */
static inline int dpdk_port_init(uint8_t port, struct rte_mempool *mbuf_pool,
                            uint16_t queues, struct rte_ether_addr *my_eth) {

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

  // display port mac address
  rte_eth_macaddr_get(port, my_eth);

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
                        struct rte_mempool **rx_mbuf_pool,
                        struct rte_mempool **tx_mbuf_pool,
                        struct rte_mempool **ctrl_mbuf_pool)
{
  const int name_size = 64;
  long long pid = getpid();
  char pool_name[name_size];

  if (port_type == dpdk) {
    // create mempools
    snprintf(pool_name, name_size, "MUBF_RX_POOL_%lld", pid);
    *rx_mbuf_pool =
      rte_pktmbuf_pool_create(pool_name, NUM_MBUFS, MBUF_CACHE_SIZE, PRIV_SIZE,
                                RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (*rx_mbuf_pool == NULL)
      rte_exit(EXIT_FAILURE, "Error can not create rx mbuf pool\n");
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

// static int setup_pfc(uint16_t port_id)
// {
//   struct rte_eth_fc_conf fc_conf;
//   // struct rte_eth_pfc_conf pfc_conf;
//   int status;
//   status = rte_eth_dev_flow_ctrl_get(port_id, &fc_conf);
//   if (status)
//     return status;
//
//   fc_conf.autoneg = 1;
//   fc_conf.mode = RTE_FC_FULL;
//
//   // pfc_conf.fc = fc_conf;
//   // pfc_conf.priority = 4;
//
//   // status = rte_eth_dev_priority_flow_ctrl_set(port_id, &pfc_conf);
//   status = rte_eth_dev_flow_ctrl_set(port_id, &fc_conf);
//   if (status)
//     return status;
// }
//

/*
 * Main function for running a server or client applications
 * the client is a udp flow generator and the server is a
 * echo server.
 * Client calculates the latency of a round trip and reports percentiles.
 * */
int main(int argc, char *argv[]) {
  // number of arguments used by dpdk arg parse
  int args_parsed;

  struct rte_mempool *rx_mbuf_pool = NULL;
  struct rte_mempool *tx_mbuf_pool = NULL;
  struct rte_mempool *ctrl_mbuf_pool = NULL;

  // mac address of connected port
  struct rte_ether_addr my_eth;

  // these ip and port values are fake
  uint32_t source_ip;
  int client_port = 5058;
  // TODO: this does not generalize to the count cpu
  int server_port[16] = {1001, 5001, 6002, 2002, 3003, 4004, 7007, 8008};

  // how much is the size of udp payload
  // TODO: take message size from arguments
  int payload_size = 64; // 1450; //

  // how many queues does the connected Nic/Vhost have
  uint16_t num_queues = 3;

  // client: 0, server: 1
  int mode = mode_client;

  // bess: 0, bkdrft: 1
  int system_mode = 1;

  // only used in client
  int count_server_ips = 0;
  uint32_t *server_ips = NULL;
  // how many flows should client generate
  // TODO: can limit the number of flows a server accepts
  int count_flow = 0;
  int32_t duration = 40;

  // only used in server
  // delay for each burst in server (us)
  unsigned int server_delay = 0;
  uint64_t delay_cycles = 0;

  // rate limit args
  uint8_t rate_limit = 0; // default off
  uint64_t rate = UINT64_MAX; // default no limit

  // contex pointers pass to functions
  struct context cntxs[20];
  char *output_buffers[20] = {};

  /* flow rule */
  // struct rte_flow *flow;
  // struct rte_flow_error flow_error;

  /* multi-core */
  // how many cores is allocated to the program (dpdk)
  // default value should be 1.
  int count_core = 1;
  int lcore_id;
  int cntxIndex;

  port_type_t port_type = dpdk;
  struct vport *virt_port = NULL;
  char port_name[PORT_NAME_LEN] = {}; // name of vport

  int bidi = 1;

  // struct rte_ether_addr tmp_addr;

  // let dpdk parse its own arguments
  args_parsed =
      dpdk_init(argc, argv);

  // parse program args
  argc -= args_parsed;
  argv += args_parsed;

  if (argc < 5) {
    printf("argc < 5\n");
    print_usage();
    rte_exit(EXIT_FAILURE, "Wrong number of arguments");
  }

  if (strncmp(argv[1], "bidi=", 5) == 0) {
    if (strncmp(argv[1] + 5, "false", 5) == 0) {
      bidi = 0;
    } else if (strncmp(argv[1] + 5, "true", 4) == 0) {
      bidi = 1;
    } else {
      printf("bidi flag value is not recognized\n");
      print_usage();
      return 1;
    }
    SHIFT_ARGS;
  }

  if (strncmp(argv[1], "vport=", 6) == 0) {
    // if starts with `vport=`
    strncpy(port_name, argv[1] + 6, PORT_NAME_LEN - 1);
    port_type = vport;
    SHIFT_ARGS;
  }

  // source_ip
  str_to_ip(argv[1], &source_ip);
  SHIFT_ARGS;

  // read number of queues
  num_queues = atoi(argv[1]);
  if (num_queues < 1)
    rte_exit(EXIT_FAILURE, "At least one queue is needed");
  SHIFT_ARGS;

  // system mode
  if (!strcmp(argv[1], "bess")) {
    system_mode = system_bess;
  } else if (!strcmp(argv[1], "bkdrft")) {
    system_mode = system_bkdrft;
  } else {
    printf("Error first argument should be either `bess` or `bkdrft`\n");
    return -1;
  }

  // mode
  printf("System Mode: %s  Host: %s\n", argv[1], argv[2]);
  if (!strcmp(argv[2], "client")) {
    // client
    mode = mode_client;

    // parse client arguments
    if (argc < 5) {
      print_usage();
      rte_exit(EXIT_FAILURE, "Wrong number of arguments");
    }

    count_server_ips = atoi(argv[3]);
    if (argc < 4 + count_server_ips) {
      rte_exit(EXIT_FAILURE, "Wrong number of arguments");
    }

    server_ips = malloc(count_server_ips * sizeof(uint32_t));
    for (int i = 0; i < count_server_ips; i++) {
      str_to_ip(argv[4 + i], server_ips + i);
    }

    count_flow = atoi(argv[4 + count_server_ips]);
    if (count_flow < 1) {
      rte_exit(EXIT_FAILURE, "number of flows should be at least one");
    }
    printf("Count flows: %d\n", count_flow);

    if (argc > 5 + count_server_ips)
      duration = atoi(argv[5 + count_server_ips]);
    printf("Experiment duration: %d\n", duration);

    if (argc > 6 + count_server_ips)
      client_port = atoi(argv[6 + count_server_ips]);
    printf("Client port: %d\n", client_port);

    if (argc > 7 + count_server_ips)
      delay_cycles = atol(argv[7 + count_server_ips]);
    printf("Client processing between each packet %ld cycles\n", delay_cycles);

    if (argc > 8 + count_server_ips) {
      rate_limit = 1;
      rate = atol(argv[8 + count_server_ips]);
    }
    if (rate_limit)
      printf("Client rate limit is on rate: %ld\n", rate);
    else
      printf("Client rate limit is off\n");

  } else if (!strcmp(argv[2], "server")) {
    // server
    mode = mode_server;

    // parse server arguments
    if (argc < 2) {
      rte_exit(EXIT_FAILURE, "wrong number of arguments");
      print_usage();
    }
    server_delay = atoi(argv[3]);
  } else {
    printf("Second argument should be `client` or `server`\n");
    return -1;
  }

  if (mode == mode_client) {
    // client does not support multi-processing (need to be investigated and
    // tested again) there are so many changes since the multiprocess feature
    // was added.
    count_core = 1;
  } else {
    count_core = rte_lcore_count();
  }
  printf("Count core: %d\n", count_core);

  if (count_core > num_queues) {
    printf("count core is more than available queues\n");
    return -1;
  }

  if (system_mode == system_bkdrft && count_core > 1) {
    printf("multi core is not implemented in bkdrft mode\n");
    return -1;
  }

  if (system_mode == system_bkdrft && num_queues < 2) {
    printf("in bkdrft mode there should be at least 2 queue available!\n");
    return -1;
  }

  printf("Number of queues: %d\n", num_queues);

  // check if dpdk port exists
  if (port_type == dpdk && !rte_eth_dev_is_valid_port(0))
    rte_exit(EXIT_FAILURE, "Error no available port\n");

  create_pools(port_type, &rx_mbuf_pool, &tx_mbuf_pool, &ctrl_mbuf_pool);

  if (port_type == dpdk) {
    if (dpdk_port_init(dpdk_port, rx_mbuf_pool, num_queues, &my_eth) != 0)
      rte_exit(EXIT_FAILURE, "Cannot init port %hhu\n", dpdk_port);
  } else {
    if (vport_init(port_name, &virt_port)) {
      rte_exit(EXIT_FAILURE, "Failed to init vport\n");
    }
    // read port mac address
    for (int i = 0; i < 6; i++){
      // printf("%.2x:", virt_port->mac_addr[i]);
      my_eth.addr_bytes[i] = virt_port->mac_addr[i];
    }
    // printf("\n");
  }

//   int res;
//   printf("einval: %d, eio: %d, enotsup: %d, enodev: %d\n", EINVAL, EIO, ENOTSUP, ENODEV);
//   if ((res = setup_pfc(dpdk_port)) != 0)
//     rte_exit(EXIT_FAILURE, "Cannot setup pfc: %d\n", res);

  // add rules so each flow is placed on a seperate queue ===========
  // for (int i = 0; i < count_server_ips; i++) {
  //   flow = generate_ipv4_flow(dpdk_port, i, server_ips[i], full_mask, 0,
  //                             no_mask, &flow_error);

  //   char ip[20];
  //   ip_to_str(server_ips[i], ip, 20);
  //   printf("map prio: 3 vlan-id: 100 ip: %s to q: %d\n", ip, i);

  //   if (flow == NULL) {
  //     printf("flow error: %s\n", flow_error.message);
  //   }
  //   // TODO: in  vhost mode it will fail
  //   // assert (flow != NULL);
  // }

  // tmp_addr = (struct rte_ether_addr){{0x98, 0xf2, 0xb3, 0xc8, 0x49, 0xd5}};
  // flow = generate_eth_flow(dpdk_port, 0, tmp_addr, eth_addr_full_mask,
  //                          eth_addr_no_mask, eth_addr_no_mask, &flow_error);
  // if (flow == NULL) {
  //   printf("flow error: %s\n", flow_error.message);
  //   // TODO: in  vhost mode it will fail
  //   // assert(0);
  // }
  // tmp_addr = (struct rte_ether_addr){{0x98, 0xf2, 0xb3, 0xcb, 0x05, 0x91}};
  // flow = generate_eth_flow(dpdk_port, 1, tmp_addr, eth_addr_full_mask,
  //                          eth_addr_no_mask, eth_addr_no_mask, &flow_error);
  // if (flow == NULL) {
  //   printf("flow error: %s\n", flow_error.message);
  //   // TODO: in  vhost mode it will fail
  //   // assert(0);
  // }
  // end of flow rules ==============================================

  // TODO: fractions are not counted here
  assert(num_queues % count_core == 0);
  // fill context objects ===========================================
  int next_qid = 0;
  int queue_per_core = num_queues / count_core;
  if (system_mode == system_bkdrft) {
    // num_queues -= 1;
    next_qid = 1;
    queue_per_core = (num_queues - 1) /count_core;
  }
  int findex = 0;
  for (int i = 0; i < count_core; i++) {
    cntxs[i].mode = mode;
    cntxs[i].system_mode = system_mode;
    cntxs[i].ptype = port_type;
    cntxs[i].rx_mem_pool = rx_mbuf_pool;
    cntxs[i].tx_mem_pool = tx_mbuf_pool;
    cntxs[i].ctrl_mem_pool = ctrl_mbuf_pool;
    cntxs[i].worker_id = i;
    cntxs[i].dpdk_port_id = dpdk_port;
    cntxs[i].virt_port = virt_port;
    cntxs[i].my_eth = my_eth;

    cntxs[i].default_qid = next_qid; // poll this queue
    next_qid += queue_per_core;
    assert(next_qid - 1 < num_queues);

    cntxs[i].running = 1;     // this job of this cntx has not finished yet
    cntxs[i].src_ip = source_ip + i;
    cntxs[i].use_vlan = 0;
    cntxs[i].bidi = bidi;
    cntxs[i].num_queues = num_queues; // how many queue port has

    /* how many queue the contex is responsible for */
    cntxs[i].count_queues = queue_per_core;

    /* allocate output buffer and get a file descriptor for that */
    output_buffers[i] = malloc(2048);
    FILE *fp = fmemopen(output_buffers[i], 2048, "w+");
    assert(fp != NULL);
    cntxs[i].fp = fp;

    cntxs[i].rate_limit = rate_limit;
    cntxs[i].rate = rate;

    if (mode == mode_server) {
      cntxs[i].src_port = server_port[0];
      cntxs[i].managed_queues = malloc(queue_per_core * sizeof(uint32_t));
      for (int q = 0; q < queue_per_core; q++) {
        cntxs[i].managed_queues[q] = (findex * queue_per_core) + q;
        if (system_mode == system_bkdrft) {
          cntxs[i].managed_queues[q]++; // zero is reserved
          if (cntxs[i].managed_queues[q] >= num_queues)
            cntxs[i].managed_queues[q] = num_queues - 1;
        }
      }
      findex++;

      cntxs[i].delay_us = server_delay;
      cntxs[i].delay_cycles = server_delay;
      printf("Server delay: %d\n", server_delay);

      cntxs[i].dst_ips = NULL;
      cntxs[i].tmp_array = malloc(sizeof(uint64_t *) *2);
      for (int z = 0; z < 2; z++)
        cntxs[i].tmp_array[z] = calloc(32, sizeof(uint64_t));

    } else {
      // this is a client application

      // TODO: fractions are not considered for this division
      int ips = count_server_ips / count_core;
      assert((count_server_ips % count_core) == 0);
      assert((count_flow % count_core) == 0);

      cntxs[i].src_port = client_port + i;
      cntxs[i].dst_ips = malloc(sizeof(int) * ips);
      {
        char ip_str[20];
        for (int j = 0; j < ips; j++) {
          cntxs[i].dst_ips[j] = server_ips[findex++];
          ip_to_str(cntxs[i].dst_ips[j], ip_str, 20);
          printf("ips: %s\n", ip_str);
        }
      }
      cntxs[i].count_dst_ip = ips;
      // TODO: next line wont work on other count_of_cpu
      // number of server ports are limited
      cntxs[i].dst_port = server_port[i];
      cntxs[i].payload_length = payload_size;

      cntxs[i].count_flow = count_flow / count_core;
      cntxs[i].base_port_number = server_port[i];

      cntxs[i].duration = duration;
      cntxs[i].delay_cycles = delay_cycles;

      /* use zipf for selecting dst ip */
      cntxs[i].destination_distribution = DIST_ZIPF; // DIST_UNIFORM;
      cntxs[i].queue_selection_distribution = DIST_UNIFORM;

      cntxs[i].managed_queues = NULL;
    }
  }

  cntxIndex = 1;
  if (mode) {
    RTE_LCORE_FOREACH_SLAVE(lcore_id) {
      rte_eal_remote_launch(do_server, (void *)&cntxs[cntxIndex++], lcore_id);
    }
    do_server(&cntxs[0]);

    // ask other workers to stop
    for (int i = 0; i < count_core; i++) {
      cntxs[i].running = 0;
    }

    rte_eal_mp_wait_lcore(); // wait until all workers are stopped

    // uint64_t tmp_array[2][32] ={};
    // for (int i = 0; i < count_core; i++) {
    //   /* stop other wokers from running */
    //   cntxs[i].running = 0;
    //   for (int z = 0; z < 32; z++) {
    //     tmp_array[0][z] += cntxs[i].tmp_array[0][z];
    //     tmp_array[1][z] += cntxs[i].tmp_array[1][z];
    //   }
    // }

    // sleep(4);

    for (int i = 0; i < count_core; i++) {
      printf("------ worker %d ------\n", i);
      printf("%s\n", output_buffers[i]);
      printf("------  end  ---------\n");
    }

    // printf("1001...1008\n");
    // for (int i = 0; i < 16; i++) {
    //   printf("flow: %d, src_port: %d, pkts: %ld\n", i, 1001 + i, tmp_array[0][i]);
    // }
    // printf("2002...2009\n");
    // for (int i = 0; i < 8; i++) {
    //   printf("flow: %d, src_port: %d, pkts: %ld\n", i, 2002 + i, tmp_array[1][i]);
    // }

  } else {
    // RTE_LCORE_FOREACH_SLAVE(lcore_id) {
    //   rte_eal_remote_launch(do_client, (void *)&cntxs[cntxIndex++], lcore_id);
    // }
    do_client(&cntxs[0]);

    // rte_eal_mp_wait_lcore();

    // print client results to stdout
    for (int i = 0; i < count_core; i++) {
      printf("%s\n", output_buffers[i]);
    }
  }

  // free
  for (int i = 0; i < count_core; i++) {
    if (mode == mode_server) {
      free(cntxs[i].managed_queues);
      free(cntxs[i].tmp_array[0]);
      free(cntxs[i].tmp_array[1]);
      free(cntxs[i].tmp_array);
    } else {
      free(cntxs[i].dst_ips);
    }
    fclose(cntxs[i].fp);
    free(output_buffers[i]);
  }
  if (port_type == dpdk) {
    for (int q = 0; q < num_queues; q++) {
      rte_eth_tx_done_cleanup(dpdk_port, q, 0);
    }
  }
  return 0;
}
