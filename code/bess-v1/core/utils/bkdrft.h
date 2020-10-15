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

int prepare_ctrl_packet(bess::Packet * pkt, uint8_t qid, uint32_t nb_pkts,
                        uint64_t sent_bytes, const Flow *flow);

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
      //                 "packet\n";
      return nullptr;  // failed
    }
  } else if (likely(ether_type == Ethernet::Type::kIpv4)) {
    ip = reinterpret_cast<Ipv4 *>(eth + 1);
  } else {
    // LOG(WARNING) << "get_ip_header: packet is not an Ip "
    //                 "packet\n";
    return nullptr;  // failed
  }
  return ip;
}

static inline int _mark_with_ip_header(bess::Packet *pkt, uint8_t qid) {
  // TODO: It is realy a pity that we can not use vlan header
  // for issue with mellanox
  using bess::utils::Ipv4;
  using bess::utils::Ethernet;
	char *new_head;
  const int new_header_size = 20;
  const uint16_t new_ether_type = Ethernet::Type::kIpv4;
  struct Ipv4 *ipv4_hdr;

  new_head = static_cast<char *>(pkt->prepend(new_header_size));
	if (new_head != nullptr) {
		// shift 14 bytes to the left by 20 bytes
		__m128i ethh;

    // load 16 byte to register for processing
		ethh = _mm_loadu_si128(reinterpret_cast<__m128i *>(new_head + 4));

    // get the current ether_type value (from reg)
		// uint16_t tpid(_mm_extract_epi16(ethh, 6));

    // insert vlan header which is 4 bytes or 32bit at 4 element (in reg)
		ethh = _mm_insert_epi16(ethh, new_ether_type, 6);

    // sotre new ether net header. new_head is 4 byte before the original
    // header position (update memory)
		_mm_storeu_si128(reinterpret_cast<__m128i *>(new_head), ethh);

    // find the place where ip header will be placed
    ipv4_hdr = reinterpret_cast<struct Ipv4 *>(new_head + 14);

    // fill new ipv4 header
    ipv4_hdr->header_length = 5;
    ipv4_hdr->version = 5;
    ipv4_hdr->type_of_service = qid << 2;
    ipv4_hdr->length = be16_t(20);
    ipv4_hdr->id = be16_t(0);
    ipv4_hdr->fragment_offset = be16_t(0);
    ipv4_hdr->ttl = 32;
    ipv4_hdr->protocol = BKDRFT_ARP_IP_PROTO; // TODO: this is only for arp?
    ipv4_hdr->checksum = 0;
    ipv4_hdr->src = be32_t(0);  // TODO: should I ?
    ipv4_hdr->dst = be32_t(0);  // TODO: ?

    return 0;
  }
  LOG(INFO) << "Failed to add bkdrft vlan like header\n";
  return -1; //failed
}

static inline int remove_ip_header(bess::Packet *pkt) {
	char *old_head = pkt->head_data<char *>();

	__m128i eth = _mm_loadu_si128(reinterpret_cast<__m128i *>(old_head));

	if (likely(pkt->adj(20) != NULL)) {
		eth = _mm_srli_si128(eth, 2);
		_mm_storeu_si128(reinterpret_cast<__m128i *>(old_head + 18), eth);
		// old_head + 18 is used because although the new head is at
		// (old_head + 20) there is 2 bytes of unwanted data in our reg.
		return 0;
	}
	return -1;
}

static inline int mark_packet_with_queue_number(bess::Packet *pkt, uint8_t q) {
  // pkt should be ip
  using bess::utils::Ethernet;
  using bess::utils::Ipv4;
  Ethernet *eth = reinterpret_cast<Ethernet *>(pkt->head_data());
  Ipv4 *ip = get_ip_header(eth);
  if (unlikely(ip == nullptr)) {
		// LOG(INFO) << "ip header not found\n";
    // return -1;  // failed
    // return _mark_packet_with_bd_vlan(eth, q);
    return _mark_with_ip_header(pkt, q);
  }
  uint8_t tos = ip->type_of_service;
  tos = tos & 0x03;  // only keep bottom to bits (ecn)
  tos = tos | (q << 2);
  ip->type_of_service = tos;
  return 0;  // success
}

}  // namespace bkdrft
}  // namespace bess

#endif
