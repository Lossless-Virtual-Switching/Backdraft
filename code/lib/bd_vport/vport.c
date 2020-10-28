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
  return port;
}

/* Allocate a new vport and vport_bar
 * Setup pipes
 * */
struct vport *new_vport(const char *name, uint16_t num_inc_q,
                        uint16_t num_out_q)
{
  uint32_t i;
  uint32_t seed;

  uint32_t bytes_per_llring;
  uint32_t total_memory_needed;

  uint8_t *ptr;
  struct llring *llr;
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
  bytes_per_llring = llring_bytes_with_slots(SLOTS_PER_LLRING);
  bytes_per_llring = ROUND_TO_64(bytes_per_llring);
  total_memory_needed = ROUND_TO_64(sizeof(struct vport_bar)) +
                bytes_per_llring * (num_inc_q + num_out_q) +
                ROUND_TO_64(sizeof(struct vport_inc_regs)) * num_inc_q +
                ROUND_TO_64(sizeof(struct vport_out_regs)) * num_out_q;

  bar = rte_zmalloc(NULL, total_memory_needed, 64);
  assert(bar);

  bar_address = (size_t)bar;
  port->bar = bar;

  // initialize vport_bar structure
  strncpy(bar->name, name, PORT_NAME_LEN);
  bar->num_inc_q = num_inc_q;
  bar->num_out_q = num_out_q;

  // move forward to reach llring section
  ptr = (uint8_t *)bar;
  ptr += ROUND_TO_64(sizeof(struct vport_bar));

  // setup inc llring
  for (i = 0; i < num_inc_q; i++) {
    bar->inc_regs[i] = (struct vport_inc_regs *)ptr;
    ptr += ROUND_TO_64(sizeof(struct vport_inc_regs));

    llr = (struct llring*)ptr;
    llring_init(llr, SLOTS_PER_LLRING, SINGLE_PRODUCER, SINGLE_CONSUMER);
    llring_set_water_mark(llr, SLOTS_WATERMARK);
    bar->inc_qs[i] = llr;
    ptr += bytes_per_llring;
  }

  // setup out llring
  for (i = 0; i < num_inc_q; i++) {
    bar->out_regs[i] = (struct vport_out_regs *)ptr;
    ptr += ROUND_TO_64(sizeof(struct vport_out_regs));

    llr = (struct llring*)ptr;
    llring_init(llr, SLOTS_PER_LLRING, SINGLE_PRODUCER, SINGLE_CONSUMER);
    llring_set_water_mark(llr, SLOTS_WATERMARK);
    bar->out_qs[i] = llr;
    ptr += bytes_per_llring;
  }

	snprintf(port_dir, PORT_DIR_LEN, "%s/%s", TMP_DIR, VPORT_DIR_PREFIX);
  if (stat(port_dir, &sb) == 0) {
    assert((sb.st_mode & S_IFMT) == S_IFDIR);
  } else {
    printf("Creating directory %s\n", port_dir);
    mkdir(port_dir, S_IRWXU | S_IRWXG | S_IRWXO);
  }

  // create named pipe
	for (i = 0; i < num_out_q; i++) {
    snprintf(file_name, PORT_DIR_LEN, "%s/%s/%s.rx%d", TMP_DIR,
             VPORT_DIR_PREFIX, name, i);

    mkfifo(file_name, 0666);

    port->out_irq_fd[i] = open(file_name, O_RDWR);
  }

  // set port mac address
  seed = __rdtsc();
  srand(seed);
  for (i = 0; i < 6; i++) {
    port->mac_addr[i] = rand() & 0xff;
  }
  port->mac_addr[0] &= 0xfe; // not broadcast/multicast
  port->mac_addr[0] |= 0x02; // locally administered



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
    uint16_t num_out_q = port->bar->num_out_q;
    char *name = port->bar->name;
    for (uint16_t i = 0; i < num_out_q; i++) {
      snprintf(file_name, PORT_DIR_LEN, "%s/%s/%s.rx%d", TMP_DIR,
               VPORT_DIR_PREFIX, name, i);

      unlink(file_name);
      close(port->out_irq_fd[i]);
    }

    snprintf(file_name, PORT_DIR_LEN, "%s/%s/%s", TMP_DIR, VPORT_DIR_PREFIX,
             name);
    unlink(file_name);

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
    q = port->bar->out_qs[qid];
  } else {
    q = port->bar->inc_qs[qid];
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
    q = port->bar->inc_qs[qid];
  } else {
    q = port->bar->out_qs[qid];
  }

  // TODO: update code to work with bulk
  ret = llring_dequeue_burst(q, pkts, cnt);
  return ret;
}
