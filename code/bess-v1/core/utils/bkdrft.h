#ifndef _BKDRFT_H
#define _BKDRFT_H

#include "../packet.h"
#include "flow.h"
#include <stdint.h>

#define BKDRFT_CTRL_QUEUE (0)
#define BKDRFT_OVERLAY_PRIO (0)
#define BKDRFT_OVERLAY_VLAN_ID (100)
#define BKDRFT_MAX_MESSAGE_SIZE (128)
#define BKDRFT_PROTO_TYPE (253)

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

int mark_packet_with_queue_number(bess::Packet *pkt, uint8_t q);

int parse_bkdrft_msg(bess::Packet *pkt, char *type, void **pb);

}  // namespace bkdrft
}  // namespace bess

#endif
