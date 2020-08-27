#include "demultiplex.h"

#include "../utils/common.h"
#include "../utils/ether.h"
#include "../utils/format.h"
#include "../utils/ip.h"
#include "../utils/tcp.h"
#include "../utils/udp.h"
#include "../utils/endian.h"

using bess::utils::Ethernet;
using bess::utils::Ipv4;
using IpProto = bess::utils::Ipv4::Proto;
using bess::utils::Udp;
using bess::utils::Tcp;
using bess::utils::be16_t;

CommandResponse Demultiplex::Init(const bess::pb::DemultiplexArg &args)
{
  count_range_ = args.ranges_size();
  if (count_range_ > Demultiplex::max_count_range) {
    return CommandFailure(EINVAL, "no more than %d ranges", Demultiplex::max_count_range);
  }

  gates_[0] = default_gate;
  count_gates_ = 1;
  bool found = false;
  for (uint32_t i = 0; i < count_range_; i++) {
    const auto &range = args.ranges(i);
    const gate_idx_t gate = range.gate();
    ranges_[i] = {
      .start = (uint16_t)range.start(),
      .end   = (uint16_t)range.end(),
      .gate  = (uint16_t)gate, 
    };

    // check if this gate has been added before
    found = false;
    for (uint32_t k = 0; k < count_gates_; k++) {
      if (gates_[k] == gate) {
        found = true;
        break;
      }
    }
    if (!found) {
      gates_[count_gates_] = gate;
      count_gates_++;
    }
  }
  return CommandSuccess();
}

void Demultiplex::Broadcast(Context *ctx, bess::Packet *pkt)
{
  for (uint32_t i = 1; i < count_gates_; i++) {
    bess::Packet *cpy = bess::Packet::copy(pkt);
    if (cpy) {
      EmitPacket(ctx, cpy, gates_[i]);
    }
  }
  EmitPacket(ctx, pkt, gates_[0]);
}

void Demultiplex::ProcessBatch(Context *ctx, bess::PacketBatch *batch)
{
  for (int i = 0; i < batch->cnt(); i++) {
    bess::Packet *pkt = batch->pkts()[i];
    Ethernet *eth = pkt->head_data<Ethernet *>();
    Ipv4 *ip = reinterpret_cast<Ipv4 *>(eth + 1);


    // Check protocol in IP header
    IpProto proto = static_cast<IpProto>(ip->protocol);
    if (unlikely(proto != IpProto::kTcp && proto != IpProto::kUdp)) {
      Broadcast(ctx, pkt);
      return; 
    }

    size_t ip_hdr_len = (ip->header_length) << 2;
    void *l4 = reinterpret_cast<uint8_t *>(ip) + ip_hdr_len;

    // UDP and TCP share the same layout for port numbers
    const Udp *udp = static_cast<const Udp *>(l4);

    const be16_t nport = udp->dst_port;
    const uint16_t port = nport.value();
    
    // find gate
    gate_idx_t dst_gate = default_gate;
    for (uint32_t i = 0; i < count_range_; i++) {
      if (port >= ranges_[i].start && port < ranges_[i].end) {
        dst_gate = ranges_[i].gate;
        break;
      }
    }
    EmitPacket(ctx, pkt, dst_gate);
    
  }
}

ADD_MODULE(Demultiplex, "demultiplex",
  "splits packets base on destination port. broadcasts everything that is not TCP or UDP")
