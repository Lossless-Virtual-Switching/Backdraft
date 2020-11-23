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

#include "vport.h"

#define ROUND_TO_64(x) ((x + 32) & (~0x3f))

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
  port = rte_zmalloc(NULL, sizeof(struct vport), 0);
  assert(port);
  port->_main = 0; // this is connected to someone elses vport_bar
  port->bar = (struct vport_bar *)bar_address;
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
  uint16_t pool_size = (num_inc_q + num_out_q) * 3 / 2;
  if (pool_size < num_inc_q + num_out_q)
    return NULL;
  return _new_vport(name, num_inc_q, num_out_q, SLOTS_PER_LLRING, pool_size);
}

struct vport *_new_vport(const char *name, uint16_t num_inc_q,
                         uint16_t num_out_q, uint16_t pool_size,
                         uint16_t q_size)
{
  uint32_t i;

  uint32_t inc_qs_ptr_size;
  uint32_t out_qs_ptr_size;
  uint32_t total_memory_needed;

  uint8_t *ptr;
  struct llr_seg *seg;
  FILE *fp;

  struct stat sb;
  char port_dir[PORT_DIR_LEN];
  char file_name[PORT_DIR_LEN];

  struct vport *port;
  struct vport_bar *bar;
  size_t bar_address;

  // allocate vport struct
  port = rte_zmalloc(NULL, sizeof(struct vport), 0);
  assert(port != NULL);
  port->_main = 1; // this is the main vport (ownership of vbar)

  // calculate how much memory is needed for vport_bar
  inc_qs_ptr_size = ROUND_TO_64(sizeof(struct llr_seg *)) * num_inc_q;
  out_qs_ptr_size = ROUND_TO_64(sizeof(struct llr_seg *)) * num_out_q;
  total_memory_needed = ROUND_TO_64(sizeof(struct vport_bar)) +
                        inc_qs_ptr_size + out_qs_ptr_size;

  bar = rte_zmalloc(NULL, total_memory_needed, 64);
  assert(bar);

  bar_address = (size_t)bar;
  port->bar = bar;

  // initialize vport_bar structure
  strncpy(bar->name, name, PORT_NAME_LEN);
  bar->num_inc_q = num_inc_q;
  bar->num_out_q = num_out_q;
  bar->pool = new_llr_pool(pool_size, q_size);

  ptr = (uint8_t *)bar;
  ptr += ROUND_TO_64(sizeof(struct vport_bar));
  bar->inc_qs = (struct llr_seg **)ptr;
  bar->out_qs = (struct llr_seg **)(ptr + inc_qs_ptr_size);

  // init each queue
  for (i = 0; i < num_inc_q; i++) {
    seg = pull_llr(bar->pool);
    bar->inc_qs[i] = seg;
  }

  for (i = 0; i < num_out_q; i++) {
    seg = pull_llr(bar->pool);
    bar->out_qs[i] = seg;
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
  struct llring *q;
  int ret;

  if (port->_main) {
    q = &port->bar->out_qs[qid]->ring;
  } else {
    q = &port->bar->inc_qs[qid]->ring;
  }

  // ret = llring_enqueue_bulk(q, pkts, cnt);
  ret = llring_enqueue_burst(q, pkts, cnt);
  ret &= 0x7fffffff;
  return ret;
  // if (ret == -LLRING_ERR_NOBUF)
  //   return 0;

  // if (__sync_bool_compare_and_swap(&port->bar->out_regs[qid]->irq_enabled, 1, 0)) {
  //   char t[1] = {'F'};
  //   ret = write(port->out_irq_fd[qid], (void *)t, 1);
  // }

  // return cnt;
}

int recv_packets_vport(struct vport *port, uint16_t qid, void**pkts, int cnt)
{
  struct llring *q;
  int ret;

  if (port->_main) {
    q = &port->bar->inc_qs[qid]->ring;
  } else {
    q = &port->bar->out_qs[qid]->ring;
  }

  // TODO: update code to work with bulk
  ret = llring_dequeue_burst(q, pkts, cnt);
  return ret;
}
