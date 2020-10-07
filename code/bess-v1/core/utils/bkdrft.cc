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

Ipv4 *get_ip_header(Ethernet *eth) {
  Ipv4 *ip = nullptr;
  uint16_t ether_type = eth->ether_type.value();
  if (ether_type == Ethernet::Type::kVlan) {
    Vlan *vlan = reinterpret_cast<Vlan *>(eth + 1);
    if (likely(vlan->ether_type.value() == Ethernet::Type::kIpv4)) {
      ip = reinterpret_cast<Ipv4 *>(vlan + 1);
    } else {
      // LOG(WARNING) << "get_ip_header: packet is not an Ip "
      //                 "packet\n";
      return nullptr;  // failed
    }
  } else if (ether_type == Ethernet::Type::kIpv4) {
    ip = reinterpret_cast<Ipv4 *>(eth + 1);
  } else {
    // LOG(WARNING) << "get_ip_header: packet is not an Ip "
    //                 "packet\n";
    return nullptr;  // failed
  }
  return ip;
}

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
  // uint8_t *ptr = reinterpret_cast<uint8_t *>(pkt->append(256));
  // assert(ptr != nullptr);
  // Ethernet *eth = reinterpret_cast<Ethernet *>(ptr);
  uint8_t *ptr; 
  Ethernet *eth = reinterpret_cast<Ethernet *>(pkt->head_data());
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
    assert(ptr != nullptr);
    bess::utils::Copy(ptr, payload, size, false);
  }

  pkt->set_total_len(256);
  pkt->set_data_len(208);
  return 256;  // successfuly prepared the pkt.
}

int prepare_ctrl_packet(bess::Packet *pkt, uint8_t qid, uint32_t nb_pkts,
                        uint64_t sent_bytes, const Flow *flow) {
  bess::pb::Ctrl ctrl_msg;
  ctrl_msg.set_qid(qid);
  ctrl_msg.set_nb_pkts(nb_pkts);
  ctrl_msg.set_total_bytes(sent_bytes);
  int size = ctrl_msg.ByteSizeLong();

  std::string payload(
      1, BKDRFT_CTRL_MSG_TYPE);  // first byte is reserved for type of message
  std::string serialized_msg;
  bool res = ctrl_msg.SerializeToString(&serialized_msg);
  if (!res) {
    LOG(WARNING) << "prepare_ctrl_packet: serializing protobuf failed\n";
    return -1;  // failed
  }
  payload.append(serialized_msg);
  size = size + 1; // add one byte for message type tag
  int pkt_size = prepare_packet(pkt, (void *)payload.c_str(), size, flow);
  if (pkt_size < 0) {
    // prepare packet failed
    LOG(WARNING) << "prepare_ctrl_packet: prepare packet failed!\n";
    return -2;
  }

  return 0;
}

int get_packet_payload(bess::Packet *pkt, void **payload,
                       bool only_bkdrft = false) {
  // if function fails the *payload should point to nullptr.
  *payload = nullptr;  // set the *payload to invalid position
  if (pkt == nullptr) {
    LOG(WARNING) << "get_packet_payload: pkt is null\n";
    return -2;  // failed
  }

  uint8_t *ptr = reinterpret_cast<uint8_t *>(pkt->head_data());
  Ethernet *eth = reinterpret_cast<Ethernet *>(ptr);
  Ipv4 *ip = get_ip_header(eth);
  if (ip == nullptr) {
    return -2;  // failed
  }

  if (only_bkdrft && ip->protocol != BKDRFT_PROTO_TYPE) {
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

int mark_packet_with_queue_number(bess::Packet *pkt, uint8_t q) {
  // pkt should be ip
  Ethernet *eth = reinterpret_cast<Ethernet *>(pkt->head_data());
  Ipv4 *ip = get_ip_header(eth);
  if (ip == nullptr) {
    return -1;  // failed
  }
  uint8_t tos = ip->type_of_service;
  tos = tos & 0x03;  // only keep bottom to bits (ecn)
  tos = tos | (q << 2);
  ip->type_of_service = tos;
  return 0;  // success
}

int parse_bkdrft_msg(bess::Packet *pkt, char *type, void **pb) {
  void *payload;
  size_t size = get_packet_payload(pkt, &payload, true);
  if (payload == nullptr) {
    // LOG(WARNING) << "parse_bkdrft_msg: could not find payload of the pkt.";
    return -1;  // failed
  }
  if (size >= BKDRFT_MAX_MESSAGE_SIZE) {
    LOG(WARNING) << "parse_bkdrft_msg: payload size is greater than expected.";
    return -1;  // failed
  }

  // copy payload to a buffer (from mbuf to char array)
  // char msg[BKDRFT_MAX_MESSAGE_SIZE];
  // bess::utils::Copy(msg, payload, size, true);
  // msg[size] = '\0';
  char *msg = reinterpret_cast<char *>(payload);

  char first_byte = msg[0];             // first byte is for finding the type
  // std::string serialized_msg(msg + 1);  // rest of the message is protobuf data
  *type = first_byte;

  // TODO: new key-word is used here. it may have performance hit.
  // maybe I should use a pool. If the effects are noticeable I have to do
  // more investigations
  if (first_byte == BKDRFT_CTRL_MSG_TYPE) {
    bess::pb::Ctrl *ctrl_msg = new bess::pb::Ctrl();
    // bool parse_res = ctrl_msg->ParseFromString(serialized_msg);
    bool parse_res = ctrl_msg->ParseFromArray(msg + 1, size - 1);
    if (!parse_res) {
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
      LOG(WARNING) << "parse_bkdrft_msg: Failed to parse ctrl message\n";
      return -1;  // failed
    }
    *pb = overlay_msg;
    return 0;  // sucessful
  }
  return -1;
}

}  // namespace bkdrft
}  // namespace bess
