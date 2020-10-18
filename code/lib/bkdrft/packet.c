#include "bkdrft.h"
#include "packet.h"
#include "bkdrft_msg.pb-c.h" /* bkdrft message protobuf */
#include <rte_byteorder.h>
#include <rte_ethdev.h>

/* This function is used for craeting Backdraft packets.
 * TODO: change this function to utilize the new create packet function.
 * */
void prepare_packet(struct rte_mbuf *buf, unsigned char *payload,
                    struct rte_mbuf *sample_pkt, size_t size) {
  char *buf_ptr;
  struct rte_ether_hdr *eth_hdr;
  struct rte_ipv4_hdr *ipv4_hdr;
  struct rte_udp_hdr *udp_hdr;

  if (!sample_pkt) {
    printf("sample pkt is null\n");
    return;
  }

  struct rte_ether_hdr *s_eth_hdr = get_ethernet_header(sample_pkt);
  if (!s_eth_hdr) {
    printf("s_eth_hdr is null\n");
    return;
  }

  struct rte_ipv4_hdr *s_ipv4_hdr = get_ipv4_header(s_eth_hdr);
  // if (!s_ipv4_hdr) {
  //   printf("s_ipv4_hdr is null\n");
  //   return;
  // }

  struct rte_udp_hdr *s_udp_hdr = NULL;
  if (s_ipv4_hdr != NULL)
     s_udp_hdr = get_udp_header(s_ipv4_hdr);
  // if (!s_udp_hdr) {
  //   printf("s_udp_hdr is null\n");
  //   return;
  // }

  /* ethernet header */
  buf_ptr = rte_pktmbuf_append(buf, RTE_ETHER_HDR_LEN);
  eth_hdr = (struct rte_ether_hdr *)buf_ptr;

  rte_ether_addr_copy(&s_eth_hdr->s_addr, &eth_hdr->s_addr);
  rte_ether_addr_copy(&s_eth_hdr->d_addr, &eth_hdr->d_addr);
  eth_hdr->ether_type = rte_cpu_to_be_16(
      RTE_ETHER_TYPE_IPV4); // rte_cpu_to_be_16(RTE_ETHER_TYPE_VLAN);

  /* IPv4 header */
  buf_ptr = rte_pktmbuf_append(buf, sizeof(struct rte_ipv4_hdr));
  ipv4_hdr = (struct rte_ipv4_hdr *)buf_ptr;
  ipv4_hdr->version_ihl = 0x45;
  ipv4_hdr->type_of_service = 0x00;
  ipv4_hdr->total_length = rte_cpu_to_be_16(sizeof(struct rte_ipv4_hdr) +
                                            sizeof(struct rte_udp_hdr) + size);
  ipv4_hdr->packet_id = 0;
  ipv4_hdr->fragment_offset = 0;
  ipv4_hdr->time_to_live = 64;
  ipv4_hdr->next_proto_id = BKDRFT_PROTO_TYPE; // IPPROTO_UDP;
  ipv4_hdr->hdr_checksum = 0;
  if (likely(s_ipv4_hdr != NULL)) {
    ipv4_hdr->src_addr = s_ipv4_hdr->src_addr;
    ipv4_hdr->dst_addr = s_ipv4_hdr->dst_addr;
  } else {
    // if ip is null ignore ip address fields
    ipv4_hdr->src_addr = rte_cpu_to_be_32(10 << 24 | 0 << 16 | 0 << 8 | 1);
    ipv4_hdr->dst_addr = rte_cpu_to_be_32(10 << 24 | 0 << 16 | 0 << 8 | 2);
  }

  /* UDP header + data */
  buf_ptr = rte_pktmbuf_append(buf, sizeof(struct rte_udp_hdr) + size);
  udp_hdr = (struct rte_udp_hdr *)buf_ptr;
  if (likely(s_udp_hdr != NULL)) {
    udp_hdr->src_port = s_udp_hdr->src_port;
    udp_hdr->dst_port = s_udp_hdr->dst_port;
  } else {
    // if udp is null ignore port fields
    udp_hdr->src_port = rte_cpu_to_be_16(6500);
    udp_hdr->dst_port = rte_cpu_to_be_16(6500);
  }
  udp_hdr->dgram_len = rte_cpu_to_be_16(sizeof(struct rte_udp_hdr) + size);
  udp_hdr->dgram_cksum = 0;
  memcpy(buf_ptr + sizeof(struct rte_udp_hdr), payload, size);

  buf->l2_len = RTE_ETHER_HDR_LEN;
  buf->l3_len = sizeof(struct rte_ipv4_hdr);
  // buf->ol_flags = PKT_TX_IP_CKSUM | PKT_TX_IPV4;
}

struct rte_ether_hdr *get_ethernet_header(struct rte_mbuf *pkt) {
  struct rte_ether_hdr *eth_hdr = NULL;
  eth_hdr = rte_pktmbuf_mtod_offset(pkt, struct rte_ether_hdr *, 0);
  return eth_hdr;
}

struct rte_vlan_hdr *get_vlan_header(struct rte_ether_hdr *eth_hdr) {
  struct rte_vlan_hdr *vlan_hdr = NULL;
  if (rte_be_to_cpu_16(eth_hdr->ether_type) == RTE_ETHER_TYPE_VLAN) {
    vlan_hdr = (struct rte_vlan_hdr *)(eth_hdr + 1);
  }

  return vlan_hdr;
}

struct rte_ipv4_hdr *get_ipv4_header(struct rte_ether_hdr *eth_hdr) {
  struct rte_ipv4_hdr *ipv4_hdr = NULL;
  uint16_t ether_type = rte_be_to_cpu_16(eth_hdr->ether_type);
  if (ether_type == RTE_ETHER_TYPE_IPV4) {
    ipv4_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);
  } else if (ether_type == RTE_ETHER_TYPE_VLAN) {
    struct rte_vlan_hdr *vlan = (struct rte_vlan_hdr *)(eth_hdr + 1);
    if (rte_be_to_cpu_16(vlan->eth_proto) == RTE_ETHER_TYPE_IPV4) {
      ipv4_hdr = (struct rte_ipv4_hdr *)(vlan + 1);
    }
  }

  return ipv4_hdr;
}

struct rte_udp_hdr *get_udp_header(struct rte_ipv4_hdr *ipv4_hdr) {
  struct rte_udp_hdr *udp_hdr = NULL;
  if (ipv4_hdr->next_proto_id == IPPROTO_UDP ||
      ipv4_hdr->next_proto_id == BKDRFT_PROTO_TYPE) {
    uint32_t ihl = ((ipv4_hdr->version_ihl) & 0x0f) * 4;
    udp_hdr = (struct rte_udp_hdr *)((uint8_t *)ipv4_hdr + ihl);
  }

  return udp_hdr;
}

struct rte_tcp_hdr *get_tcp_header(struct rte_ipv4_hdr *ipv4_hdr) {
  struct rte_tcp_hdr *tcp_hdr = NULL;

  if (ipv4_hdr->next_proto_id == IPPROTO_TCP) {
    uint32_t ihl = ((ipv4_hdr->version_ihl) & 0x0f) * 4;
    tcp_hdr = (struct rte_tcp_hdr *)((uint8_t *)ipv4_hdr + ihl);
  }

  return tcp_hdr;
}

size_t get_payload(struct rte_mbuf *pkt, void **payload) {
  struct rte_ether_hdr *eth = get_ethernet_header(pkt);
  *payload = NULL;
  if (!eth) {
    printf("eth is null\n");
    return 0;
  }
  struct rte_ipv4_hdr *ip = get_ipv4_header(eth);
  if (!ip) {
    printf("ip is null\n");
    return 0;
  }
  struct rte_udp_hdr *udp = get_udp_header(ip);
  if (!udp) {
    printf("udp is null\n");
    return 0;
  }
  *payload = (udp + 1);
  return (rte_be_to_cpu_16(udp->dgram_len) - sizeof(struct rte_udp_hdr));
}


extern inline size_t create_bkdraft_ctrl_msg(uint8_t qid, uint32_t nb_bytes,
                                             uint32_t nb_pkts,
                                             unsigned char **buf)
{
  size_t size = 0;
  // size_t packed_size = 0;
  unsigned char *payload;

  // memset(ctrl_msg, 0, sizeof(DpdkNetPerf__Ctrl));

  DpdkNetPerf__Ctrl ctrl_msg = DPDK_NET_PERF__CTRL__INIT;
  // dpdk_net_perf__ctrl__init(&ctrl_msg);
  ctrl_msg.qid = qid;
  ctrl_msg.total_bytes = nb_bytes;
  ctrl_msg.nb_pkts = nb_pkts;

  size = dpdk_net_perf__ctrl__get_packed_size(&ctrl_msg);
  size = size + 1; // one byte for adding message type tag

  // TODO: check if malloc has performance hit.
  payload = malloc(size);
  assert(payload);

  // first byte defines the bkdrft message type
  payload[0] = BKDRFT_CTRL_MSG_TYPE;
  dpdk_net_perf__ctrl__pack(&ctrl_msg, payload + 1);

  *buf = payload;
  return size;
}

