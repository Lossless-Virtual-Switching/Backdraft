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

#include "../kmod/llring.h"
#include <rte_config.h>
#include <rte_errno.h>
#include <rte_ethdev.h>

#include "../module.h"
#include "../pktbatch.h"
#include "../port.h"
#include "../utils/cuckoo_map.h"
#include "../utils/ip.h"

using bess::utils::CuckooMap;
using bess::utils::Ipv4Prefix;
// #include "../utils/dql.h"

typedef uint16_t dpdk_port_t;

#define DPDK_PORT_UNKNOWN RTE_MAX_ETHPORTS

/*!
 * This driver binds a port to a device using DPDK.
 * This is the recommended driver for performance.
 */
class BFCPMDPort final : public Port {
 public:
  BFCPMDPort()
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
  CommandResponse Init(const bess::pb::BFCPMDPortArg &arg);

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

  /**
   * @brief This function manages the amount of the data should be send out of a
   * PMDport there are so many things to be tuned here, but limit is the most
   * important parameter for now. I'm also thinking of move entire BQL
   * functionality to a seperated class.
   *
   * @param qid BQL is working on per queue(per flow basis)
   */
  void BQLUpdateLimit(queue_t qid);

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
  bess::Packet *GeneratePauseMessage(packet_dir_t dir, queue_t qid);

  struct FlowId {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint32_t src_port;
    uint32_t dst_port;
    // uint8_t src_maddr[6];
    // uint8_t dst_maddr[6];
    uint8_t protocol;
  };

  struct Flow {
    uint64_t queue_number;      // physical queue it has been assigned to
    bool pause_status;          // whether it is paused or not?
    FlowId VFID;                // allows the flow to remove itself from the map
    int queued_packets;         // How many packets are queued?
    Flow() : queue_number(0), pause_status(false), VFID() {};
    Flow(FlowId new_id, queue_t qid)
        : queue_number(qid), pause_status(false), VFID(new_id){};
    ~Flow(){};
  };

  // hashes a FlowId
  struct Hash {
    // a similar method to boost's hash_combine in order to combine hashes
    inline void combine(std::size_t &hash, const unsigned int &val) const {
      std::hash<unsigned int> hasher;
      hash ^= hasher(val) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    }
    bess::utils::HashResult operator()(const FlowId &id) const {
      std::size_t hash = 0;
      combine(hash, id.src_ip);
      combine(hash, id.dst_ip);
      combine(hash, id.src_port);
      combine(hash, id.dst_port);
      combine(hash, (uint32_t)id.protocol);
      return hash;
    }
  };

  struct EqualTo {
    bool operator()(const FlowId &id1, const FlowId &id2) const {
      bool ips = (id1.src_ip == id2.src_ip) && (id1.dst_ip == id2.dst_ip);
      bool ports =
          (id1.src_port == id2.src_port) && (id1.dst_port == id2.dst_port);
      return (ips && ports) && (id1.protocol == id2.protocol);
    }
  };

  FlowId GetId(bess::Packet *pkt);

  // void BookingRecv(queue_t qid, bess::Packet **pkts, int cnt);
  void BookingEnqueue(queue_t qid, bess::Packet *pkt);

  void BookingDequeue(bess::Packet *pkt);

  uint64_t GetFlags() const override {
    return DRIVER_FLAG_SELF_INC_STATS | DRIVER_FLAG_SELF_OUT_STATS;
  };

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
    void InitDCBPortConfig(dpdk_port_t port_id, struct rte_eth_conf * eth_conf);

    bool PauseFlow(bess::Packet * *pkts, int cnt);

    void EnqueueOutstandingPackets(bess::Packet * newpkt, queue_t qid,
                                   int *err);

    void RetrieveOutstandingPacketBatch(queue_t qid, int *err);

    llring *InitllringQueue(uint32_t slots, int *err);

    int AggressiveSend(queue_t qid, bess::Packet * *pkts, int cnt);

    int SensitiveSend(queue_t qid, bess::Packet * *pkts, int cnt);

    int SendPeriodicPauseMessage(bess::Packet * *pkts, int cnt);

    // double RateProber(packet_dir_t dir, queue_t qid, struct rte_eth_stats
    // stats);
    double RateProber(queue_t qid, struct rte_eth_stats stats);

    void PauseThresholdUpdate(packet_dir_t dir, queue_t qid);

    void UpdateQueueOverloadStats(packet_dir_t dir, queue_t qid);

    void InitPauseThreshold();

    int SendPacketsllring(queue_t qid, bess::Packet **pkts, int cnt);

    int SendPacketsDefault(queue_t qid, bess::Packet **pkts, int cnt);
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

    /*
     * TODO: Backdraft: It requires a lot of modification but just for the sake
     * of testing!
     * It should go to private side!
     * It requires some access functions.
     */
    // Parent tasks of this module in the current pipeline.
    // std::vector<Module *> parent_tasks_;
    // std::vector<Module *> bp_parent;
    static CuckooMap<FlowId, Flow *, Hash, EqualTo> book_;

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
    bool overload_[PACKET_DIRS][MAX_QUEUES_PER_DIR];

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
     * effect time or tau is BFC paper
     */
    uint32_t etime_;

    uint32_t llring_slots;

    uint64_t last_bess_bytes_[PACKET_DIRS][MAX_QUEUES_PER_DIR];

    uint64_t overcap_;

    uint64_t prev_overcap_;

    uint64_t pause_window_[PACKET_DIRS][MAX_QUEUES_PER_DIR];

    uint64_t pause_timestamp_[PACKET_DIRS][MAX_QUEUES_PER_DIR];

    uint64_t limit_[MAX_QUEUES_PER_DIR];

    struct llring **bbql_queue_list_;

    bess::PacketBatch *outstanding_pkts_batch;

    uint64_t last_pause_message_timestamp_;

    uint64_t last_pause_window_;

    uint64_t test1;

    uint64_t test2;

    // In bytes
    uint64_t pause_threshold_[PACKET_DIRS][MAX_QUEUES_PER_DIR];


    /*!
     * The NUMA node to which device is attached
     */

    placement_constraint node_placement_;

    std::string driver_;  // ixgbe, i40e, ...
};

#endif  // BESS_DRIVERS_PMD_H_
