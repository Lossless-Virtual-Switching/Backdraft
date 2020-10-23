// Copyright (c) 2014-2016, The Regents of the University of California.
// Copyright (c) 2016-2017, Nefeli Networks, Inc.
// All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// * Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
// * Neither the names of the copyright holders nor the names of their
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include "bd_vport.h"
#include "../utils/bkdrft.h"

#include <rte_bus_pci.h>
#include <rte_ethdev.h>
#include <rte_flow.h>

#include "../utils/bkdrft.h"
// #include "../utils/bkdrft_flow_rules.h"
#include "../utils/ether.h"
#include "../utils/format.h"


// VPORT ==============================
#include <stdio.h>
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
struct vport *from_vbar_addr(size_t bar_address)
{
  struct vport *port;
  port = (struct vport *)rte_zmalloc(NULL, sizeof(struct vport), 0);
  assert(port);
  port->_main = 0; // this is connected to someone elses vport_bar
  port->bar = (struct vport_bar *)bar_address;
  return port;
}

/* Allocate a new vport and vport_bar
 * Setup pipes
 * */
struct vport *new_vport(const char *name, uint16_t num_inc_q, uint16_t num_out_q)
{
  uint32_t i;

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
  port = (struct vport *)rte_zmalloc(NULL, sizeof(struct vport), 0);
  assert(port != NULL);
  port->_main = 1; // this is the main vport (ownership of vbar)

  // calculate how much memory is needed for vport_bar
  bytes_per_llring = llring_bytes_with_slots(SLOTS_PER_LLRING);
  bytes_per_llring = ROUND_TO_64(bytes_per_llring);
  total_memory_needed = ROUND_TO_64(sizeof(struct vport_bar)) +
                bytes_per_llring * (num_inc_q + num_out_q) +
                ROUND_TO_64(sizeof(struct vport_inc_regs)) * num_inc_q +
                ROUND_TO_64(sizeof(struct vport_out_regs)) * num_out_q;

  bar = (struct vport_bar *)rte_zmalloc(NULL, total_memory_needed, 64);
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

  // TODO: use bulk
  ret = llring_enqueue_burst(q, pkts, cnt);
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

  ret = llring_dequeue_burst(q, pkts, cnt);
  return ret;
}
// VPORT ==============================

/*!
 * The following are deprecated. Ignore us.
 */
#define SN_TSO_SG 0
#define SN_HW_RXCSUM 0
#define SN_HW_TXCSUM 0

void BDVPort::InitDriver() {
}

CommandResponse BDVPort::Init(const bess::pb::BDVPortArg &arg) {
  LOG(INFO) << "packet: " << sizeof(bess::Packet);
  int num_txq = num_queues[PACKET_DIR_OUT];
  int num_rxq = num_queues[PACKET_DIR_INC];

  int numa_node = -1;

  std::string port_name;

  port_name = arg.port_name();

  if (arg.rate_limiting()) {
    conf_.rate_limiting = true;
    uint32_t rate = arg.rate() * 1000; // 1000000;

    if (!rate)
      return CommandFailure(ENOENT, "rate not found");

    for (queue_t qid = 0; qid < VPORT_MAX_QUEUES_PER_DIR; qid++) {
      limiter_.limit[PACKET_DIR_OUT][qid] = rate;
      limiter_.limit[PACKET_DIR_INC][qid] = rate;
    }

    LOG(INFO) << "Rate limiting on " << rate << " (pps) \n";
  }

  // setup a new vport structure and pipes make sure the
  // port_name is unique.
  port_ = new_vport(port_name.c_str(), num_rxq, num_txq);

  driver_ = "N/A";

  node_placement_ =
      numa_node == -1 ? UNCONSTRAINED_SOCKET : (1ull << numa_node);

  // Reset hardware stat counters, as they may still contain previous data
  CollectStats(true);

  return CommandSuccess();
}

CommandResponse BDVPort::UpdateConf(__attribute__((unused))const Conf &conf) {
  return CommandSuccess();
}

void BDVPort::DeInit() {
  free_vport(port_);
}

void BDVPort::CollectStats(__attribute__((unused)) bool reset) {
}

int BDVPort::RecvPackets(queue_t qid, bess::Packet **pkts, int cnt) {
  int recv = 0;
  uint32_t allowed_packets = 0;

  // if (ptype_ == VHOST && conf_.rate_limiting && qid)
  if (conf_.rate_limiting) {
    allowed_packets = RateLimit(PACKET_DIR_INC, qid);
    if (allowed_packets == 0) {
      // shouldn't read any packets; this affect the rate so update the rate;
      RecordRate(PACKET_DIR_INC, qid, pkts, 0);
      return 0;
    }

    if (allowed_packets < (uint32_t)cnt) {
      cnt = allowed_packets;
    }

    recv = recv_packets_vport(port_, qid, (void **)pkts, cnt);

    UpdateTokens(PACKET_DIR_INC, qid, recv);
  } else {
    recv = recv_packets_vport(port_, qid, (void **)pkts, cnt);
  }

  RecordRate(PACKET_DIR_INC, qid, pkts, recv);
  return recv;
}

int BDVPort::SendPackets(queue_t qid, bess::Packet **pkts, int cnt) {
  int sent;
  sent = send_packets_vport(port_, qid, (void**)pkts, cnt);
  RecordRate(PACKET_DIR_OUT, qid, pkts, sent);
  return sent;
}

Port::LinkStatus BDVPort::GetLinkStatus() {
  return LinkStatus{.speed = 0U,
                    .full_duplex = true,
                    .autoneg = false,
                    .link_up = 0U}; // TODO: keep track of connected apps
                                       // in vport.
}

ADD_DRIVER(BDVPort, "bdvport_port", "Backdraft VPort")
