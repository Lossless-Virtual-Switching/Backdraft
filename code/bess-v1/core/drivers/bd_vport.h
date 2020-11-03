// Copyright (c) 2014-2016, The Regents of the University of California.
// Copyright (c) 2016-2017, Nefeli Networks, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
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

#ifndef BESS_DRIVERS_BDVPORT_H_
#define BESS_DRIVERS_BDVPORT_H_

// VPORT =================================
#include <stdint.h>
#include "../kmod/llring.h"

// #define MAX_QUEUES_PER_DIR 16384
#define MAX_QUEUES_PER_DIR 128

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
  uint8_t mac_addr[6];
  int out_irq_fd[MAX_QUEUES_PER_DIR];
};

struct vport *from_vport_name(char *port_name);
struct vport *from_vbar_addr(size_t bar_address);
struct vport *new_vport(const char *name, uint16_t num_inc_q,
                        uint16_t num_out_q);
int free_vport(struct vport *port);
int send_packets_vport(struct vport *port, uint16_t qid, void**pkts, int cnt);
int recv_packets_vport(struct vport *port, uint16_t qid, void**pkts, int cnt);
// VPORT =================================

#include <string>

#include <rte_config.h>
#include <rte_errno.h>
#include <rte_ethdev.h>

#include "../module.h"
#include "../port.h"

typedef uint16_t dpdk_port_t;

#define DPDK_PORT_UNKNOWN RTE_MAX_ETHPORTS
class BDVPort final : public Port {
 public:
  BDVPort()
      : Port(),
        dpdk_port_id_(DPDK_PORT_UNKNOWN),
        hot_plugged_(false),
        node_placement_(UNCONSTRAINED_SOCKET) {}

  void InitDriver() override;

  /*!
   * Initialize the port. Doesn't actually bind to the device, just grabs all
   * the parameters. InitDriver() does the binding.
   *
   * PARAMETERS:
   * * bool loopback : Is this a loopback device?
   * * uint32 port_id : The DPDK port ID for the device to bind to.
   * * string pci : The PCI address of the port to bind to.
   * * string vdev : If a virtual device, the virtual device address (e.g.
   * tun/tap)
   *
   * EXPECTS:
   * * Must specify exactly one of port_id or PCI or vdev.
   */
  CommandResponse Init(const bess::pb::BDVPortArg &arg);

  /*!
   * Release the device.
   */
  void DeInit() override;

  /*!
   * Copies rte port statistics into queue_stats datastructure (see port.h).
   *
   * PARAMETERS:
   * * bool reset : if true, reset DPDK local statistics and return (do not
   * collect stats).
   */
  void CollectStats(bool reset) override;

  /*!
   * Receives packets from the device.
   *
   * PARAMETERS:
   * * queue_t quid : NIC queue to receive from.
   * * bess::Packet **pkts   : buffer to store received packets in to.
   * * int cnt  : max number of packets to pull.
   *
   * EXPECTS:
   * * Only call this after calling Init with a device.
   * * Don't call this after calling DeInit().
   *
   * RETURNS:
   * * Total number of packets received (<=cnt)
   */
  int RecvPackets(queue_t qid, bess::Packet **pkts, int cnt) override;

  /*!
   * Sends packets out on the device.
   *
   * PARAMETERS:
   * * queue_t quid : NIC queue to transmit on.
   * * bess::Packet ** pkts   : packets to transmit.
   * * int cnt  : number of packets in pkts to transmit.
   *
   * EXPECTS:
   * * Only call this after calling Init with a device.
   * * Don't call this after calling DeInit().
   *
   * RETURNS:
   * * Total number of packets sent (<=cnt).
   */
  int SendPackets(queue_t qid, bess::Packet **pkts, int cnt) override;

  uint64_t GetFlags() const override {
    return 0; // DRIVER_FLAG_SELF_INC_STATS; //  | DRIVER_FLAG_SELF_OUT_STATS;
  }

  LinkStatus GetLinkStatus() override;

  CommandResponse UpdateConf(const Conf &conf) override;

  /*!
   * Get any placement constraints that need to be met when receiving from this
   * port.
   */
  placement_constraint GetNodePlacementConstraint() const override {
    return node_placement_;
  }

 private:
  /*!
   * The DPDK port ID number (set after binding).
   */

  dpdk_port_t dpdk_port_id_;

  /*!
   * True if device did not exist when bessd started and was later patched in.
   */
  bool hot_plugged_;

  /*!
   * The NUMA node to which device is attached
   */
  placement_constraint node_placement_;

  std::string driver_;  // ixgbe, i40e, ...
  struct vport *port_;
};


#endif  // BESS_DRIVERS_BDVPort_H_
