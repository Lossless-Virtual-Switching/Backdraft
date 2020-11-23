#ifndef _VPORT_H
#define _VPORT_H
#include <stdint.h>
#include "list.h"
#include "llring_pool.h"

// #define MAX_QUEUES_PER_DIR 16384
#define MAX_QUEUES_PER_DIR 10000

#define SLOTS_PER_LLRING 1024
#define SLOTS_WATERMARK ((SLOTS_PER_LLRING >> 3) * 7) /* 87.5% */

#define SINGLE_PRODUCER 1
#define SINGLE_CONSUMER 1

#define PORT_NAME_LEN 128
#define VPORT_DIR_PREFIX "vports"
#define PORT_DIR_LEN PORT_NAME_LEN + 256
#define TMP_DIR "/tmp"

struct vport_bar {
  char name[PORT_NAME_LEN];

  int num_inc_q;
  int num_out_q;

  // These are an array of extendable llrings
  // list of extendable-queues
  struct llr_seg **inc_qs;
  struct llr_seg **out_qs;
  struct llring_pool *pool;
};

struct vport {
  int _main;
  struct vport_bar *bar;
  uint8_t mac_addr[6];
  // int out_irq_fd[MAX_QUEUES_PER_DIR];
};

struct vport *from_vport_name(char *port_name);
struct vport *from_vbar_addr(size_t bar_address);
struct vport *new_vport(const char *name, uint16_t num_inc_q,
                        uint16_t num_out_q);
struct vport *_new_vport(const char *name, uint16_t num_inc_q,
                         uint16_t num_out_q, uint16_t pool_size,
                         uint16_t q_size);
int free_vport(struct vport *port);
int send_packets_vport(struct vport *port, uint16_t qid, void**pkts, int cnt);
int recv_packets_vport(struct vport *port, uint16_t qid, void**pkts, int cnt);

#endif
