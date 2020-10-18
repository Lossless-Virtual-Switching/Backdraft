#ifndef _VPORT_H
#define _VPORT_H
#include <stdint.h>
#include "llring.h"

#define MAX_QUEUES_PER_DIR 8

#define SLOTS_PER_LLRING 1024
#define SLOTS_WATERMARK ((SLOTS_PER_LLRING >> 3) * 7) /* 87.5% */

#define SINGLE_PRODUCER 0
#define SINGLE_CONSUMER 0

#define PORT_NAME_LEN 128
#define VPORT_DIR_PREFIX "vports"
#define PORT_DIR_LEN PORT_NAME_LEN + 256
#define TMP_DIR "/tmp"

struct vport_inc_regs {
  uint64_t dropped;
} __attribute__ ((aligned (64)));

struct vport_out_regs {
  uint32_t irq_enabled;
} __attribute__ ((aligned (64)));

struct vport_bar {
  char name[PORT_NAME_LEN];

  int num_inc_q;
  int num_out_q;

  struct vport_inc_regs *inc_regs[MAX_QUEUES_PER_DIR];
  struct llring *inc_qs[MAX_QUEUES_PER_DIR];

  struct vport_out_regs *out_regs[MAX_QUEUES_PER_DIR];
  struct llring *out_qs[MAX_QUEUES_PER_DIR];
};

struct vport {
  int _main;
  struct vport_bar *bar;
  int out_irq_fd[MAX_QUEUES_PER_DIR];
};

struct vport *from_vbar_addr(size_t bar_address);
struct vport *new_vport(const char *name, uint16_t num_inc_q,
                        uint16_t num_out_q);
int free_vport(struct vport *port);
int send_packets_vport(struct vport *port, uint16_t qid, void**pkts, int cnt);
int recv_packets_vport(struct vport *port, uint16_t qid, void**pkts, int cnt);

#endif
