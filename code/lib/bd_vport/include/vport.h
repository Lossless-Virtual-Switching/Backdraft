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

#define RATE_SEQUENCE_SIZE 4
#define RATE_INIT_VALUE 20000000
struct rate {
  uint64_t pps;
  uint64_t last_ts;
  uint64_t tp;
  uint64_t sum;
  uint64_t sequence[RATE_SEQUENCE_SIZE];
  size_t tail;
  size_t head;
}__attribute__((__aligned__(64)));

struct queue_handler {
  struct llr_seg *reader_head;
  struct llr_seg *writer_head;
  struct rate rate;
  size_t total_size;
  volatile uint32_t enqueue_pkts;
  volatile uint32_t dequeue_pkts;
  size_t th_over;
  size_t th_goal;
  uint64_t count_empty;
  uint8_t is_paused;
}__attribute__((__aligned__(64)));

enum queue_direction {
  INC=0,
  OUT=1,
};

struct vport_bar {
  char name[PORT_NAME_LEN];
  int num_inc_q;
  int num_out_q;
  struct queue_handler *queues[2]; // INC and OUT
  struct llring_pool *pool;
}__attribute__((__aligned__(64)));

struct vport {
  int _main;
  struct vport_bar *bar;
  uint8_t mac_addr[6];
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
int send_packets_vport_with_bp(struct vport *port, uint16_t qid, void **pkts,
                               int cnt, uint64_t *pause_duration);
int recv_packets_vport(struct vport *port, uint16_t qid, void**pkts, int cnt);
void set_queue_pause_state(struct vport *port, int dir, uint16_t qid,
    uint8_t state);

#endif
