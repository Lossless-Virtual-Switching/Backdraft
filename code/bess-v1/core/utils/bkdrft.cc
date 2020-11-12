#include "bkdrft.h"
#include "../pb/bkdrft_msg.pb.h"
#include "endian.h"
#include "ether.h"
#include "flow.h"
#include "ip.h"
#include "tcp.h"
#include "udp.h"

namespace bess {
namespace bkdrft {
using bess::bkdrft::Flow;
using bess::utils::be16_t;
using bess::utils::Ethernet;
using bess::utils::Ipv4;
using bess::utils::Tcp;
using bess::utils::Udp;
using bess::utils::Vlan;

Udp *get_udp_header(Ipv4 *ip) {
  if (ip->protocol == Ipv4::Proto::kUdp) {
    const uint32_t ihl = ip->header_length * 4;
    Udp *udp = reinterpret_cast<Udp *>(reinterpret_cast<uint8_t *>(ip) + ihl);
    return udp;
  }
  return nullptr;
}

int prepare_packet(bess::Packet *pkt, void *payload, size_t size,
                   const Flow *flow) {
  // prepare packet
  pkt->set_data_off(SNBUF_HEADROOM);

  // uint16_t total_packet_len =
  //     sizeof(Ethernet) + sizeof(Ipv4) + sizeof(Udp) + size;

  // Ethernet
  // pkt->append(128)
  uint8_t *ptr = reinterpret_cast<uint8_t *>(pkt->head_data());
  // uint8_t *ptr = reinterpret_cast<uint8_t *>(pkt->append(256));
  // assert(ptr != nullptr);
  Ethernet *eth = reinterpret_cast<Ethernet *>(ptr);
  eth->dst_addr = flow->eth_dst_addr;  // s_eth->dst_addr;
  eth->src_addr = flow->eth_src_addr;  // s_eth->src_addr;
  eth->ether_type =
      be16_t(Ethernet::Type::kIpv4);  // be16_t(Ethernet::Type::kVlan);
  // Ip
  Ipv4 *ip = reinterpret_cast<Ipv4 *>(eth + 1);
  ip->src = flow->addr_src;  // s_ip->src;
  ip->dst = flow->addr_dst;  // s_ip->dst;
  // ip->protocol = Ipv4::Proto::kUdp;
  ip->protocol = BKDRFT_PROTO_TYPE;  // mark this packet as bkdrft overlay/ctrl
  ip->header_length = 5;
  ip->checksum = 0;
  // Udp
  Udp *udp = reinterpret_cast<Udp *>(ip + 1);
  udp->src_port = flow->port_src;  // s_udp->src_port;
  udp->dst_port = flow->port_dst;  // s_udp->dst_port;
  udp->length = be16_t(sizeof(Udp) + size);
  udp->checksum = 0;
  // Payload
  if (payload != nullptr) {
    ptr = reinterpret_cast<uint8_t *>(udp + 1);
    bess::utils::Copy(ptr, payload, size, false);
    LOG(INFO) << "Copying\n";
  }

  pkt->set_total_len(256);
  pkt->set_data_len(208);
  return 256;  // successfuly prepared the pkt.
}

int prepare_ctrl_packet(bess::Packet *pkt, uint8_t qid, uint32_t prio,
                        uint32_t nb_pkts, uint64_t sent_bytes,
                        const Flow *flow)
{
  bess::pb::Ctrl ctrl_msg;
  ctrl_msg.set_qid(qid);
  ctrl_msg.set_prio(prio);
  ctrl_msg.set_nb_pkts(nb_pkts);
  ctrl_msg.set_total_bytes(sent_bytes);
  int size = ctrl_msg.ByteSizeLong();

  // std::string payload(
  //     1, BKDRFT_CTRL_MSG_TYPE);  // first byte is reserved for type of message
  // std::string serialized_msg;
  // bool res = ctrl_msg.SerializeToString(&serialized_msg);
  size += 2; // one byte for type and one byte for \0
  // char payload[size];
  pkt->set_data_off(SNBUF_HEADROOM);
  char *payload = reinterpret_cast<char *>(pkt->head_data())
                  + sizeof(Ethernet) + sizeof(Ipv4) + sizeof(Udp);
  payload[0] = BKDRFT_CTRL_MSG_TYPE;
  bool res = ctrl_msg.SerializeToArray(payload + 1, size - 2);
  payload[size] = '\0';
  if (!res) {
    // LOG(WARNING) << "prepare_ctrl_packet: serializing protobuf failed\n";
    return -1;  // failed
  }
  // payload.append(serialized_msg);
  // int pkt_size = prepare_packet(pkt, (void *)payload.c_str(), size, flow);
  int pkt_size = prepare_packet(pkt, nullptr, size, flow);
  if (pkt_size < 0) {
    // prepare packet failed
    // LOG(WARNING) << "prepare_ctrl_packet: prepare packet failed!\n";
    return -2;
  }

  return 0;
}

int get_packet_payload(bess::Packet *pkt, void **payload,
                       bool only_bkdrft = false) {
  // if function fails the *payload should point to nullptr.
  *payload = nullptr;  // set the *payload to invalid position
  if (unlikely(pkt == nullptr)) {
    LOG(WARNING) << "get_packet_payload: pkt is null\n";
    return -2;  // failed
  }

  uint8_t *ptr = reinterpret_cast<uint8_t *>(pkt->head_data());
  Ethernet *eth = reinterpret_cast<Ethernet *>(ptr);
  Ipv4 *ip = get_ip_header(eth);
  if (unlikely(ip == nullptr)) {
    // LOG(INFO) << "No ip header\n";
    return -2;  // failed
  }

  if (only_bkdrft && ip->protocol != BKDRFT_PROTO_TYPE) {
    // LOG(INFO) << "get_packet_payload: ip-proto: " << ip->protocol <<"\n";
    // LOG(INFO) << "get_packet_payload: ip-tos: " << ip->type_of_service <<"\n";
    return -1;  // failed - not bkdrft packet
  }

  void *data = nullptr;
  uint32_t size = 0;
  if (ip->protocol == Ipv4::Proto::kUdp || ip->protocol == BKDRFT_PROTO_TYPE) {
    const uint32_t ihl = ip->header_length * 4;
    Udp *udp = reinterpret_cast<Udp *>(reinterpret_cast<uint8_t *>(ip) + ihl);
    data = reinterpret_cast<void *>(udp + 1);
    const uint32_t udp_len = udp->length.value();
    if (udp_len < sizeof(Udp)) {
      LOG(WARNING) << "Udp length is less than Udp header size!\n";
      return -3;  // failed
    }
    size = udp_len - sizeof(Udp);
  } else if (ip->protocol == Ipv4::Proto::kTcp) {
    // Tcp *tcp = reinterpret_cast<Tcp *>(ip + 1);
    // data =
    LOG(WARNING) << "get_packet_payload: not implemented for tcp packets!\n";
    return -4;  // not implemented yet
  } else {
    return -4;  // failed
  }

  *payload = data;
  return size;
}

// static inline int _mark_packet_with_bd_vlan(bess::Packet *pkt, uint8_t q) {
// 	char *new_head;
//
// 	if ((new_head = static_cast<char *>(pkt->prepend(4))) != nullptr) {
//     uint32_t tag = BKDRFT_VLAN_HEADER << 16 | (uint32_t)qid;
//
// 		// shift 12 bytes to the left by 4 bytes
// 		__m128i ethh;
//
//     // load 16 byte to register for processing
// 		ethh = _mm_loadu_si128(reinterpret_cast<__m128i *>(new_head + 4));
//
//     // get the current ether_type value (from reg)
// 		be16_t tpid(be16_t::swap(_mm_extract_epi16(ethh, 6)));
//
//     // insert vlan header which is 4 bytes or 32bit at 4 element (in reg)
// 		ethh = _mm_insert_epi32(ethh, tag, 3);
//
//     // sotre new ether net header. new_head is 4 byte before the original
//     // header position (update memory)
// 		_mm_storeu_si128(reinterpret_cast<__m128i *>(new_head), ethh);
//     return 0;
//   }
//   LOG(INFO) << "Failed to add bkdrft vlan like header\n";
//   return -1; //failed
// }

int parse_bkdrft_msg(bess::Packet *pkt, char *type, void **pb) {
  void *payload;
  size_t size = get_packet_payload(pkt, &payload, true);
  if (unlikely(payload == nullptr)) {
    // LOG(WARNING) << "parse_bkdrft_msg: could not find payload of the pkt.";
    return -1;  // failed
  }
  if (unlikely(size >= BKDRFT_MAX_MESSAGE_SIZE)) {
    LOG(WARNING) << "parse_bkdrft_msg: payload size is greater than expected.";
    return -1;  // failed
  }

  // copy payload to a buffer (from mbuf to char array)
  // char msg[BKDRFT_MAX_MESSAGE_SIZE];
  // bess::utils::Copy(msg, payload, size, true);
  char *msg;
  msg = reinterpret_cast<char *>(payload);
  // msg[size] = '\0';

  char first_byte = msg[0];             // first byte is for finding the type
  // std::string serialized_msg(msg + 1);  // rest of the message is protobuf data
  *type = first_byte;

  // TODO: new key-word is used here. it may have performance hit.
  // maybe I should use a pool. If the effects are noticeable I have to do
  // more investigations
  if (first_byte == BKDRFT_CTRL_MSG_TYPE) {
    // TODO: use a pool of these objects
    // TODO: (or if no concurrent call just static object is enough)
    // or just ask caller to allocate memory
    bess::pb::Ctrl *ctrl_msg;
    if (unlikely(*pb == nullptr))
      ctrl_msg = new bess::pb::Ctrl();
    else
      ctrl_msg = reinterpret_cast<bess::pb::Ctrl *>(*pb);
    bool parse_res = ctrl_msg->ParseFromArray(msg + 1, size - 1);
    if (unlikely(!parse_res)) {
      LOG(WARNING) << "parse_bkdrft_msg: Failed to parse ctrl message\n";
      return -1;  // failed
    }
    *pb = ctrl_msg;
    return 0;
  } else if (first_byte == BKDRFT_OVERLAY_MSG_TYPE) {
    bess::pb::Overlay *overlay_msg = new bess::pb::Overlay();
    // bool parse_res = overlay_msg->ParseFromString(serialized_msg);
    bool parse_res = overlay_msg->ParseFromArray(msg + 1, size - 1);
    if (!parse_res) {
      LOG(WARNING) << "parse_bkdrft_msg: Failed to parse overlay message\n";
      return -1;  // failed
    }
    *pb = overlay_msg;
    return 0;  // sucessful
  }
  return -1;
}

}  // namespace bkdrft
}  // namespace bess
