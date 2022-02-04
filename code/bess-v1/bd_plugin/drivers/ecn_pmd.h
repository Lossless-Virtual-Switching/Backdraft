#ifndef BD_DRIVERS_ECN_PMD_H_
#define BD_DRIVERS_ECN_PMD_H_

#include <string>

#include <rte_config.h>
#include <rte_errno.h>
#include <rte_ethdev.h>

#include "pb/bkdrft_driver_msg.pb.h"
#include "module.h"
#include "port.h"

typedef uint16_t dpdk_port_t;

#define DPDK_PORT_UNKNOWN RTE_MAX_ETHPORTS
class ECNPMDPort final : public Port {
 public:
  ECNPMDPort()
      : Port(),
        dpdk_port_id_(DPDK_PORT_UNKNOWN),
        hot_plugged_(false),
        node_placement_(UNCONSTRAINED_SOCKET) {}

  void InitDriver() override;

  CommandResponse Init(const bkdrft::pb::ECNPMDPortArg &arg);

  void DeInit() override;

  void CollectStats(bool reset) override;

  int RecvPackets(queue_t qid, bess::Packet **pkts, int cnt) override;

  int SendPackets(queue_t qid, bess::Packet **pkts, int cnt) override;

  uint64_t GetFlags() const override {
    return 0; //DRIVER_FLAG_SELF_INC_STATS; //  | DRIVER_FLAG_SELF_OUT_STATS;
  }

  LinkStatus GetLinkStatus() override;

  CommandResponse UpdateConf(const Conf &conf) override;

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

  uint32_t ecn_threshold_;
};

#endif
