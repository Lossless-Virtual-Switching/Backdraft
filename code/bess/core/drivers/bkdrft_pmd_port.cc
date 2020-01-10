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

#include "bkdrft_pmd_port.h"

#include <rte_cycles.h>
#include <rte_ethdev_pci.h>

#include "../utils/ether.h"
#include "../utils/format.h"
#include "../utils/ip.h"
#include "../utils/udp.h"

/*!
 * The following are deprecated. Ignore us.
 */
#define SN_TSO_SG 0
#define SN_HW_RXCSUM 0
#define SN_HW_TXCSUM 0

// #define BACKDRAFT_DEBUG_A
#define DCB 0

// Template for generating Pause packets without data
struct [[gnu::packed]] PausePacketTemplate {
  bess::utils::Ethernet eth;

  PausePacketTemplate() {
    eth.dst_addr = bess::utils::Ethernet::Address();  // To fill in
    eth.src_addr = bess::utils::Ethernet::Address();  // To fill in
    eth.ether_type = bess::utils::be16_t(0x8808);
  }
};

static PausePacketTemplate pause_template;

static const struct rte_eth_conf default_eth_conf() {
  struct rte_eth_conf ret = rte_eth_conf();
  // struct rte_eth_dev_info dev_info;

  // rte_eth_dev_info_get(dpdk_port_t, &dev_info);

  ret.link_speeds = ETH_LINK_SPEED_AUTONEG;

  ret.rxmode.mq_mode = ETH_MQ_RX_RSS;
  ret.rxmode.ignore_offload_bitfield = 1;
  ret.rxmode.offloads |= DEV_RX_OFFLOAD_CRC_STRIP;
  ret.rxmode.offloads |= (SN_HW_RXCSUM ? DEV_RX_OFFLOAD_CHECKSUM : 0x0);

  ret.rx_adv_conf.rss_conf = {
      .rss_key = nullptr,
      .rss_key_len = 40,
      /* TODO: query rte_eth_dev_info_get() to set this*/
      .rss_hf = ETH_RSS_IP | ETH_RSS_UDP | ETH_RSS_TCP | ETH_RSS_SCTP,
  };

  return ret;
}

void BKDRFTPMDPort::InitDriver() {
  dpdk_port_t num_dpdk_ports = rte_eth_dev_count();

  LOG(INFO) << static_cast<int>(num_dpdk_ports)
            << " DPDK PMD ports have been recognized:";

  for (dpdk_port_t i = 0; i < num_dpdk_ports; i++) {
    struct rte_eth_dev_info dev_info;
    std::string pci_info;
    int numa_node = -1;
    bess::utils::Ethernet::Address lladdr;

    rte_eth_dev_info_get(i, &dev_info);

    if (dev_info.pci_dev) {
      pci_info = bess::utils::Format(
          "%08x:%02hhx:%02hhx.%02hhx %04hx:%04hx  ",
          dev_info.pci_dev->addr.domain, dev_info.pci_dev->addr.bus,
          dev_info.pci_dev->addr.devid, dev_info.pci_dev->addr.function,
          dev_info.pci_dev->id.vendor_id, dev_info.pci_dev->id.device_id);
    }

    numa_node = rte_eth_dev_socket_id(static_cast<int>(i));
    rte_eth_macaddr_get(i, reinterpret_cast<ether_addr *>(lladdr.bytes));

    LOG(INFO) << "DPDK port_id " << static_cast<int>(i) << " ("
              << dev_info.driver_name << ")   RXQ " << dev_info.max_rx_queues
              << " TXQ " << dev_info.max_tx_queues << "  " << lladdr.ToString()
              << "  " << pci_info << " numa_node " << numa_node;
  }
}

// Find a port attached to DPDK by its integral id.
// returns 0 and sets *ret_port_id to "port_id" if the port is valid and
// available.
// returns > 0 on error.
static CommandResponse find_dpdk_port_by_id(dpdk_port_t port_id,
                                            dpdk_port_t *ret_port_id) {
  if (port_id >= RTE_MAX_ETHPORTS) {
    return CommandFailure(EINVAL, "Invalid port id %d", port_id);
  }
  if (rte_eth_devices[port_id].state != RTE_ETH_DEV_ATTACHED) {
    return CommandFailure(ENODEV, "Port id %d is not available", port_id);
  }

  *ret_port_id = port_id;
  return CommandSuccess();
}

// Find a port attached to DPDK by its PCI address.
// returns 0 and sets *ret_port_id to the port_id of the port at PCI address
// "pci" if it is valid and available. *ret_hot_plugged is set to true if the
// device was attached to DPDK as a result of calling this function.
// returns > 0 on error.
static CommandResponse find_dpdk_port_by_pci_addr(const std::string &pci,
                                                  dpdk_port_t *ret_port_id,
                                                  bool *ret_hot_plugged) {
  dpdk_port_t port_id = DPDK_PORT_UNKNOWN;
  struct rte_pci_addr addr;

  if (pci.length() == 0) {
    return CommandFailure(EINVAL, "No PCI address specified");
  }

  if (eal_parse_pci_DomBDF(pci.c_str(), &addr) != 0 &&
      eal_parse_pci_BDF(pci.c_str(), &addr) != 0) {
    return CommandFailure(EINVAL,
                          "PCI address must be like "
                          "dddd:bb:dd.ff or bb:dd.ff");
  }

  dpdk_port_t num_dpdk_ports = rte_eth_dev_count();
  for (dpdk_port_t i = 0; i < num_dpdk_ports; i++) {
    struct rte_eth_dev_info dev_info;
    rte_eth_dev_info_get(i, &dev_info);

    if (dev_info.pci_dev) {
      if (rte_eal_compare_pci_addr(&addr, &dev_info.pci_dev->addr) == 0) {
        port_id = i;
        break;
      }
    }
  }

  // If still not found, maybe the device has not been attached yet
  if (port_id == DPDK_PORT_UNKNOWN) {
    int ret;
    char name[RTE_ETH_NAME_MAX_LEN];
    snprintf(name, RTE_ETH_NAME_MAX_LEN, "%08x:%02x:%02x.%02x", addr.domain,
             addr.bus, addr.devid, addr.function);

    ret = rte_eth_dev_attach(name, &port_id);

    if (ret < 0) {
      return CommandFailure(ENODEV, "Cannot attach PCI device %s", name);
    }

    *ret_hot_plugged = true;
  }

  *ret_port_id = port_id;
  return CommandSuccess();
}

/*
 * Note: When configuring for DCB operation, at port initialization, both the
 number of transmit queues and the number of receive queues must be set to 128.
*/
CommandResponse BKDRFTPMDPort::ReconfigureDCBQueueNumbers() {
  // We set the queue numbers to 128 regardless of what user has
  // requested in the bess script.

  // TODO: I have to check and remove the limitation of queue numbers
  // in the bess script.

  num_queues[PACKET_DIR_INC] = 128;
  num_queues[PACKET_DIR_OUT] = 128;

  return CommandSuccess();
}

// Find a DPDK vdev by name.
// returns 0 and sets *ret_port_id to the port_id of "vdev" if it is valid and
// available. *ret_hot_plugged is set to true if the device was attached to
// DPDK as a result of calling this function.
// returns > 0 on error.
static CommandResponse find_dpdk_vdev(const std::string &vdev,
                                      dpdk_port_t *ret_port_id,
                                      bool *ret_hot_plugged) {
  dpdk_port_t port_id = DPDK_PORT_UNKNOWN;

  if (vdev.length() == 0) {
    return CommandFailure(EINVAL, "No vdev specified");
  }

  const char *name = vdev.c_str();
  int ret = rte_eth_dev_attach(name, &port_id);

  if (ret < 0) {
    return CommandFailure(ENODEV, "Cannot attach vdev %s", name);
  }

  *ret_hot_plugged = true;
  *ret_port_id = port_id;
  return CommandSuccess();
}

CommandResponse BKDRFTPMDPort::pause_queue_setup() {
  num_queues[PACKET_DIR_INC]++;
  num_queues[PACKET_DIR_OUT]++;

  if (num_queues[PACKET_DIR_INC] < 2 || num_queues[PACKET_DIR_OUT] < 2)
    return CommandFailure(ENODEV, "Cannot attach these queues");

  return CommandSuccess();
}

CommandResponse BKDRFTPMDPort::Init(const bess::pb::BKDRFTPMDPortArg &arg) {
  dpdk_port_t ret_port_id = DPDK_PORT_UNKNOWN;

  struct rte_eth_dev_info dev_info;
  struct rte_eth_conf eth_conf;
  struct rte_eth_conf eth_conf_temp;

  struct rte_eth_rxconf eth_rxconf;
  struct rte_eth_txconf eth_txconf;

  int num_txq = num_queues[PACKET_DIR_OUT];
  int num_rxq = num_queues[PACKET_DIR_INC];

  // pause_queue_setup();

  int ret;

  int i;

  int numa_node = -1;
  etime_ = 100;
  hrtt_ = 1000;
  nactive_ = 1;  // FIXME: Still there is no notion of flows here is I will
                 // revise this once I have all flows available. I don't need it
                 // right now because I have one flow per queue, and I can
                 // adjust this number based on the number of the queues.

  LOG(INFO) << "backdraft pmd port initialized";

  CommandResponse err;
  switch (arg.port_case()) {
    case bess::pb::BKDRFTPMDPortArg::kPortId: {
      err = find_dpdk_port_by_id(arg.port_id(), &ret_port_id);
      break;
    }
    case bess::pb::BKDRFTPMDPortArg::kPci: {
      err = find_dpdk_port_by_pci_addr(arg.pci(), &ret_port_id, &hot_plugged_);
      break;
    }
    case bess::pb::BKDRFTPMDPortArg::kVdev: {
      err = find_dpdk_vdev(arg.vdev(), &ret_port_id, &hot_plugged_);
      break;
    }
    default:
      return CommandFailure(EINVAL, "No port specified");
  }

  if (err.error().code() != 0) {
    return err;
  }

  if (ret_port_id == DPDK_PORT_UNKNOWN) {
    return CommandFailure(ENOENT, "Port not found");
  }

  eth_conf = default_eth_conf();

  if (arg.loopback()) {
    eth_conf.lpbk_mode = 1;
  }

  /* Use defaut rx/tx configuration as provided by PMD drivers,
   * with minor tweaks */
  rte_eth_dev_info_get(ret_port_id, &dev_info);

  if (dev_info.driver_name) {
    driver_ = dev_info.driver_name;
  }

  eth_rxconf = dev_info.default_rxconf;

  /* #36: em driver does not allow rx_drop_en enabled */
  if (driver_ != "net_e1000_em") {
    eth_rxconf.rx_drop_en = 1;
  }

  eth_txconf = dev_info.default_txconf;
  eth_txconf.txq_flags = ETH_TXQ_FLAGS_NOVLANOFFL |
                         ETH_TXQ_FLAGS_NOMULTSEGS * (1 - SN_TSO_SG) |
                         ETH_TXQ_FLAGS_NOXSUMS * (1 - SN_HW_TXCSUM);

  if (!DCB) {
    ret = rte_eth_dev_configure(ret_port_id, num_rxq, num_txq, &eth_conf);
    if (ret != 0) {
      return CommandFailure(-ret, "rte_eth_dev_configure() failed");
    }
  } else {
    // DCB configuration starts
    ReconfigureDCBQueueNumbers();
    // Updating num_rxq and num_txq for tx and rx setup.
    // There is nothing to do with the queue numbers in
    // the eth configuration.
    num_rxq = num_queues[PACKET_DIR_INC];
    num_txq = num_queues[PACKET_DIR_OUT];

    // overload_[MAX_QUEUES_PER_DIR] = false;

    InitDCBPortConfig(ret_port_id, &eth_conf_temp);

    ret = rte_eth_dev_configure(ret_port_id, num_rxq, num_txq, &eth_conf_temp);
    if (ret != 0) {
      return CommandFailure(-ret, "DCB rte_eth_dev_configure() failed");
    }
    // DCB configuration ends

    // I only want just 4 queues
    // num_rxq = 4;
    // num_txq = 4;
  }

  rte_eth_promiscuous_enable(ret_port_id);

  // NOTE: As of DPDK 17.02, TX queues should be initialized first.
  // Otherwise the DPDK virtio PMD will crash in rte_eth_rx_burst() later.
  for (i = 0; i < num_txq; i++) {
    int sid = 0; /* XXX */

    ret = rte_eth_tx_queue_setup(ret_port_id, i, queue_size[PACKET_DIR_OUT],
                                 sid, &eth_txconf);
    if (ret != 0) {
      return CommandFailure(-ret, "rte_eth_tx_queue_setup() failed");
    }
  }

  for (i = 0; i < num_rxq; i++) {
    int sid = rte_eth_dev_socket_id(ret_port_id);

    /* if socket_id is invalid, set to 0 */
    if (sid < 0 || sid > RTE_MAX_NUMA_NODES) {
      sid = 0;
    }

    ret = rte_eth_rx_queue_setup(ret_port_id, i, queue_size[PACKET_DIR_INC],
                                 sid, &eth_rxconf,
                                 bess::PacketPool::GetDefaultPool(sid)->pool());
    if (ret != 0) {
      return CommandFailure(-ret, "rte_eth_rx_queue_setup() failed");
    }
  }

  int offload_mask = 0;
  offload_mask |= arg.vlan_offload_rx_strip() ? ETH_VLAN_STRIP_OFFLOAD : 0;
  offload_mask |= arg.vlan_offload_rx_filter() ? ETH_VLAN_FILTER_OFFLOAD : 0;
  offload_mask |= arg.vlan_offload_rx_qinq() ? ETH_VLAN_EXTEND_OFFLOAD : 0;
  if (offload_mask) {
    ret = rte_eth_dev_set_vlan_offload(ret_port_id, offload_mask);
    if (ret != 0) {
      return CommandFailure(-ret, "rte_eth_dev_set_vlan_offload() failed");
    }
  }

  ret = rte_eth_dev_start(ret_port_id);
  if (ret != 0) {
    return CommandFailure(-ret, "rte_eth_dev_start() failed");
  }
  dpdk_port_id_ = ret_port_id;

  numa_node = rte_eth_dev_socket_id(static_cast<int>(ret_port_id));
  node_placement_ =
      numa_node == -1 ? UNCONSTRAINED_SOCKET : (1ull << numa_node);

  rte_eth_macaddr_get(dpdk_port_id_,
                      reinterpret_cast<ether_addr *>(conf_.mac_addr.bytes));

  // Reset hardware stat counters, as they may still contain previous data

  for (int queue = 0; queue < num_rxq; queue++) {
    rte_eth_dev_set_rx_queue_stats_mapping(dpdk_port_id_, queue, queue);
  }

  for (int queue = 0; queue < num_txq; queue++) {
    rte_eth_dev_set_tx_queue_stats_mapping(dpdk_port_id_, queue, queue);
  }

  // for (queue_t queue = 0; queue < num_rxq ? num_rxq > num_txq : num_txq;
  //      queue++) {
  for (queue_t queue = 0; queue < num_rxq; queue++) {
    limit_[queue] =
        queue_size[queue] * 1500 * 0.5;  // Initial limit for my bql like thing!
    overload_[PACKET_DIR_INC][queue] = false;
    overload_[PACKET_DIR_OUT][queue] = false;
    pause_window_[PACKET_DIR_INC][queue] = 0;
    pause_window_[PACKET_DIR_OUT][queue] = 0;
    pause_timestamp_[PACKET_DIR_INC][queue] = tsc_to_ns(rdtsc());
    pause_timestamp_[PACKET_DIR_OUT][queue] = tsc_to_ns(rdtsc());
    pause_threshold_[PACKET_DIR_INC][queue] = 0;
    pause_threshold_[PACKET_DIR_OUT][queue] = 0;
  }

  outstanding_pkts_batch = new bess::PacketBatch();
  outstanding_pkts_batch->clear();

  llring_slots = 256;
  int err_ring = 0;
  bbql_queue_list_ =
      static_cast<struct llring **>(malloc(sizeof(struct llring *) * num_rxq));
  for (int i = 0; i < num_rxq; i++) {
    bbql_queue_list_[i] = InitllringQueue(llring_slots, &err_ring);
    if (err_ring != 0)
      return CommandFailure(-err_ring, "some error");
  }

  last_pause_message_timestamp_ = 0;
  last_pause_window_ = 0;

  CollectStats(true);

  return CommandSuccess();
}

llring *BKDRFTPMDPort::InitllringQueue(uint32_t slots, int *err) {
  int bytes = llring_bytes_with_slots(slots);
  int ret;

  llring *queue = static_cast<llring *>(aligned_alloc(alignof(llring), bytes));
  if (!queue) {
    *err = -ENOMEM;
    return nullptr;
  }

  ret = llring_init(queue, slots, 1, 1);
  if (ret) {
    std::free(queue);
    *err = -EINVAL;
    return nullptr;
  }
  return queue;
}

int BKDRFTPMDPort::UpdateConf(const Conf &conf) {
  rte_eth_dev_stop(dpdk_port_id_);

  if (conf_.mtu != conf.mtu && conf.mtu != 0) {
    int ret = rte_eth_dev_set_mtu(dpdk_port_id_, conf.mtu);
    if (ret == 0) {
      conf_.mtu = conf_.mtu;
    } else {
      LOG(WARNING) << "rte_eth_dev_set_mtu() failed: " << rte_strerror(-ret);
      return ret;
    }
  }

  if (conf_.mac_addr != conf.mac_addr && !conf.mac_addr.IsZero()) {
    ether_addr tmp;
    ether_addr_copy(reinterpret_cast<const ether_addr *>(&conf.mac_addr.bytes),
                    &tmp);
    int ret = rte_eth_dev_default_mac_addr_set(dpdk_port_id_, &tmp);
    if (ret == 0) {
      conf_.mac_addr = conf.mac_addr;
    } else {
      LOG(WARNING) << "rte_eth_dev_default_mac_addr_set() failed: "
                   << rte_strerror(-ret);
      return ret;
    }
  }

  if (conf.admin_up) {
    int ret = rte_eth_dev_start(dpdk_port_id_);
    if (ret == 0) {
      conf_.admin_up = true;
    } else {
      LOG(WARNING) << "rte_eth_dev_start() failed: " << rte_strerror(-ret);
      return ret;
    }
  }

  return 0;
}

void BKDRFTPMDPort::DeInit() {
  rte_eth_dev_stop(dpdk_port_id_);

  if (hot_plugged_) {
    char name[RTE_ETH_NAME_MAX_LEN];
    int ret;

    rte_eth_dev_close(dpdk_port_id_);
    ret = rte_eth_dev_detach(dpdk_port_id_, name);
    if (ret < 0) {
      LOG(WARNING) << "rte_eth_dev_detach(" << static_cast<int>(dpdk_port_id_)
                   << ") failed: " << rte_strerror(-ret);
    }
  }

  if (bbql_queue_list_) {
    bess::Packet *pkt;
    struct llring *queue;
    for (queue_t qid = 0; qid < num_queues[PACKET_DIR_INC]; qid++) {
      queue = bbql_queue_list_[qid];
      while (llring_sc_dequeue(queue, reinterpret_cast<void **>(&pkt)) == 0) {
        bess::Packet::Free(pkt);
      }
    }
    std::free(bbql_queue_list_);
  }
}

void BKDRFTPMDPort::CollectStats(bool reset) {
  struct rte_eth_stats stats;
  int ret;

  packet_dir_t dir;
  queue_t qid;

  if (reset) {
    rte_eth_stats_reset(dpdk_port_id_);
    return;
  }

  ret = rte_eth_stats_get(dpdk_port_id_, &stats);
  if (ret < 0) {
    LOG(ERROR) << "rte_eth_stats_get(" << static_cast<int>(dpdk_port_id_)
               << ") failed: " << rte_strerror(-ret);
    return;
  }

  VLOG(1) << bess::utils::Format(
      "PMD port %d: ipackets %" PRIu64 " opackets %" PRIu64 " ibytes %" PRIu64
      " obytes %" PRIu64 " imissed %" PRIu64 " ierrors %" PRIu64
      " oerrors %" PRIu64 " rx_nombuf %" PRIu64,
      dpdk_port_id_, stats.ipackets, stats.opackets, stats.ibytes, stats.obytes,
      stats.imissed, stats.ierrors, stats.oerrors, stats.rx_nombuf);

  port_stats_.inc.dropped = stats.imissed;

  // i40e/net_e1000_igb PMD drivers, ixgbevf and net_bonding vdevs don't
  // support per-queue stats
  if (driver_ == "net_i40e" || driver_ == "net_i40e_vf" ||
      driver_ == "net_ixgbe_vf" || driver_ == "net_bonding" ||
      driver_ == "net_e1000_igb") {
    // NOTE:
    // - if link is down, tx bytes won't increase
    // - if destination MAC address is incorrect, rx pkts won't increase
    port_stats_.inc.packets = stats.ipackets;
    port_stats_.inc.bytes = stats.ibytes;
    port_stats_.out.packets = stats.opackets;
    port_stats_.out.bytes = stats.obytes;
  } else {
    dir = PACKET_DIR_INC;
    for (qid = 0; qid < num_queues[dir]; qid++) {
      queue_stats[dir][qid].packets = stats.q_ipackets[qid];
      queue_stats[dir][qid].bytes = stats.q_ibytes[qid];
      queue_stats[dir][qid].dropped = stats.q_errors[qid];
      queue_stats[dir][qid].dpdk_bytes = queue_stats[dir][qid].bytes;
    }

    dir = PACKET_DIR_OUT;
    for (qid = 0; qid < num_queues[dir]; qid++) {
      queue_stats[dir][qid].packets = stats.q_opackets[qid];
      queue_stats[dir][qid].bytes = stats.q_obytes[qid];
      queue_stats[dir][qid].dpdk_bytes = queue_stats[dir][qid].bytes;
    }
  }
}

int BKDRFTPMDPort::RecvPackets(queue_t qid, bess::Packet **pkts, int cnt) {
  int recv = 0;

  if (qid == 0) {
    recv = SendPeriodicPauseMessage(pkts, cnt);
  } else {
    recv =
        rte_eth_rx_burst(dpdk_port_id_, qid, (struct rte_mbuf **)pkts, cnt);
    UpdateQueueOverloadStats(PACKET_DIR_OUT, qid);
    if (overload_[PACKET_DIR_INC][qid]) {
      // LOG(INFO) << "I got in " << recv << " " << (int)qid;
      bess::Packet::Free(pkts, recv);
      recv = 0;
    }
  }
  return recv;
}

int BKDRFTPMDPort::SendPackets(queue_t qid, bess::Packet **pkts, int cnt) {
  uint64_t sent_bytes = 0;
  int sent = 0;

  if (qid == 0) {
    // It is queue zero so  there is nothing that I can do!
    PauseMessageHandle(pkts, cnt);
    sent = rte_eth_tx_burst(dpdk_port_id_, qid,
                            reinterpret_cast<struct rte_mbuf **>(pkts), cnt);

    for (int i = 0; i < cnt; i++) {
      sent_bytes += pkts[i]->total_len();
    }

    for (int i = 0; i < sent; i++) {
      queue_stats[PACKET_DIR_OUT][qid].dpdk_bytes += pkts[i]->total_len();
    }

    int dropped = cnt - sent;

    queue_stats[PACKET_DIR_OUT][qid].bytes += sent_bytes;
    queue_stats[PACKET_DIR_OUT][qid].dropped += dropped;
    queue_stats[PACKET_DIR_OUT][qid].requested_hist[cnt]++;
    queue_stats[PACKET_DIR_OUT][qid].actual_hist[sent]++;
    queue_stats[PACKET_DIR_OUT][qid].diff_hist[dropped]++;

    return sent;

  } else {
    // It is not the queue zero so I would like to send the packets without
    // handling the pause messages.
    sent = rte_eth_tx_burst(dpdk_port_id_, qid,
                            reinterpret_cast<struct rte_mbuf **>(pkts), cnt);
    for (int pkt = 0; pkt < cnt; pkt++) {
      sent_bytes += pkts[pkt]->total_len();
    }

    for (int pkt = 0; pkt < sent; pkt++) {
      queue_stats[PACKET_DIR_OUT][qid].dpdk_bytes += pkts[pkt]->total_len();
    }

    UpdateQueueOverloadStats(PACKET_DIR_OUT, qid);

    int dropped = cnt - sent;

    queue_stats[PACKET_DIR_OUT][qid].bytes += sent_bytes;
    queue_stats[PACKET_DIR_OUT][qid].dropped += dropped;
    queue_stats[PACKET_DIR_OUT][qid].requested_hist[cnt]++;
    queue_stats[PACKET_DIR_OUT][qid].actual_hist[sent]++;
    queue_stats[PACKET_DIR_OUT][qid].diff_hist[dropped]++;

    return sent;
  }
}

int BKDRFTPMDPort::AggressiveSend(queue_t qid, bess::Packet **pkts, int cnt) {
  int sent = 0;
  while (sent != cnt) {
    sent = sent + rte_eth_tx_burst(dpdk_port_id_, qid,
                                   reinterpret_cast<struct rte_mbuf **>(pkts),
                                   cnt);
  }
  return sent;
}

int BKDRFTPMDPort::SensitiveSend(queue_t qid, bess::Packet **pkts, int cnt) {
  int sent = rte_eth_tx_burst(dpdk_port_id_, qid,
                              reinterpret_cast<struct rte_mbuf **>(pkts), cnt);
  return sent;
}

void BKDRFTPMDPort::RetrieveOutstandingPacketBatch(queue_t qid, int *err) {
  bess::Packet *pkt;
  while (!outstanding_pkts_batch->full()) {
    *err =
        llring_dequeue(bbql_queue_list_[qid], reinterpret_cast<void **>(&pkt));
    if (*err != 0)
      break;

    outstanding_pkts_batch->add(pkt);
  }
}

int BKDRFTPMDPort::SendPauseMessage(bess::Packet **pkts, int cnt) {
  using bess::utils::Ethernet;
  Ethernet *peth;
  Ethernet *beth;
  bess::Packet *pause_frame;
  bess::Packet *batch_packet;
  int pause_message_count = 0;

  for (queue_t queue = 1; queue < num_queues[PACKET_DIR_OUT];
       queue++) {  // We never send pause messages for qid = 0

    // if (overload_[PACKET_DIR_OUT][queue] && // One of the main differences
    // between our solution of BFC
    if (cnt) {  // Outgoing directions is getting overloaded because of the
                // slow receiver so outgoing drection
      batch_packet = pkts[pause_message_count];
      pause_frame = GeneratePauseMessage(PACKET_DIR_OUT, queue);
      beth = batch_packet->head_data<Ethernet *>();
      peth = pause_frame->head_data<Ethernet *>();
      peth->src_addr = beth->dst_addr;
      peth->dst_addr = beth->src_addr;
      bess::Packet::Free(batch_packet);
      pkts[pause_message_count] = pause_frame;
      pause_message_count++;

#ifdef BACKDRAFT_DEBUG
      LOG(INFO) << "queue " << (int)queue << " is overloaded "
                << pause_message_count;
#endif
    }
  }
  assert(pause_message_count < num_queues[PACKET_DIR_OUT]);
  return pause_message_count;
}

bess::Packet *BKDRFTPMDPort::GeneratePauseMessage(packet_dir_t dir,
                                                  queue_t qid) {
  static const uint16_t pause_op_code = 0x0001;
  char *ptr;
  // I might be able to add hrtt_ or etime_ here to pause time.
  uint16_t pause_time;

  if (overload_[dir][qid])
    pause_time = 1;
  else
    pause_time = 0;

  bess::Packet *pkt = current_worker.packet_pool()->Alloc();
  ptr = static_cast<char *>(pkt->buffer()) + SNBUF_HEADROOM;

  pkt->set_data_off(SNBUF_HEADROOM);

  constexpr size_t len =
      sizeof(pause_op_code) + sizeof(pause_time) + sizeof(qid);
  pkt->set_total_len(sizeof(pause_template) + len);
  pkt->set_data_len(sizeof(pause_template) + len);

  bess::utils::Copy(ptr, &pause_template, sizeof(pause_template));
  bess::utils::Copy(ptr + sizeof(pause_template), &pause_op_code,
                    sizeof(pause_op_code));
  bess::utils::Copy(ptr + sizeof(pause_template) + sizeof(pause_op_code),
                    &pause_time, sizeof(pause_time));
  bess::utils::Copy(
      ptr + sizeof(pause_template) + sizeof(pause_op_code) + sizeof(pause_time),
      &qid, sizeof(qid));

  return pkt;
}

void BKDRFTPMDPort::BQLUpdateLimit(queue_t qid) {
  double rate = 10;

  // rate = RateProber(PACKET_DIR_OUT, qid) / 1000000000;

  // LOG(INFO) << rate;

  if (rate == 0)
    rate = 0.01;  // This is problematic!!! I have to build this bloomfilter.

  pause_window_[PACKET_DIR_OUT][qid] =
      (queue_size[qid] + llring_count(bbql_queue_list_[qid])) * 1500 * 8 /
      rate * nactive_;  // Highly pessimistic random rate I put here!
  overload_[PACKET_DIR_OUT][qid] = true;
}

Port::LinkStatus BKDRFTPMDPort::GetLinkStatus() {
  struct rte_eth_link status;
  // rte_eth_link_get() may block up to 9 seconds, so use _nowait() variant.
  rte_eth_link_get_nowait(dpdk_port_id_, &status);

  return LinkStatus{.speed = status.link_speed,
                    .full_duplex = static_cast<bool>(status.link_duplex),
                    .autoneg = static_cast<bool>(status.link_autoneg),
                    .link_up = static_cast<bool>(status.link_status)};
}

void BKDRFTPMDPort::InitDCBPortConfig(dpdk_port_t port_id,
                                      struct rte_eth_conf *eth_conf) {
  uint8_t pfc_en = 0;
  struct rte_eth_dev_info dev_info;
  struct rte_eth_rss_conf rss_conf;
  enum rte_eth_nb_tcs tc = ETH_8_TCS;

  rss_conf = {
      .rss_key = NULL,
      .rss_key_len = 40,
      /* TODO: query rte_eth_dev_info_get() to set this*/
    .rss_hf = ETH_RSS_IP | ETH_RSS_UDP | ETH_RSS_TCP | ETH_RSS_SCTP,
  };

  rte_eth_dev_info_get(port_id, &dev_info);
  memset(eth_conf, 0, sizeof(*eth_conf));

  // memset(eth_conf, 0, sizeof(
  //   *eth_conf_temp));

  eth_conf->lpbk_mode = 0;
  eth_conf->link_speeds = ETH_LINK_SPEED_AUTONEG;
  eth_conf->rxmode.mq_mode = ETH_MQ_RX_DCB;
  eth_conf->txmode.mq_mode = ETH_MQ_TX_DCB;
  // eth_conf->rx_adv_conf.rss_conf = rss_conf;

  eth_conf->rx_adv_conf.dcb_rx_conf.nb_tcs = tc;
  eth_conf->tx_adv_conf.dcb_tx_conf.nb_tcs = tc;

  for (int i = 0; i < ETH_DCB_NUM_USER_PRIORITIES; i++) {
    eth_conf->rx_adv_conf.dcb_rx_conf.dcb_tc[i] = i % tc;
    eth_conf->tx_adv_conf.dcb_tx_conf.dcb_tc[i] = i % tc;
  }

  if (pfc_en)
    eth_conf->dcb_capability_en = ETH_DCB_PG_SUPPORT | ETH_DCB_PFC_SUPPORT;
  else
    eth_conf->dcb_capability_en = ETH_DCB_PG_SUPPORT;

  return;
}

BKDRFTPMDPort::FlowId BKDRFTPMDPort::GetFlowId(bess::Packet *pkt) {
  using bess::utils::Ethernet;
  using bess::utils::Ipv4;
  using bess::utils::Udp;

  Ethernet *eth = pkt->head_data<Ethernet *>();
  Ipv4 *ip = reinterpret_cast<Ipv4 *>(eth + 1);
  size_t ip_bytes = ip->header_length << 2;
  Udp *udp = reinterpret_cast<Udp *>(reinterpret_cast<uint8_t *>(ip) +
                                     ip_bytes);  // Assumes a l-4 header
  // TODO(joshua): handle packet fragmentation
  FlowId id = {ip->src.value(), ip->dst.value(), udp->src_port.value(),
               udp->dst_port.value(), ip->protocol};
  return id;
}

void BKDRFTPMDPort::PauseMessageHandle(bess::Packet **pkts, int cnt) {
  using bess::utils::Ethernet;
  // static bess::utils::be16_t pause_eth_type = (bess::utils::be16_t)0x8808;
  bess::Packet *pause_frame;
  char *ptr;

  static const uint16_t pause_op_code = 0x0001;
  static const uint16_t pause_eth_type = 0x8808;

  uint16_t packet_op_code;
  uint16_t packet_pause_time;
  queue_t packet_qid;

  static Ethernet *eth;
  uint64_t cur;

  for (int i = 0; i < cnt; i++) {
    pause_frame = pkts[i];
    if (pause_frame) {
      eth = pause_frame->head_data<Ethernet *>();
      if (eth->ether_type.value() == pause_eth_type) {
        // For now I assume that I want to stop just queue 0
        ptr = static_cast<char *>(pause_frame->buffer()) + SNBUF_HEADROOM;
        bess::utils::Copy(&packet_op_code, ptr + sizeof(pause_template),
                          sizeof(packet_op_code));
        bess::utils::Copy(&packet_pause_time,
                          ptr + sizeof(pause_template) + sizeof(packet_op_code),
                          sizeof(packet_pause_time));
        bess::utils::Copy(&packet_qid,
                          ptr + sizeof(pause_template) +
                              sizeof(packet_op_code) +
                              sizeof(packet_pause_time),
                          sizeof(packet_qid));
        if (packet_op_code == pause_op_code) {
          assert(0 < packet_qid < num_queues[PACKET_DIR_INC]);
#ifdef BACKDRAFT_DEBUG
          LOG(INFO) << (int)packet_qid << " " << (int)packet_pause_time << " "
                    << packet_op_code;
#endif
          cur = tsc_to_ns(rdtsc());
          pause_timestamp_[PACKET_DIR_INC][packet_qid] = cur;
          pause_window_[PACKET_DIR_INC][packet_qid] = packet_pause_time;

          // Here is the difference for BFC
          if (packet_pause_time == 0)
            overload_[PACKET_DIR_INC][packet_qid] = false;
          else 
            overload_[PACKET_DIR_INC][packet_qid] = true;
            // LOG(INFO) << "Here i receive some shit!!! "
            //          << overload_[PACKET_DIR_INC][packet_qid];
    
        }
      }
    }
  }
}

void BKDRFTPMDPort::EnqueueOutstandingPackets(bess::Packet *newpkt, queue_t qid,
                                              int *err) {
  // if the queue is full. drop the packet.
  if (llring_count(bbql_queue_list_[qid]) == llring_slots - 1) {
    // if (llring_count(&(bbql_queue_list_[qid])) < 32) {
    bess::Packet::Free(newpkt);
    *err = -4;
    return;
  }
  // LOG(INFO) << "ARE YOU GOING HERER?";
  *err =
      llring_enqueue(bbql_queue_list_[qid], reinterpret_cast<void *>(newpkt));

  if (*err != 0) {
    if (*err == -LLRING_ERR_NOBUF)
      LOG(INFO) << "Warning " << *err << " High water mark exceeded "
                << llring_count(bbql_queue_list_[qid]);
    else if (*err == -LLRING_ERR_NOBUF)
      LOG(INFO) << "Error " << *err << " We gonna have some dropps";
    else
      LOG(INFO) << "Error " << *err << " Unknown reason!";
    bess::Packet::Free(newpkt);

    *err = -5;
  }
  return;
}

bool BKDRFTPMDPort::PauseFlow(bess::Packet **pkts, int cnt) {
  using bess::utils::Ethernet;
  bess::Packet *pause_frame;
  bess::utils::be16_t eth_type;
  // bess::utils::be16_t my_type_p = (bess::utils::be16_t)102;
  bess::utils::be16_t my_type_p = (bess::utils::be16_t)0x8808;

  for (int i = 0; i < cnt; i++) {
    pause_frame = pkts[i];
    eth_type = pause_frame->head_data<Ethernet *>()->ether_type;
    if (eth_type.value() == my_type_p.value()) {
      // LOG(INFO) << dpdk_port_id_ << " PauseFlow "
      //           << " Flow Detected, skipping ... ";
      return true;
    }
  }
  return false;
}

int BKDRFTPMDPort::SendPeriodicPauseMessage(bess::Packet **pkts, int cnt) {
  int pause_messages = 0;
  uint64_t cur;
  static uint64_t last_time = 0;
  static const packet_dir_t dir = PACKET_DIR_OUT;

  for (queue_t qid = 1; qid < num_queues[dir]; qid++)
    UpdateQueueOverloadStats(dir, qid);

  cur = tsc_to_ns(rdtsc());

  if (cur - last_time > etime_) {
      pause_messages = SendPauseMessage(pkts, cnt);
      last_time = cur;
  }

  return pause_messages;
}

// double BKDRFTPMDPort::RateProber(packet_dir_t dir, queue_t qid) {
double BKDRFTPMDPort::RateProber(queue_t qid, struct rte_eth_stats stats) {
  uint64_t cur;
  static double rate = 0;
  static uint64_t last_dpdk_byte_sent = 0;
  static uint64_t last_dpdk_packet_sent_timestamp = 0;

  cur = tsc_to_ns(rdtsc());

  // rate = (queue_stats[dir][qid].dpdk_bytes - last_dpdk_byte_sent) *
  // 1000000000 /
  if (cur - last_dpdk_packet_sent_timestamp > 1000000000) {
    rate = (stats.q_obytes[qid] - last_dpdk_byte_sent) * 1000000000 /
           (tsc_to_ns(rdtsc()) - last_dpdk_packet_sent_timestamp);

    // last_dpdk_byte_sent = queue_stats[dir][qid].dpdk_bytes;
    last_dpdk_byte_sent = stats.q_obytes[qid];
    last_dpdk_packet_sent_timestamp = cur;
    LOG(INFO) << "test " << rate / 1000000000;
  }


  return rate / 1000000000;
}

void BKDRFTPMDPort::PauseThresholdUpdate(packet_dir_t dir, queue_t qid) {
  static const int link_capacity = 1500;  // MBps
  static int counter = 0;

  // rate = RateProber(PACKET_DIR_OUT, qid, stats) / 1000000000;
  // rate = RateProber(qid, stats);
  counter++;
  if (counter > 4) {
    pause_threshold_[dir][qid] = 100000000000;
    counter = 0;
  }
  else
    pause_threshold_[dir][qid] = (hrtt_ + etime_) * link_capacity / nactive_;
}

void BKDRFTPMDPort::UpdateQueueOverloadStats(packet_dir_t dir, queue_t qid) {
  struct rte_eth_stats stats;
  int64_t temp = 0;
  uint64_t queue_occupancy = 0;
  int ret;

  // Calculate the queue occupancy
  ret = rte_eth_stats_get(dpdk_port_id_, &stats);
  // queue_occupancy = stats.q_ibytes[qid] - stats.q_obytes[qid];
  temp = stats.q_obytes[qid] - stats.q_ibytes[qid];

  if (temp < 0)
    queue_occupancy = 0;
  else
    queue_occupancy = temp;

  if (ret != 0)
    LOG(INFO) << "We have some problem in retrieving the data about the queue";

  // First update the pause threshold
  PauseThresholdUpdate(dir, qid);

  // Update the status!
  if (queue_occupancy >= pause_threshold_[dir][qid])
    overload_[dir][qid] = true;
  else
    overload_[dir][qid] = false;

  /* if (overload_[dir][qid])
    LOG(INFO) << queue_occupancy << " " << pause_threshold_[dir][qid] << " "
              << overload_[dir][qid] << "  " << (int)qid << " "
              << stats.q_ibytes[qid] << " " << stats.q_obytes[qid];
        */
}

ADD_DRIVER(BKDRFTPMDPort, "bkdrft_pmd_port", "BackDraft DPDK poll mode driver")
