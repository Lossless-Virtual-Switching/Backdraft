// TODO: add license of VPORT
#include <x86intrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>

#include <rte_malloc.h>

#include "vport_const.h"
#include "vport.h"
#include "llring_pool.h"
#include "list.h"

#define ROUND_TO_64(x) ((x + 64) & (~0x3f))
#define MIN(x, y) ((x < y) ? x : y)

void _rand_set_port_mac_address(struct vport *port)
{
  uint32_t i;
  uint32_t seed;
  seed = __rdtsc();
  srand(seed);
  for (i = 0; i < 6; i++) {
    port->mac_addr[i] = rand() & 0xff;
  }
  port->mac_addr[0] &= 0xfe; // not broadcast/multicast
  port->mac_addr[0] |= 0x02; // locally administered
}

/* Connect to an existing vport_bar
 * */
struct vport *from_vport_name(char *port_name)
{
  int res;
  size_t bar_address;
  FILE *fp;
  char port_path[PORT_DIR_LEN];
  res = snprintf(port_path, PORT_DIR_LEN, "%s/%s/%s", TMP_DIR, VPORT_DIR_PREFIX,
                 port_name);
  if (res >= PORT_DIR_LEN) return NULL;

  fp = fopen(port_path, "r");
  if (fp == NULL) return NULL;

  res = fread(&bar_address, 8, 1, fp);
  if (res == 0) return NULL;

  fclose(fp);

  return from_vbar_addr(bar_address);
}

/* Connect to an existing vport_bar
 * */
struct vport *from_vbar_addr(size_t bar_address)
{
  struct vport *port;
  size_t port_size;
  struct vport_bar *bar;
  uint8_t *ptr;

  bar = (struct vport_bar *)bar_address;
  port_size = sizeof(struct vport);
  ptr = rte_zmalloc(NULL, port_size, 0);
  port = (struct vport *)ptr;
  assert(port);
  port->_main = 0; // this is connected to someone elses vport_bar
  port->bar = bar;

  // set port mac address
  _rand_set_port_mac_address(port);
  return port;
}

/* Allocate a new vport and vport_bar
 * Setup pipes
 * */
struct vport *new_vport(const char *name, uint16_t num_inc_q,
                        uint16_t num_out_q)
{
  uint16_t pool_size = (num_inc_q + num_out_q) * 3 / 2 + 64;
  if (pool_size < num_inc_q + num_out_q)
    return NULL;
  return _new_vport(name, num_inc_q, num_out_q, pool_size, SLOTS_PER_LLRING);
}

struct vport *_new_vport(const char *name, uint16_t num_inc_q,
                         uint16_t num_out_q, uint16_t pool_size,
                         uint16_t q_size)
{
  printf("Creating vport with qsize: %d\n", q_size);
  uint32_t i;
  uint32_t dir;
  uint16_t num_queues[2] = {num_inc_q, num_out_q};

  size_t total_memory_needed;
  uint32_t port_size;

  uint8_t *ptr;
  struct llr_seg *seg;
  FILE *fp;

  struct stat sb;
  char port_dir[PORT_DIR_LEN];
  char file_name[PORT_DIR_LEN];

  struct vport *port;
  struct vport_bar *bar;
  size_t bar_address;

  struct rate *rate;
  struct queue_handler *q_handler;

  // allocate vport struct
  port_size = sizeof(struct vport);
  ptr = rte_zmalloc(NULL, port_size, 0);
  port = (struct vport *)ptr;
  assert(port != NULL);
  port->_main = 1; // this is the main vport (ownership of vbar)

  // Calculate how much memory is needed for vport_bar
  total_memory_needed = ROUND_TO_64(sizeof(struct vport_bar)) +
                        ((num_inc_q + num_out_q) *
                          ROUND_TO_64(sizeof(struct queue_handler)));
  bar = rte_zmalloc(NULL, total_memory_needed, 64);
  assert(bar != NULL);

  bar_address = (size_t)bar;
  port->bar = bar;

  // Initialize vport_bar structure
  strncpy(bar->name, name, PORT_NAME_LEN - 1);
  bar->num_inc_q = num_inc_q;
  bar->num_out_q = num_out_q;
  bar->pool = new_llr_pool(name, SOCKET_ID_ANY, pool_size, q_size);

  ptr = (uint8_t *)bar;
  ptr += ROUND_TO_64(sizeof(struct vport_bar));
  bar->queues[INC] = (struct queue_handler *)ptr;
  ptr += num_inc_q * ROUND_TO_64(sizeof(struct queue_handler));
  bar->queues[OUT] = (struct queue_handler *)ptr;

  // Initialize queue handlers
  for (dir = 0; dir < 2; dir++) {
    for (i = 0; i < num_queues[dir]; i++) {
      // printf("dir: %d qid: %d\n", dir, i);
      q_handler = &bar->queues[dir][i];

      // Assign a llring segment to this queue
      seg = pull_llr(bar->pool);

      assert(seg != NULL);
      // assert(llring_free_count(&seg->ring) > 0);

      INIT_LIST_HEAD(&seg->list);
      q_handler->reader_head = seg;
      q_handler->writer_head = seg;
      // printf("assigning qid: %d, ptr: %p next: %p\n",
      //         i, &seg->list, seg->list.next);

      rate = &q_handler->rate;
      rate->tail = RATE_SEQUENCE_SIZE - 1;

      q_handler->total_size = q_size;
      q_handler->th_over = q_size * 7 / 8;
      q_handler->th_goal = q_size / 8;
    }
  }

  // Create temp directory
  snprintf(port_dir, PORT_DIR_LEN, "%s/%s", TMP_DIR, VPORT_DIR_PREFIX);
  if (stat(port_dir, &sb) == 0) {
    assert((sb.st_mode & S_IFMT) == S_IFDIR);
  } else {
    printf("Creating directory %s\n", port_dir);
    mkdir(port_dir, S_IRWXU | S_IRWXG | S_IRWXO);
  }

  // Set port mac address
  _rand_set_port_mac_address(port);

  // Create socket file (file with shared memory information)
  snprintf(file_name, PORT_DIR_LEN, "%s/%s/%s", TMP_DIR,
           VPORT_DIR_PREFIX, name);
  printf("Writing port information to %s\n", file_name);
  fp = fopen(file_name, "w");
  fwrite(&bar_address, 8, 1, fp);
  fclose(fp);
  return port;
}

int free_vport(struct vport *port)
{
  if (port->_main) {
    // TODO: keep track of number of connected vports
    // Make sure there is no other vport connected to this vport bar
    char file_name[PORT_DIR_LEN];
    char *name = port->bar->name;

    snprintf(file_name, PORT_DIR_LEN, "%s/%s/%s", TMP_DIR, VPORT_DIR_PREFIX,
             name);
    unlink(file_name);

    free_llr_pool(port->bar->pool);
    rte_free(port->bar);
  }

  rte_free(port);
  return 0;
}

int send_packets_vport(struct vport *port, uint16_t qid, void**pkts, int cnt)
{
  uint64_t pause_duration;
  return send_packets_vport_with_bp(port, qid, pkts, cnt, &pause_duration);
}

static inline uint32_t _count_pkts_in_q(struct queue_handler *q)
{
  uint32_t enq = q->enqueue_pkts;
  uint32_t deq = q->dequeue_pkts;
  uint32_t size = enq - deq;
  // TODO: we do mess up!
  // if ((size > q->total_size) || (size < 0)) {
  //   printf("messed up enq: %d deq: %d size: %d, total size: %ld\n",
  //       enq, deq, size, q->total_size);
  //   assert(0);
  // }
  return size;
}

/* Send packet to the queue.
 * pause_duration is in nano-seconds (ns)
 * */
int send_packets_vport_with_bp(struct vport *port, uint16_t qid, void **pkts,
                               int cnt, uint64_t *pause_duration)
{
  struct queue_handler *q;
  struct llr_seg *seg;
  int32_t ret;
  int enqueued = 0;
  // uint64_t pps;
  // uint32_t count_pkts;

  *pause_duration = 0;

  if (port->_main) {
    q = &port->bar->queues[OUT][qid];
  } else {
    q = &port->bar->queues[INC][qid];
  }
  // pps = q->rate.pps;
  seg = q->writer_head;

send_pkts:
  ret = llring_sp_enqueue_burst(&seg->ring, pkts, cnt);
  ret &= 0x7fffffff;
  enqueued += ret;
  q->enqueue_pkts += ret;
  if (ret < cnt && seg->list.next != &seg->list) {
    // Check if next segment is ready for the remaining packets
    seg = list_entry(seg->list.next, struct llr_seg, list);
    if (llring_count(&seg->ring) == 0) {
      q->writer_head = seg;
      cnt -= ret;
      pkts += ret;
      goto send_pkts;
    }
  }

  // Check if pause request should be signaled
  /* count_pkts = _count_pkts_in_q(q); */
  /* if (count_pkts > q->th_over) { */
  /*   if (pps > 0) { */
  /*     *pause_duration = ((uint64_t)(count_pkts - q->th_goal)) * */
  /*                           ((1000000000L) / pps); */
  /*     // Just for debuging */
  /*     // if (*pause_duration > 1000000000L) { */
  /*     //   printf("large pause: duration: %lu q: %d pps: %ld\n", */
  /*     //       *pause_duration, _count_pkts_in_q(q), pps); */
  /*     // } */
  /*   } else { */
  /*     *pause_duration = 10000; */
  /*   } */
  /* } */

  return enqueued;
}

int recv_packets_vport(struct vport *port, uint16_t qid, void**pkts, int cnt)
{
  struct queue_handler *q;
  struct llr_seg *read_seg;
  struct llr_seg *seg;
  int ret;
  /* struct rate *rate; */
  int dequeued = 0;

  if (port->_main) {
    q = &port->bar->queues[INC][qid];
  } else {
    q = &port->bar->queues[OUT][qid];
  }
  /* rate = &q->rate; */
  read_seg = q->reader_head;

  // Check if queue needs to be extended
  uint32_t pkts_in_q = _count_pkts_in_q(q);
  /* const int extend = 0; */
  // Setting an upper bound for how long the queue can be extend...
  if (EXTENDABLE && pkts_in_q > q->th_over &&
      q->total_size < QUEUE_LENGTH_UPPER_BOUND) {
    /* uint64_t b = rte_get_timer_cycles(); */
    // water mark crossed
    seg = pull_llr(port->bar->pool);
    assert(seg != NULL);
    /* uint64_t m1 = rte_get_timer_cycles(); */
    list_add_tail(&seg->list, &read_seg->list);
    /* uint64_t m2 = rte_get_timer_cycles(); */
    q->total_size += port->bar->pool->llr_size;
    q->th_goal = q->total_size / 10;
    q->th_over = q->total_size * 7 / 8;
    /* uint64_t e = rte_get_timer_cycles(); */
    /* printf("extend: pull: %ld add_tail: %ld counters: %ld total: %ld\n", m1 - */
    /*     b, m2 - m1, e - m2, e - b); */
    // printf("Extend queue %d q: %p new list: %p next: %p\n\n",
    //     qid, &q_list->list, &seg->list, q_list->list.next);
    // printf("Extending queue %d. Filled %d Size %ld\n", qid,
    // _count_pkts_in_q(q), q->total_size);
    // fflush(stdout);
  }

  // Try to dequeue packets
read_pkts:
  ret = llring_sc_dequeue_burst(&read_seg->ring, pkts, cnt);
  dequeued += ret;
  q->dequeue_pkts += ret;
  if (ret < cnt) {
    read_seg = list_entry(read_seg->list.next, struct llr_seg, list);
    // if (llring_count(&read_seg->ring) > 0) {
    if (_count_pkts_in_q(q) > 0) {
      q->reader_head = read_seg;
      cnt -= ret;
      pkts += ret;
      goto read_pkts;
    }
  }

  /* if (dequeued == 0 && q->is_paused) { */
  /*   q->count_empty++; */
  /*   if (q->empty_start_ts == 0) { */
  /*     q->empty_start_ts = rte_get_timer_cycles(); */
  /*   } */
  /* } else if(q->empty_start_ts != 0) { */
  /*   q->empty_cycles += rte_get_timer_cycles() - q->empty_start_ts; */
  /*   q->empty_start_ts = 0; */
  /* } */

  // // Queue Deallocation
  // if (0) {
  //   // check if there is a tail
  //   if (q_list->list.next != &q_list->list) {
  //     // printf("qid: %d me: %p, next: %p\n", qid, &q_list->list, q_list->list.next);
  //     // fflush(stdout);
  //     // there are some other queues
  //     if (port->_main) {
  //       port->bar->inc_qs[qid] = list_entry(q_list->list.next, struct llr_seg, list);
  //     } else {
  //       port->bar->out_qs[qid] = list_entry(q_list->list.next, struct llr_seg, list);
  //     }
  //     // list_del(&q_list->list);
  //     // push_llr(port->bar->pool, q_list);
  //     // printf("push llring back\n");
  //   }
  // }

  // Udpate queue throughput stats
  /* rate->tp += dequeued; */
  /* uint64_t now = rte_get_timer_cycles(); */
  /* if (now - rate->last_ts > rte_get_timer_hz()) { */
  /*   rate->sum += rate->tp; */
  /*   rate->sequence[rate->tail] = rate->tp; */
  /*   rate->tail = (rate->tail + 1) % RATE_SEQUENCE_SIZE; */
  /*   // if (rate->tail == rate->head) { */
  /*     rate->sum -= rate->sequence[rate->head]; */
  /*     rate->head = (rate->head + 1) % RATE_SEQUENCE_SIZE; */
  /*   // } */
  /*   rate->tp = 0; */
  /*   rate->last_ts = now; */
  /*   rate->pps = rate->sum / (RATE_SEQUENCE_SIZE - 1); */
  /*   // printf("main: %d qid: %d pps: %ld\n", port->_main, qid, rate->pps); */
  /*   // printf("main: %d qid: %d size: %d\n", */
  /*   //     port->_main, qid, llring_count(&read_seg->ring)); */
  /*   // printf("main: %d qid: %d empty: %ld cycles: %ld\n", */
  /*   //     port->_main, qid, q->count_empty, q->empty_cycles); */
  /*   q->count_empty = 0; */
  /*   q->empty_cycles = 0; */
  /* } */
  return dequeued;
}

// dir: 0 INC, 1 OUT
void set_queue_pause_state(struct vport *port, int dir, uint16_t qid,
    uint8_t state)
{
  if (port->_main) {
    port->bar->queues[dir][qid].is_paused = state;
  } else {
    port->bar->queues[1 - dir][qid].is_paused = state;
  }
}
