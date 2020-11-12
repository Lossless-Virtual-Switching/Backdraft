#ifndef _BKDRFT_H
#define _BKDRFT_H

#include "../packet.h"
#include "ether.h"
#include "ip.h"
#include "flow.h"
#include <stdint.h>

#define BKDRFT_CTRL_QUEUE (0)
#define BKDRFT_OVERLAY_PRIO (0)
#define BKDRFT_OVERLAY_VLAN_ID (100)
#define BKDRFT_MAX_MESSAGE_SIZE (128)
#define BKDRFT_PROTO_TYPE (253)
#define BKDRFT_VLAN_HEADER (0x8101)
#define BKDRFT_ARP_IP_PROTO (3)

#define BKDRFT_CTRL_MSG_TYPE ('1')
#define BKDRFT_OVERLAY_MSG_TYPE ('2')

#define IP(a, b, c, d) (a << 24 | b << 16 | c < 8 | d)

namespace bess {
namespace bkdrft {

using bess::bkdrft::Flow;

inline uint64_t total_len(bess::Packet **pkts, int count) {
  int i;
  uint64_t bytes = 0;
  for (i = 0; i < count; i++)
    bytes += pkts[i]->total_len();
  return bytes;
}

/*
 Populates the alocated pkt with headers (eth/vlan/ip/udp) and appends the
 payload to it. the address used in headers are extracted from sample_packet.
 returns the size of prepared packet. if negative the packet preparation has
 failed.
*/
int prepare_packet(bess::Packet *pkt, void *payload, size_t size,
                   const Flow *flow);

int prepare_ctrl_packet(bess::Packet * pkt, uint8_t qid, uint32_t prio,
                        uint32_t nb_pkts, uint64_t sent_bytes,
                        const Flow *flow);

/*
  Finds the payload of the pkt by traversing the headers and updates the
  `**payload` parameter to point the the begining of the data section (payload).
   does not allocate memory.
  (if pkt is freed the `*payload` pointer is not be valid any more)
  returns the size of the payload.

  if `*payload` is pointing to `nullptr` then the function has faileda.
*/
int get_packet_payload(bess::Packet *pkt, void **payload, bool only_bckdrft);

int parse_bkdrft_msg(bess::Packet *pkt, char *type, void **pb);

static inline bess::utils::Ipv4 *get_ip_header(bess::utils::Ethernet *eth) {
  using bess::utils::Ethernet;
  using bess::utils::Ipv4;
  Ipv4 *ip = nullptr;
  uint16_t ether_type = eth->ether_type.value();
  // LOG(INFO) << "Ether type: " << ether_type << "\n";
  if (ether_type == Ethernet::Type::kVlan) {
    bess::utils::Vlan *vlan = reinterpret_cast<bess::utils::Vlan *>(eth + 1);
    if (likely(vlan->ether_type.value() == Ethernet::Type::kIpv4)) {
      ip = reinterpret_cast<Ipv4 *>(vlan + 1);
    } else {
      // LOG(WARNING) << "get_ip_header: packet is not an Ip "
      //                 "packet (type: " << ether_type << ")\n";
      return nullptr;  // failed
    }
  } else if (likely(ether_type == Ethernet::Type::kIpv4)) {
    ip = reinterpret_cast<Ipv4 *>(eth + 1);
  } else {
    // LOG(WARNING) << "get_ip_header: packet is not an Ip "
    //                 "packet (type: " << ether_type << ")\n";
    return nullptr;  // failed
  }
  return ip;
}

static inline int mark_packet_with_queue_number(bess::Packet *pkt, uint8_t q) {
  using bess::utils::Ethernet;
  char *new_head;
  be32_t tag;
  uint16_t tci = q << 3 | 0 << 12 | q; // vlan (prio=3, vlan-id=q)

  new_head = static_cast<char *>(pkt->prepend(4));
  if (unlikely(new_head == nullptr)) {
    return -1;
  }

  __m128i ethh;
  ethh = _mm_loadu_si128(reinterpret_cast<__m128i *>(new_head + 4));
  be16_t tpid(be16_t::swap(_mm_extract_epi16(ethh, 6)));

  if (tpid.value() == Ethernet::Type::kVlan) {
    tag = be32_t((Ethernet::Type::kQinQ << 16) | tci);
  } else {
    tag = be32_t((Ethernet::Type::kVlan << 16) | tci);
  }
  ethh = _mm_insert_epi32(ethh, tag.raw_value(), 3);
  _mm_storeu_si128(reinterpret_cast<__m128i *>(new_head), ethh);
  return 0;  // success
}

}  // namespace bkdrft
}  // namespace bess

#endif
