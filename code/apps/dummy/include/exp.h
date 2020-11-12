#ifndef __SLOW_RECEIVER_EXP_
#define __SLOW_RECEIVER_EXP_

#include <stdint.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include "vport.h" // struct vport, ...

#define mode_client (0)
#define mode_server (1)

#define system_bess (0)
#define system_bkdrft (1)

#define MAKE_IP_ADDR(a, b, c, d)      \
  (((uint32_t) a << 24) | ((uint32_t) b << 16) |  \
   ((uint32_t) c << 8) | (uint32_t) d)

/* distributions  */
#define DIST_UNIFORM 0
#define DIST_ZIPF    1

typedef enum {
  dpdk,
  vport,
} port_type_t;

struct context {
  int cdq; // bess or bkdrft
  port_type_t ptype; // what kind of port to use

  struct rte_mempool *rx_mem_pool;
  struct rte_mempool *tx_mem_pool;
  struct rte_mempool *ctrl_mem_pool;

  uint32_t worker_id;

  // a descriptor for accessing the port
  uint32_t dpdk_port_id;
  struct vport *virt_port;

  uint16_t num_queues;
  uint8_t default_qid;

  uint32_t count_queues; // number of queues the managed by the worker
  uint32_t *managed_queues;

  // server
  uint64_t delay_cycles;

  // client
  uint32_t *dst_ips;
  int count_dst_ip;

  int running; // this is set to zero when client is done.
};

static const struct rte_ether_addr broadcast_mac = {
    .addr_bytes = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
};

int str_to_ip(const char *str, uint32_t *addr);

void ip_to_str(uint32_t addr, char *str, uint32_t size);

int do_server(void *cntx);
#endif // __SLOW_RECEIVER_EXP_
