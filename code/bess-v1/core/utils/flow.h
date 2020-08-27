#ifndef _BKDRFT_FLOW_
#define _BKDRFT_FLOW_

#include <string> // std string

#include <rte_hash_crc.h>

#include "endian.h"
#include "ether.h"
#include "ip.h"
#include "icmp.h"
#include "udp.h"
#include "tcp.h"
#include "../packet.h"

using bess::utils::be32_t;
using bess::utils::be16_t;

namespace bess {
namespace bkdrft {

struct Flow {
  be32_t addr_src;
  be32_t addr_dst;
  be16_t port_src;
  be16_t port_dst;
  uint32_t protocol;
  utils::Ethernet::Address eth_dst_addr;
  utils::Ethernet::Address eth_src_addr;
  
  struct Hash {
    std::size_t operator()(const Flow &f) const
    {
      // only hash first 16 bytes, skip ethernet section
      return rte_hash_crc(&f, 16, 0);
    }
  };

  struct EqualTo {
    bool operator()(const Flow &lf, const Flow &rf) const
    {
      const union {
        Flow f;
        struct {
          uint64_t p1; // ips
          uint32_t p2; // ports
          uint32_t p3; //proto
          uint32_t p4; // eth
          uint64_t p5; // eth
        };
      } &left = {.f =lf}, &right = {.f = rf};

      // not checking ethernet addresses
      if (left.p1 == right.p1 && \
          left.p2 == right.p2 && \
          left.p3 == right.p3) {
        return true;
      }
      return false;
    }
  };
};

// static_assert(sizeof(Flow) == 16, "Flow structure size is not correct");

static const Flow empty_flow = {
  .addr_src = (be32_t)0,
  .addr_dst = (be32_t)0,
  .port_src = (be16_t)0,
  .port_dst = (be16_t)0,
  .protocol = 0,
  .eth_dst_addr = utils::Ethernet::Address(),
  .eth_src_addr = utils::Ethernet::Address(),
};

static inline Flow PacketToFlow(const bess::Packet &pkt)
{
  using bess::utils::Ethernet;
  using bess::utils::Vlan;
  using bess::utils::Ipv4;
  using IpProto = bess::utils::Ipv4::Proto;
  using bess::utils::Udp;
  using bess::utils::Tcp;
  using bess::utils::Icmp;

  Ethernet *eth = pkt.head_data<Ethernet *>();
  uint32_t ether_type = eth->ether_type.value();
  Ipv4 *ip;
  void *l4;
  size_t ip_hdr_bytes;
  be32_t ip_src;
  be32_t ip_dst;
  be16_t port_src;
  be16_t port_dst;
  IpProto proto;

  if (ether_type == Ethernet::Type::kVlan) {
    char *p = pkt.head_data<char *>();
    ip = reinterpret_cast<Ipv4 *>(p + sizeof(Ethernet) + sizeof(Vlan));
  } else if (ether_type == Ethernet::Type::kIpv4) {
    // eth + 1 skips the ethernet header
    // assuming the next header is ip
    ip =reinterpret_cast<Ipv4 *>(eth + 1);
  } else {
    ip = nullptr;
  }

  if (ip != nullptr) {
    // Ip header length tells the number of 32 bit
    // rows (4 byte rows) so multiply by four to
    // get number of bytes.
    ip_hdr_bytes = (ip->header_length) << 2;
    // Layer 4 header (TCP, UDP, ICMP, ...)
    l4 = reinterpret_cast<uint8_t *>(ip) + ip_hdr_bytes;
    // Find out ip protocol
    proto = static_cast<IpProto>(ip->protocol);
    if (likely(proto == IpProto::kTcp || proto == IpProto::kUdp)) {
        // UDP and TCP share the same layout for port numbers
        const Udp *udp = static_cast<const Udp *>(l4);
        port_src = udp->src_port;
        port_dst = udp->dst_port;
    } else {
      // only works for udp and tcp
      // LOG(WARNING) << "PacketToFlow called on non-(Udp/Tcp) packet\n";
      port_src = be16_t(50);
      port_dst = be16_t(50);
    }
    ip_src = ip->src;
    ip_dst = ip->dst;
  } else {
    l4 = nullptr;
    ip_src = be32_t(10 << 24 | 0 << 16 | 0 << 8 | 1);
    ip_dst = be32_t(10 << 24 | 0 << 16 | 0 << 8 | 2);
    port_src = be16_t(50);
    port_dst = be16_t(50);
    proto = IpProto::kRaw;
  }

  Flow f = {
      .addr_src = ip_src,
      .addr_dst = ip_dst,
      .port_src = port_src,
      .port_dst = port_dst,
      .protocol = proto,
      .eth_dst_addr = eth->dst_addr,
      .eth_src_addr = eth->src_addr,
  };
  return f;
}

static inline Flow SwapFlow(const Flow &flow) {
  Flow f = {
      .addr_src = flow.addr_dst,
      .addr_dst = flow.addr_src,
      .port_src = flow.port_dst,
      .port_dst = flow.port_src,
      .protocol = flow.protocol,
      .eth_dst_addr = flow.eth_src_addr,
      .eth_src_addr = flow.eth_dst_addr,
  };
  return f;
}

#define GET_BYTE(val, n) ((x >> (n * 8)) & 0xff)
#define IP_STR(ip) ({uint32_t x = ip;\
    std::ostringstream st; \
    st << GET_BYTE(x,3) << ":" << GET_BYTE(x,2)  << ":" << GET_BYTE(x,1) << ":" << GET_BYTE(x,0); \
    st.str();})

static inline std::string FlowToString(const Flow &flow)
{
  std::ostringstream oss;
  oss << "<flow: src_ip: " << IP_STR(flow.addr_src.value()) \
      << " dst_ip: " << IP_STR(flow.addr_dst.value()) \
      << " port_src: "  << flow.port_src.value() \
      << " port_dst: " << flow.port_dst.value() \
      << " eth_src: " << flow.eth_src_addr.ToString() \
      << " eth_dst: " << flow.eth_dst_addr.ToString() \
      << " proto: " << flow.protocol \
      << ">";
  return oss.str();
}

}
}
#endif // _BKDRFT_FLOW_
