#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <stdint.h>
#include <string.h>

#include "include/exp.h"
#include "include/flow_rules.h"

#define RX_RING_SIZE (64)
#define TX_RING_SIZE (64)

#define NUM_MBUFS (8191)
#define MBUF_CACHE_SIZE (250)

static unsigned int dpdk_port = 0;
// TODO: find out if connected to nic or not
// static int connected_to_nic = 0;

/*
 * Initialize an ethernet port
 */
static inline int port_init(uint8_t port, struct rte_mempool *mbuf_pool,
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

static int dpdk_init(int argc, char *argv[], struct rte_mempool **rx_mbuf_pool,
                     struct rte_mempool **tx_mbuf_pool,
                     struct rte_mempool **ctrl_mbuf_pool) {
  int args_parsed;

  args_parsed = rte_eal_init(argc, argv);
  if (args_parsed < 0)
    rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

  if (!rte_eth_dev_is_valid_port(0))
    rte_exit(EXIT_FAILURE, "Error no available port\n");

  // create mempools
  *rx_mbuf_pool =
      rte_pktmbuf_pool_create("MBUF_RX_POOL", NUM_MBUFS, MBUF_CACHE_SIZE, 0,
                              RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

  if (*rx_mbuf_pool == NULL)
    rte_exit(EXIT_FAILURE, "Error can not create rx mbuf pool\n");

  *tx_mbuf_pool =
      rte_pktmbuf_pool_create("MBUF_TX_POOL", NUM_MBUFS, MBUF_CACHE_SIZE, 0,
                              RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

  if (*tx_mbuf_pool == NULL)
    rte_exit(EXIT_FAILURE, "Error can not create tx mbuf pool\n");

  *ctrl_mbuf_pool =
      rte_pktmbuf_pool_create("MBUF_CTRL_POOL", NUM_MBUFS, MBUF_CACHE_SIZE, 0,
                              RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

  if (*ctrl_mbuf_pool == NULL)
    rte_exit(EXIT_FAILURE, "Error can not create tx mbuf pool\n");

  return args_parsed;
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

/*
 * Main function for running a server or client applications
 * the client is a udp flow generator and the server is a
 * echo server.
 * Client calculates the latency of a round trip and reports percentiles.
 * */
int main(int argc, char *argv[]) {
  // number of arguments used by dpdk arg parse
  int args_parsed;

  struct rte_mempool *rx_mbuf_pool;
  struct rte_mempool *tx_mbuf_pool;
  struct rte_mempool *ctrl_mbuf_pool;

  // mac address of connected port
  struct rte_ether_addr my_eth;

  // these ip and port values are fake
  // uint32_t client_ip;
  // uint32_t server_ip;
  uint32_t source_ip;
  int client_port = 5058;
  // TODO: this does not generalize to the count cpu
  int server_port[16] = {1001, 5001, 6002};

  // how much is the size of udp payload
  // TODO: take message size from arguments
  int payload_size = 1450; // 64; // 

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
  uint32_t duration = 40;

  // only used in server
  // delay for each burst in server (us)
  unsigned int server_delay = 0;

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

  // struct rte_ether_addr tmp_addr;

  // let dpdk parse its own arguments
  args_parsed =
      dpdk_init(argc, argv, &rx_mbuf_pool, &tx_mbuf_pool, &ctrl_mbuf_pool);

  // this is filling fake ip variables
  // str_to_ip("192.168.2.55", &client_ip);
  // str_to_ip("192.168.2.10", &server_ip);

  // parse program args
  argc -= args_parsed;
  argv += args_parsed;

  // arguments:
  // * source_ip: ip of host on which app is running (useful for arp)
  // * number of queue
  // * system mode: bess or bkdrft
  // * mode: client or server
  // [client]
  // * count destination ips
  // * valid ip values (as many as defined in prev. param)
  // * count flow
  // * experiment duration
  // * client port
  // [server]
  // * server delay for each batch

  // source_ip
  str_to_ip(argv[1], &source_ip);
  argv++;
  argc--;

  // read number of queues
  num_queues = atoi(argv[1]);
  argv++;
  argc--;

  // check number of remaining args
  if (argc < 3) {
    rte_exit(EXIT_FAILURE, "Wrong number of arguments");
  }

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
    if (argc > 5 + count_server_ips)
      duration = atoi(argv[5 + count_server_ips]);
    printf("Experiment duration: %d\n", duration);

    if (argc > 6 + count_server_ips)
      client_port = atoi(argv[6 + count_server_ips]);
    printf("Client port: %d\n", client_port);

  } else if (!strcmp(argv[2], "server")) {
    // server
    mode = mode_server;

    // parse server arguments
    if (argc < 2) {
      rte_exit(EXIT_FAILURE, "wrong number of arguments");
    }
    server_delay = atoi(argv[3]);
  } else {
    printf("Second argument should be `client` or `server`\n");
    return -1;
  }

  count_core = rte_lcore_count();
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
  int tmp_port;
  for(tmp_port = 0; tmp_port < 4; tmp_port++)
    if (port_init(tmp_port, rx_mbuf_pool, num_queues, &my_eth) != 0)
      rte_exit(EXIT_FAILURE, "Cannot init port %hhu\n", dpdk_port);

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

  // fill context objects ===========================================
  int next_qid = system_mode == system_bkdrft ? 1 : 0;
  int findex = 0;
  for (int i = 0; i < count_core; i++) {
    cntxs[i].mode = mode;
    cntxs[i].system_mode = system_mode;
    cntxs[i].rx_mem_pool = rx_mbuf_pool;
    cntxs[i].tx_mem_pool = tx_mbuf_pool;
    cntxs[i].ctrl_mem_pool = ctrl_mbuf_pool;
    cntxs[i].port = i; // port id
    cntxs[i].num_queues = num_queues;
    cntxs[i].my_eth = my_eth;
    cntxs[i].default_qid = next_qid++; // poll this queue
    cntxs[i].running = 1;     // this job of this cntx has not finished yet
    cntxs[i].src_ip = source_ip;
    cntxs[i].use_vlan = 0;
    cntxs[i].bidi = 0;
    cntxs[i].worker_id = i;
    if (mode == mode_server) {
      // TODO: fractions are not counted here
      assert(num_queues % count_core == 0);
      int queue_per_core = num_queues; // / count_core;

      // if it is server application
      cntxs[i].src_port = server_port[0];

      cntxs[i].count_queues = queue_per_core;
      cntxs[i].managed_queues = malloc(queue_per_core * sizeof(uint32_t));
      // for (int q = 0; q < queue_per_core; q++) {
      for (int q = 0; q < queue_per_core; q++) {
        // cntxs[i].managed_queues[q] = (findex * queue_per_core) + q;
        cntxs[i].managed_queues[q] = q;
        if (system_mode == system_bkdrft) {
          cntxs[i].managed_queues[q]++; // zero is reserved
					if (cntxs[i].managed_queues[q] >= num_queues)
						cntxs[i].managed_queues[q] = num_queues - 1;
					}
      }
      findex++;

      // if(i == 0)
      cntxs[i].delay_us = server_delay;
      printf("Server delay: %d\n", server_delay);

      cntxs[i].dst_ips = NULL;
    } else {
      // this is client application

      // TODO: fractions are not considered for this division
      int ips = count_server_ips / count_core;
      assert((count_server_ips % count_core) == 0);

      cntxs[i].src_port = client_port;
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

      // allocate output buffer and get a file descriptor for that
      output_buffers[i] = malloc(2048);
      FILE *fp = fmemopen(output_buffers[i], 2048, "w+");
      assert(fp != NULL);
      cntxs[i].fp = fp;
      cntxs[i].count_flow = count_flow;
      cntxs[i].base_port_number = server_port[i];
      cntxs[i].count_queues = num_queues;

      cntxs[i].duration = duration;

      cntxs[i].managed_queues = NULL;
    }
  }

  // free
  for (int q = 0; q < num_queues; q++) {
    rte_eth_tx_done_cleanup(dpdk_port, q, 0);
  }

  cntxIndex = 1;
  if (mode) {
    RTE_LCORE_FOREACH_SLAVE(lcore_id) {
      rte_eal_remote_launch(do_server, (void *)&cntxs[cntxIndex++], lcore_id);
    }
    do_server(&cntxs[0]);
  } else {
    RTE_LCORE_FOREACH_SLAVE(lcore_id) {
      rte_eal_remote_launch(do_client, (void *)&cntxs[cntxIndex++], lcore_id);
    }
    do_client(&cntxs[0]);

    // TODO: change this with a barrier
    for (int i = 0; i < count_core; i++) {
      while (cntxs[i].running) {
        rte_delay_us_block(1000); // 1 msec
      }
    }

    // print client results to stdout
    for (int i = 0; i < count_core; i++) {
      printf("%s\n", output_buffers[i]);
    }
  }

  // free
  for (int i = 0; i < count_core; i++) {
    if (mode == mode_server) {
      printf("%d\n",i);
      printf("%d\n", cntxs[i].managed_queues[0]);
      // free(cntxs[i].managed_queues);
    } else {
      free(cntxs[i].dst_ips);
      fclose(cntxs[i].fp);
      free(output_buffers[i]);
    }
  }
  for (int q = 0; q < num_queues; q++) {
    rte_eth_tx_done_cleanup(dpdk_port, q, 0);
  }
  return 0;
}
