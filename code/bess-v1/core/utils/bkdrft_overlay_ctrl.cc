#include "bkdrft_overlay_ctrl.h"
#include "checksum.h"
#include "ether.h"
#include "ip.h"
#include "udp.h"
#include "endian.h"
#include "bkdrft.h"

#include <rte_ether.h>

#include <string>
// #include "time.h"

#define max_overlay_payload_size (128) // in bytes

namespace bess {
namespace bkdrft {

using utils::Ethernet;
using utils::Vlan;
using bess::utils::Ipv4;
using utils::Udp;
using bess::utils::be16_t;
using bess::utils::be32_t;

/*
 * pps: packet per second
 * */
void BKDRFTOverlayCtrl::FillOverlayPacket(const Flow &f, Packet *pkt, uint64_t pps,
		uint64_t pause_duration) {

  std::string payload(1, BKDRFT_OVERLAY_MSG_TYPE);
  std::string serialized_msg;
  uint32_t size; // serialize data size
  std::string src_mac;
  std::string dst_mac;

  src_mac = f.eth_src_addr.ToString();
  dst_mac = f.eth_dst_addr.ToString();

  // Protobuf message (overlay)
  bess::pb::Overlay overlay_msg;
  overlay_msg.set_src_port(f.port_src.value());
  overlay_msg.set_dst_port(f.port_dst.value());
  overlay_msg.set_src_ip(f.addr_src.value());
  overlay_msg.set_dst_ip(f.addr_dst.value());
  overlay_msg.set_src_mac(src_mac);
  overlay_msg.set_dst_mac(dst_mac);
  overlay_msg.set_protocol(f.protocol);
  overlay_msg.set_wnd_size(12);
  overlay_msg.set_packet_per_sec(pps);
  overlay_msg.set_pause_duration(pause_duration);

  // Preparing our serialized data
  size = overlay_msg.ByteSizeLong();
  overlay_msg.SerializeToString(&serialized_msg);
  payload.append(serialized_msg); 
  size = size + 1; // add one byte for message type tag

  Flow flow = SwapFlow(f);
  prepare_packet(pkt, (void *)payload.c_str(), size, &flow);
}

void BKDRFTOverlayCtrl::ReadOverlayPacket(Packet *pkt) {
  // TODO:  validity of the packet is checked both here and in the caller
  // performance optimiztion may be applied.
  // This function reads overlay messages and deserialize the protobuf content
  bess::pb::Overlay overlay_msg;

  uint8_t *ptr = reinterpret_cast<uint8_t *>(pkt->head_data());
  Ethernet *eth = reinterpret_cast<Ethernet *>(ptr);
  // if (eth->ether_type.value() != Ethernet::Type::kVlan) {
  if (eth->ether_type.value() != Ethernet::Type::kIpv4) {
    LOG(WARNING) << "Not an overlay message (not ip)\n";
    return;
  }
  //Vlan *vlan = reinterpret_cast<Vlan *>(eth + 1);
  //if ((vlan->tci.value() & 0x0fff) != 100) {
  //  LOG(WARNING) << "Not an overlay message (not from vlan_id 100)\n";
  //  return;
  //}

  // Ipv4 *ip = reinterpret_cast<Ipv4 *>(vlan + 1);
  Ipv4 *ip = reinterpret_cast<Ipv4 *>(eth + 1);
  // if (ip->protocol == Ipv4::Proto::kUdp) {
  if (ip->protocol == BKDRFT_PROTO_TYPE) {
    Udp *udp = reinterpret_cast<Udp *>(ip + 1);
    uint8_t *data_pointer = reinterpret_cast<uint8_t *>(udp + 1);
    uint32_t size = udp->length.value() - sizeof(Udp); // payload size
    if (size > max_overlay_payload_size) {
      LOG(WARNING) << "Overlay message payload is larger than expected\n";
      return;
    }
    char body[max_overlay_payload_size];
    bess::utils::Copy(body, data_pointer, size, true);
    body[size] = '\0';  // null terminate the message
    std::string data(body);
    bool res = overlay_msg.ParseFromString(data);
    if (res) {
      uint32_t wnd = overlay_msg.wnd_size();
      LOG(INFO) << "window size: " << wnd << "\n";
      // TODO: return or propagate the message in other it affects the system
    } else {
      LOG(WARNING) << "Failed to parse overlay protobuf message\n";
    }
  } else {
    LOG(WARNING) <<  "ReadOverlayPacket is called on a non-udp packet!\n";
  }
}

/*
 * updates the overlay global flow table.
 * returns the flow's pause timestamp. caller may check to see if this flow is
 * paused or not.
 * */
uint64_t BKDRFTOverlayCtrl::FillBook(Port *port, queue_t qid,  const Flow &flow) {
  auto entry = flowBook_.Find(flow);
  if (entry == nullptr) {
    // LOG(INFO) << "new flow !\n";
    // LOG(INFO) << FlowToString(flow) << "\n";
    flowBook_.Insert(
        flow, {.port = port, qid = qid, .block = false, .pause_until_ts = 0,
          .window = DEFAULT_WINDOW_SIZE});
    return 0;
  }
  else {
    entry->second.port = port; // I can also update time to leave here.
    entry->second.qid = qid;
    return entry->second.pause_until_ts;
  }
}

int BKDRFTOverlayCtrl::SendOverlayMessage(const Flow &flow, Packet *pkt,
  uint64_t pps, uint64_t pause_duration) {

  auto entry = flowBook_.Find(flow);
  if (entry != nullptr) {
    Port *port = entry->second.port;
    if (port->getConnectedPortType() == VHOST)
      return -2;

    // LOG(INFO) << "Sending overlay: name: " << 0 << " pps: " << pps
    //   << " pause duration: " << pause_duration << "\n";
    // LOG(INFO) << "original flow: " << FlowToString(flow) << "\n";
    // LOG(INFO) << "overlay flow: " << FlowToString(PacketToFlow(*pkt)) << "\n";

    FillOverlayPacket(flow, pkt, pps, pause_duration);
    int sent = entry->second.port->SendPackets(BKDRFT_CTRL_QUEUE, &pkt, 1);
    if(sent == 0) {
      bess::Packet::Free(pkt);
      LOG(INFO) << "FREED bkdrft overlay packet, failed to send!\n";
      return -1; // failed
    }
    return 0; // success
  } else {
    LOG(INFO) << "Send Overlay Failed: flow not found (not mapped)\n";
    LOG(INFO) << "Flow: " << FlowToString(flow) << "\n";
  }
  return -1; // failed
}

void BKDRFTOverlayCtrl::ApplyOverlayMessage(bess::pb::Overlay &overlay_msg,
                uint64_t current_ns) {
  uint64_t pps;
  uint64_t pause_duration;
  queue_t qid;
  Flow flow;

  BKDRFTOverlayCtrl::ExtractFlow(overlay_msg, flow); 

  auto entry = flowBook_.Find(flow);
  if (entry == nullptr) {
    LOG(INFO) << "RateLimitFlow: Flow not found!("<< FlowToString(flow) << ")\n";
    return;
  }

  qid = entry->second.qid;
  
  // limit pps
  pps = overlay_msg.packet_per_sec();
  entry->second.port->limiter_.limit[PACKET_DIR_OUT][qid] = pps;
  entry->second.port->limiter_.limit[PACKET_DIR_INC][qid] = pps;

  // update pause timestamp
  pause_duration = overlay_msg.pause_duration();
  entry->second.pause_until_ts = pause_duration + current_ns;

  // LOG(INFO) << "port: " << entry->second.port->name()
  //         << " limit qid: " << (int)qid << " flow " << FlowToString(flow)
  //         << " pps " << pps
  //         << " pause: " << pause_duration << "\n";
}

int BKDRFTOverlayCtrl::ExtractFlow(bess::pb::Overlay overlay_msg, Flow &flow) {
  bool parse_result;

  // set src mac address from string
  parse_result = flow.eth_src_addr.FromString(overlay_msg.src_mac());
  if (!parse_result)
    return -1; // failed

  // set dst mac address from string
  parse_result =  flow.eth_dst_addr.FromString(overlay_msg.dst_mac());
  if (!parse_result)
    return -1; // failed

  flow.addr_src = be32_t(overlay_msg.src_ip());
  flow.addr_dst = be32_t(overlay_msg.dst_ip());
  flow.port_src = be16_t(overlay_msg.src_port());
  flow.port_dst = be16_t(overlay_msg.dst_port());
  flow.protocol = overlay_msg.protocol();
  return 0; // success
}

const OverlayEntry *BKDRFTOverlayCtrl::getOverlayEntry(const Flow &flow) {
  auto entry = flowBook_.Find(flow);
  if (entry == nullptr) {
    LOG(INFO) << "getOverlayEntry: Flow not found!("<< FlowToString(flow) << ")\n";
    return nullptr;
  }
  return &entry->second;
}


} // namespace bkdrft
} // namespace bess
