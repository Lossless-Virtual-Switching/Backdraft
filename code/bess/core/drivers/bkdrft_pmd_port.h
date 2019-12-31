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

#ifndef BESS_DRIVERS_PMD_H_
#define BESS_DRIVERS_PMD_H_

#include <string>

#include <rte_config.h>
#include <rte_errno.h>
#include <rte_ethdev.h>

#include "../module.h"
#include "../port.h"
#include "../utils/dql.h"

typedef uint16_t dpdk_port_t;

#define DPDK_PORT_UNKNOWN RTE_MAX_ETHPORTS
/*!
 * This driver binds a port to a device using DPDK.
 * This is the recommended driver for performance.
 */
class BKDRFTPMDPort final : public Port {
 public:
  BKDRFTPMDPort()
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
  CommandResponse Init(const bess::pb::BKDRFTPMDPortArg &arg);

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

  /**
   * @brief This function is being called in the recv path of a PMDport and it
   * creates pause message in case it finds a PMDport overloaded queues. What if
   * we have pause messages more than packet batch?
   *
   * @param pkts packet batch
   * @param cnt burst size of length of the packet batch
   * @return int number of pause messages in the packet batch ready to send.
   */
  int SendPauseMessage(bess::Packet **pkts, int cnt);

  // void PeriodicOverloadUpdate(queue_t qid);

  /**
   * @brief This function manages the amount of the data should be send out of a
   * PMDport there are so many things to be tuned here, but limit is the most
   * important parameter for now. I'm also thinking of move entire BQL
   * functionality to a seperated class.
   *
   * @param qid BQL is working on per queue(per flow basis)
   */
  void BQLRequestToSend(queue_t qid);

  void BQLUpdateLimit(queue_t qid, int dropped);

  /**
   * @brief This funcation is only called or qid == 0, since only qid==0 is
   * responsible for the pause messages. We are making sure that nobody would
   * block the pause messages.
   *
   * @param pkts pointer to the batch of packets
   * @param cnt length of the packet batch
   */
  void PauseMessageHandle(bess::Packet **pkts, int cnt);

  /**
   * @brief This function create an ethernet pause message
   *
   * @param qid It supposes to pause an specific queue in a Backdraft PMDport
   * @return bess::Packet* The pointer to the newly generated pause message.
   */
  bess::Packet *GeneratePauseMessage(queue_t qid);

  struct FlowId {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint32_t src_port;
    uint32_t dst_port;
    uint8_t protocol;
  };

  uint64_t GetFlags() const override {
    return DRIVER_FLAG_SELF_INC_STATS | DRIVER_FLAG_SELF_OUT_STATS;
  }

  LinkStatus GetLinkStatus() override;

  int UpdateConf(const Conf &conf) override;

  /*!
   * Get any placement constraints that need to be met when receiving from
   * this port.
   */
  placement_constraint GetNodePlacementConstraint() const override {
    return node_placement_;
  }

  /*
   * DCB confiuration, it is just have 4 Traffic classes at the moment,
   * it is just for testing, however, TODO: I'll develop the interface in the
   * BESS script so that different DCB configurations can be passed to the
   * PMD driver without any hassle.
   */
  void InitDCBPortConfig(dpdk_port_t port_id, struct rte_eth_conf *eth_conf);

  bool PauseFlow(bess::Packet **pkts, int cnt);

  /*
   * The name is pretty verbose.
   */
  CommandResponse ReconfigureDCBQueueNumbers();

  /*
   * It leaves a queue for the pause messages for now.
   * TODO: I need to go back to this function soon.
   */
  CommandResponse pause_queue_setup();

  // void SignalOverload();

  // void SignalUnderload();

  FlowId GetFlowId(bess::Packet *pkt);

  /*
   * TODO: Backdraft: It requires a lot of modification but just for the sake
   * of testing!
   * It should go to private side!
   * It requires some access functions.
   */
  // Parent tasks of this module in the current pipeline.
  // std::vector<Module *> parent_tasks_;
  // std::vector<Module *> bp_parent;

 private:
  /*!
   * The DPDK port ID number (set after binding).
   */
  dpdk_port_t dpdk_port_id_;

  /*
   * TODO: Backdraft: It requires a lot of modification but just for the sake
   * of testing!
   * It should go to private side!
   * It needs some access functions.
   */
  // Whether the module itself is overloaded.
  bool overload_[MAX_QUEUES_PER_DIR];

  // bool overload_[PACKET_DIRS][MAX_QUEUES_PER_DIR];

  /*!
   * True if device did not exist when bessd started and was later patched in.
   */
  bool hot_plugged_;

  /*
   * number of active flows, maybe it should be per queue
   */
  int nactive_;

  /*
   * half rrt + effect time
   */
  int hrtt_;

  /*
   * effect time
   */
  int etime_;

  uint64_t pause_window_[MAX_QUEUES_PER_DIR];

  uint64_t pause_timestamp_[MAX_QUEUES_PER_DIR];

  int limit_[MAX_QUEUES_PER_DIR];

  /**
   * This clasee would manage the queuing at the sender
   * and here we would understand if we should send anything or
   * we should pause. This is my understanding for now.
   */
  bess::utils::dql bql;

  /*!
   * The NUMA node to which device is attached
   */

  placement_constraint node_placement_;

  std::string driver_;  // ixgbe, i40e, ...
};

#endif  // BESS_DRIVERS_PMD_H_
