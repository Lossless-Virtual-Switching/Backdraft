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
  int mode; // client or server
  int system_mode; // bess or bkdrft
  port_type_t ptype; // what kind of port to use

  struct rte_mempool *rx_mem_pool;
  struct rte_mempool *tx_mem_pool;
  struct rte_mempool *ctrl_mem_pool;

  uint32_t worker_id;

  // a descriptor for accessing the port
  uint32_t dpdk_port_id;
  struct vport *virt_port;

  uint16_t num_queues;
  uint16_t default_qid;

  struct rte_ether_addr my_eth;
  uint32_t src_ip;
  uint16_t src_port;
  uint32_t count_queues; // number of queues the managed by the worker

  uint8_t use_vlan;
  uint8_t bidi;

  FILE *fp;

  // server
  uint32_t delay_us;
  uint64_t delay_cycles;
  uint32_t *managed_queues; // array holding number of queues managed by the server
  uint64_t **tmp_array;

  // client
  int32_t duration;
  uint32_t *dst_ips;
  int count_dst_ip;
  uint16_t dst_port;
  int payload_length;

  int running; // this is set to zero when client is done.

  uint32_t count_flow; // how many flow to generate (each core)
  uint32_t base_port_number; // generate flows will have port numbers starting x  up to the x + count_flow
  uint8_t destination_distribution;
  uint8_t queue_selection_distribution;

  uint8_t rate_limit; // true or false
  uint64_t rate; // pps
};

static const struct rte_ether_addr broadcast_mac = {
    .addr_bytes = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
};

int str_to_ip(const char *str, uint32_t *addr);

void ip_to_str(uint32_t addr, char *str, uint32_t size);

int check_eth_hdr(uint32_t my_ip, struct rte_ether_addr *host_mac,
    struct rte_mbuf *buf, struct rte_mempool *tx_mbuf_pool, uint8_t cdq,
    uint16_t port, uint16_t queue);

int check_eth_hdr_vport(uint32_t my_ip, struct rte_ether_addr *host_mac,
    struct rte_mbuf *buf, struct rte_mempool *tx_mbuf_pool, uint8_t cdq,
    struct vport *port, uint16_t queue);

int do_server(void *cntx);

int do_client(void *cntx);

void wait(uint64_t ns);
#endif // __SLOW_RECEIVER_EXP_
