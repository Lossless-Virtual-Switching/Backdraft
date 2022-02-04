#include "ecn_pmd.h"
#include "utils/bkdrft.h"

#include <rte_bus_pci.h>
#include <rte_ethdev.h>
#include <rte_flow.h>

#include "utils/bkdrft.h"
#include "utils/bkdrft_flow_rules.h"
#include "utils/ether.h"
#include "utils/ip.h"
#include "utils/format.h"

#define SN_TSO_SG 0
#define SN_HW_RXCSUM 0
#define SN_HW_TXCSUM 0

static inline void ecnMark(bess::Packet *pkt) {
  using bess::utils::Ethernet;
  using bess::utils::Ipv4;
  // if (pkt->data_len() < 34) {
  //     return;
  // }
  // TODO: make sure pkt is for a TCP/IPv4  stack
  Ethernet *eth = pkt->head_data<Ethernet *>();
  Ipv4 *ip = reinterpret_cast<Ipv4 *>(eth + 1);
  ip->type_of_service = 0x03;
}

static const struct rte_eth_conf default_eth_conf(
        struct rte_eth_dev_info dev_info, int num_rxq) {
    struct rte_eth_conf ret = rte_eth_conf();
    uint64_t rss_hf = ETH_RSS_IP | ETH_RSS_UDP | ETH_RSS_TCP | ETH_RSS_SCTP;

    if (num_rxq <= 1) {
        rss_hf = 0;
    } else if (dev_info.flow_type_rss_offloads) {
        rss_hf = dev_info.flow_type_rss_offloads;
    } else {
        rss_hf = 0x0;
    }
    ret.link_speeds = ETH_LINK_SPEED_AUTONEG;
    ret.rxmode.mq_mode = ETH_MQ_RX_RSS;
    ret.rxmode.offloads |= (SN_HW_RXCSUM ? DEV_RX_OFFLOAD_CHECKSUM : 0x0);
    ret.rx_adv_conf.rss_conf = {
        .rss_key = nullptr,
        .rss_key_len = 40,
        .rss_hf = rss_hf,
    };
    return ret;
}

/* These rules set which packet should be placed on which queue
 * */
static int _data_mapping_rule_setup(dpdk_port_t port_id, uint16_t count_queue) {
    struct rte_flow *flow;
    struct rte_flow_error error;
    const uint8_t tos_mask = 0xfc;
    // uint16_t vlan_id;
    // uint16_t prio = 3; // Notice: Only mapping vlan packets with prio=3
    // uint16_t vlan_mask = 0x0fff;

    // mapping vlan_id (i) -> queue (i - 1) for i in [1, count_queue]
    for (int i = 0; i < count_queue; i++) {
        uint8_t tos = i << 2;
        flow = bkdrft::filter_by_ip_tos(port_id, tos, tos_mask, i, &error);
        // vlan_id = i + 1;
        // flow = bkdrft::filter_by_vlan_id(port_id, prio, vlan_id, vlan_mask,
        //                                 i, &error);
        if (!flow) {
            LOG(INFO) << "Data mapping error message: " << error.message << " \n";
            return -1;  // failed
        }
        // also consider ingress packets having vlan
        // prio: 3, vlan_id: 100
        flow = bkdrft::filter_by_ip_tos_with_vlan(port_id, tos,
                tos_mask, 3, 100, 0xefff, i, &error);

        if (!flow) {
            LOG(INFO) << "Data mapping error message: " << error.message << " \n";
            return -1;  // failed
        }
    }
    return 0;
}

void ECNPMDPort::InitDriver() {
    dpdk_port_t num_dpdk_ports = rte_eth_dev_count_avail();

    LOG(INFO) << static_cast<int>(num_dpdk_ports)
        << " DPDK PMD ports have been recognized:";

    for (dpdk_port_t i = 0; i < num_dpdk_ports; i++) {
        struct rte_eth_dev_info dev_info;
        struct rte_pci_device *pci_dev;
        struct rte_bus *bus = nullptr;
        std::string pci_info;
        int numa_node = -1;
        bess::utils::Ethernet::Address lladdr;

        rte_eth_dev_info_get(i, &dev_info);

        numa_node = rte_eth_dev_socket_id(static_cast<int>(i));
        rte_eth_macaddr_get(i, reinterpret_cast<rte_ether_addr *>(lladdr.bytes));

        if (dev_info.device) {
            bus = rte_bus_find_by_device(dev_info.device);
            if (bus && !strcmp(bus->name, "pci")) {
                pci_dev = RTE_DEV_TO_PCI(dev_info.device);
                pci_info = bess::utils::Format(
                        "%08x:%02hhx:%02hhx.%02hhx %04hx:%04hx  ", pci_dev->addr.domain,
                        pci_dev->addr.bus, pci_dev->addr.devid, pci_dev->addr.function,
                        pci_dev->id.vendor_id, pci_dev->id.device_id);
            }
        }

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
    struct rte_bus *bus = nullptr;
    const struct rte_pci_device *pci_dev;

    if (pci.length() == 0) {
        return CommandFailure(EINVAL, "No PCI address specified");
    }

    if (rte_pci_addr_parse(pci.c_str(), &addr) != 0) {
        return CommandFailure(EINVAL,
                "PCI address must be like "
                "dddd:bb:dd.ff or bb:dd.ff");
    }

    dpdk_port_t num_dpdk_ports = rte_eth_dev_count_avail();
    for (dpdk_port_t i = 0; i < num_dpdk_ports; i++) {
        struct rte_eth_dev_info dev_info;
        rte_eth_dev_info_get(i, &dev_info);

        if (dev_info.device) {
            bus = rte_bus_find_by_device(dev_info.device);
            if (bus && !strcmp(bus->name, "pci")) {
                pci_dev = RTE_DEV_TO_PCI(dev_info.device);
                if (rte_pci_addr_cmp(&addr, &pci_dev->addr) == 0) {
                    port_id = i;
                    break;
                }
            }
        }
    }

    // If still not found, maybe the device has not been attached yet
    if (port_id == DPDK_PORT_UNKNOWN) {
        int ret;
        char name[RTE_ETH_NAME_MAX_LEN];
        snprintf(name, RTE_ETH_NAME_MAX_LEN, "%08x:%02x:%02x.%02x", addr.domain,
                addr.bus, addr.devid, addr.function);

        struct rte_devargs da;
        memset(&da, 0, sizeof(da));
        da.bus = bus;
        ret = rte_eal_hotplug_add(bus->name, name, da.args);

        if (ret < 0) {
            return CommandFailure(ENODEV, "Cannot attach PCI device %s", name);
        }
        ret = rte_eth_dev_get_port_by_name(name, &port_id);
        if (ret < 0) {
            return CommandFailure(ENODEV, "Cannot find port id for PCI device %s",
                    name);
        }
        *ret_hot_plugged = true;
    }

    *ret_port_id = port_id;
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

    int ret = rte_dev_probe(vdev.c_str());
    if (ret < 0) {
        return CommandFailure(ENODEV, "Cannot attach vdev %s", vdev.c_str());
    }

    struct rte_dev_iterator iterator;
    RTE_ETH_FOREACH_MATCHING_DEV(port_id, vdev.c_str(), &iterator) {
        LOG(INFO) << "port id: " << port_id << "matches vdev: " << vdev;
        rte_eth_iterator_cleanup(&iterator);
        break;
    }

    *ret_hot_plugged = true;
    *ret_port_id = port_id;
    return CommandSuccess();
}

CommandResponse ECNPMDPort::Init(const bkdrft::pb::ECNPMDPortArg &arg) {
    dpdk_port_t ret_port_id = DPDK_PORT_UNKNOWN;

    struct rte_eth_dev_info dev_info;
    struct rte_eth_conf eth_conf;
    struct rte_eth_rxconf eth_rxconf;
    struct rte_eth_txconf eth_txconf;

    int num_txq = num_queues[PACKET_DIR_OUT];
    int num_rxq = num_queues[PACKET_DIR_INC];

    int ret;

    int i;

    int numa_node = -1;

    CommandResponse err;
    switch (arg.port_case()) {
        case bkdrft::pb::ECNPMDPortArg::kPortId:
            err = find_dpdk_port_by_id(arg.port_id(), &ret_port_id);
            ptype_ = NIC;
            break;
        case bkdrft::pb::ECNPMDPortArg::kPci:
            err = find_dpdk_port_by_pci_addr(arg.pci(),
                    &ret_port_id, &hot_plugged_);
            ptype_ = NIC;
            break;

        case bkdrft::pb::ECNPMDPortArg::kVdev:
            err = find_dpdk_vdev(arg.vdev(), &ret_port_id, &hot_plugged_);
            ptype_ = VHOST;
            break;
        default:
            return CommandFailure(EINVAL, "No port specified");
    }

    if (err.error().code() != 0) {
        return err;
    }

    if (ret_port_id == DPDK_PORT_UNKNOWN) {
        return CommandFailure(ENOENT, "Port not found");
    }

    if (arg.rate_limiting()) {
        conf_.rate_limiting = true;
        uint32_t rate = arg.rate() * 1000;
        if (!rate)
            return CommandFailure(ENOENT, "rate not found");

        for (queue_t qid = 0; qid < MAX_QUEUES_PER_DIR; qid++) {
            limiter_.limit[PACKET_DIR_OUT][qid] = rate;
            limiter_.limit[PACKET_DIR_INC][qid] = rate;
        }

        LOG(INFO) << "Rate limiting on " << rate << "\n";
    }

    /* Use defaut rx/tx configuration as provided by PMD drivers,
     * with minor tweaks */
    rte_eth_dev_info_get(ret_port_id, &dev_info);
    eth_conf = default_eth_conf(dev_info, num_rxq);
    if (arg.loopback()) {
        eth_conf.lpbk_mode = 1;
    }
    if (dev_info.driver_name) {
        driver_ = dev_info.driver_name;
    }
    eth_rxconf = dev_info.default_rxconf;

    /* #36: em driver does not allow rx_drop_en enabled */
    if (driver_ != "net_e1000_em") {
        eth_rxconf.rx_drop_en = 1;
    }

    ret = rte_eth_dev_configure(ret_port_id, num_rxq, num_txq, &eth_conf);
    if (ret != 0) {
        return CommandFailure(-ret, "rte_eth_dev_configure() failed");
    }

    rte_eth_promiscuous_enable(ret_port_id);

    eth_txconf = dev_info.default_txconf;

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

    // ---------- data mapping ----------- //
    if (arg.data_mapping()) {
        ret = _data_mapping_rule_setup(ret_port_id, num_rxq);
        LOG(INFO) << "Setup data mapping rule\n";
        if (ret != 0) {
            return CommandFailure(-ret, "priority mapping rule setup failed.");
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
            reinterpret_cast<rte_ether_addr *>(conf_.mac_addr.bytes));

    // ECN
    ecn_threshold_ = 64;

    // Reset hardware stat counters, as they may still contain previous data
    CollectStats(true);

    for (int queue = 0; queue < num_rxq; queue++) {
        rte_eth_dev_set_rx_queue_stats_mapping(dpdk_port_id_, queue, queue);
    }

    for (int queue = 0; queue < num_txq; queue++) {
        rte_eth_dev_set_tx_queue_stats_mapping(dpdk_port_id_, queue, queue);
    }

    return CommandSuccess();
}

CommandResponse ECNPMDPort::UpdateConf(const Conf &conf) {
    rte_eth_dev_stop(dpdk_port_id_);

    if (conf_.mtu != conf.mtu && conf.mtu != 0) {
        if (conf.mtu > SNBUF_DATA || conf.mtu < RTE_ETHER_MIN_MTU) {
            return CommandFailure(EINVAL, "mtu should be >= %d and <= %d",
                    RTE_ETHER_MIN_MTU, SNBUF_DATA);
        }

        int ret = rte_eth_dev_set_mtu(dpdk_port_id_, conf.mtu);
        if (ret == 0) {
            conf_.mtu = conf.mtu;
        } else {
            return CommandFailure(-ret, "rte_eth_dev_set_mtu() failed");
        }
    }

    if (conf_.mac_addr != conf.mac_addr && !conf.mac_addr.IsZero()) {
        rte_ether_addr tmp;
        rte_ether_addr_copy(
                reinterpret_cast<const rte_ether_addr *>(&conf.mac_addr.bytes), &tmp);
        int ret = rte_eth_dev_default_mac_addr_set(dpdk_port_id_, &tmp);
        if (ret == 0) {
            conf_.mac_addr = conf.mac_addr;
        } else {
            return CommandFailure(-ret, "rte_eth_dev_default_mac_addr_set() failed");
        }
    }

    if (conf.admin_up) {
        int ret = rte_eth_dev_start(dpdk_port_id_);
        if (ret == 0) {
            conf_.admin_up = true;
        } else {
            return CommandFailure(-ret, "rte_eth_dev_start() failed");
        }
    }

    return CommandSuccess();
}

void ECNPMDPort::DeInit() {
    struct rte_flow_error error;
    int ret;

    ret = rte_flow_flush(dpdk_port_id_, &error);

    if (ret != 0)
        LOG(WARNING) << "rte_flow_flush(" << static_cast<int>(dpdk_port_id_)
            << ") failed: " << rte_strerror(-ret);

    rte_eth_dev_stop(dpdk_port_id_);

    if (hot_plugged_) {
        struct rte_eth_dev_info dev_info;
        struct rte_bus *bus = nullptr;
        rte_eth_dev_info_get(dpdk_port_id_, &dev_info);

        char name[RTE_ETH_NAME_MAX_LEN];

        if (dev_info.device) {
            bus = rte_bus_find_by_device(dev_info.device);
            if (rte_eth_dev_get_name_by_port(dpdk_port_id_, name) == 0) {
                rte_eth_dev_close(dpdk_port_id_);
                ret = rte_eal_hotplug_remove(bus->name, name);
                if (ret < 0) {
                    LOG(WARNING) << "rte_eal_hotplug_remove("
                        << static_cast<int>(dpdk_port_id_)
                        << ") failed: " << rte_strerror(-ret);
                }
                return;
            } else {
                LOG(WARNING) << "rte_eth_dev_get_name failed for port"
                    << static_cast<int>(dpdk_port_id_);
            }
        } else {
            LOG(WARNING) << "rte_eth_def_info_get failed for port"
                << static_cast<int>(dpdk_port_id_);
        }

        rte_eth_dev_close(dpdk_port_id_);
    }
}

void ECNPMDPort::CollectStats(bool reset) {
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

    // i40e/net_e1000_igb PMD drivers, ixgbevf and net_bonding vdevs don't support
    // per-queue stats
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
        }

        dir = PACKET_DIR_OUT;
        for (qid = 0; qid < num_queues[dir]; qid++) {
            queue_stats[dir][qid].packets = stats.q_opackets[qid];
            queue_stats[dir][qid].bytes = stats.q_obytes[qid];
        }
    }
}

int ECNPMDPort::RecvPackets(queue_t qid, bess::Packet **pkts, int cnt) {
    int recv = 0;
    uint32_t allowed_packets = 0;
    if (conf_.rate_limiting) {
        allowed_packets = RateLimit(PACKET_DIR_INC, qid);
        if (allowed_packets == 0) {
            // shouldn't read any packets; we affect the rate so update the rate;
            RecordRate(PACKET_DIR_INC, qid, pkts, 0);
            return 0;
        }
        if (allowed_packets < (uint32_t)cnt) {
            cnt = allowed_packets;
        }
        recv = rte_eth_rx_burst(dpdk_port_id_, qid, (struct rte_mbuf **)pkts, cnt);
        UpdateTokens(PACKET_DIR_INC, qid, recv);
    } else {
        recv = rte_eth_rx_burst(dpdk_port_id_, qid, (struct rte_mbuf **)pkts, cnt);
    }
    RecordRate(PACKET_DIR_INC, qid, pkts, recv);
    return recv;
}

int ECNPMDPort::SendPackets(queue_t qid, bess::Packet **pkts, int cnt) {
    int buf_len = rte_eth_rx_queue_count(dpdk_port_id_, qid);
    int left_space = ecn_threshold_ - buf_len;
    if (left_space < 0) {
        left_space = 0;
    }
    // for (int i = 0; i < cnt; i++) {
    for (int i = left_space; i < cnt; i++) {
        ecnMark(pkts[i]);
    }
    int sent = rte_eth_tx_burst(dpdk_port_id_, qid,
            reinterpret_cast<struct rte_mbuf **>(pkts), cnt);
    RecordRate(PACKET_DIR_OUT, qid, pkts, sent);
    return sent;
}

Port::LinkStatus ECNPMDPort::GetLinkStatus() {
    struct rte_eth_link status;
    // rte_eth_link_get() may block up to 9 seconds, so use _nowait() variant.
    rte_eth_link_get_nowait(dpdk_port_id_, &status);

    return LinkStatus{.speed = status.link_speed,
        .full_duplex = static_cast<bool>(status.link_duplex),
        .autoneg = static_cast<bool>(status.link_autoneg),
        .link_up = static_cast<bool>(status.link_status)};
}

ADD_DRIVER(ECNPMDPort, "ecn_pmd_port", "DPDK poll mode driver with ECN marking")
