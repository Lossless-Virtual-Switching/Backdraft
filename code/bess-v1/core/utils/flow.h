#ifndef _BKDRFT_FLOW_
#define _BKDRFT_FLOW_

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
	
	struct Hash {
		std::size_t operator()(const Flow &f) const
		{
			return rte_hash_crc(&f, sizeof(Flow), 0);
		}
	};

	struct EqualTo {
		bool operator()(const Flow &lf, const Flow &rf) const
		{
			const union {
				Flow f;
				struct {
					uint64_t p1;
					uint64_t p2;
				};
			} &left = {.f =lf}, &right = {.f = rf};
			if (left.p1 == right.p1)
				return left.p2 == right.p2;
			return false;
		}
	};
};

static_assert(sizeof(Flow) == 16, "Flow structure size is not correct");


static inline Flow PacketToFlow(const bess::Packet &pkt)
{
	using bess::utils::Ethernet;
	using bess::utils::Ipv4;
	using IpProto = bess::utils::Ipv4::Proto;
	using bess::utils::Udp;
	using bess::utils::Tcp;
	using bess::utils::Icmp;

	Ethernet *eth = pkt.head_data<Ethernet *>();
	// eth + 1 skips the ethernet header
	// assuming the next header is ip
	Ipv4 *ip =reinterpret_cast<Ipv4 *>(eth + 1);
	// Ip header length tells the number of 32 bit
	// rows (4 byte rows) so multiply by four to
	// get number of bytes.
	size_t ip_hdr_bytes = (ip->header_length) << 2;
	// Layer 4 header (TCP, UDP, ICMP, ...)
	void *l4 = reinterpret_cast<uint8_t *>(ip) + ip_hdr_bytes;

	// Find out ip protocol
	IpProto proto = static_cast<IpProto>(ip->protocol);
	be16_t port_src, port_dst;
	if (likely(proto == IpProto::kTcp || proto == IpProto::kUdp)) {
			// UDP and TCP share the same layout for port numbers
			const Udp *udp = static_cast<const Udp *>(l4);
			port_src = udp->src_port;
			port_dst = udp->dst_port;
	} else {
		// only works for udp and tcp
		return {};
	}
	
	Flow f = {
		.addr_src = ip->src,
		.addr_dst = ip->dst,
		.port_src = port_src,
		.port_dst = port_dst,
		.protocol = proto,
	};
	return f;
}

}
}
#endif // _BKDRFT_FLOW_
