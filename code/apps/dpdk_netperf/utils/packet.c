#include "include/bkdrft.h"
#include "include/packet.h"
#include <rte_byteorder.h>
#include <rte_ethdev.h>

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
  if (!s_ipv4_hdr) {
    printf("s_ipv4_hdr is null\n");
    return;
  }
  struct rte_udp_hdr *s_udp_hdr = get_udp_header(s_ipv4_hdr);
  if (!s_udp_hdr) {
    printf("s_udp_hdr is null\n");
    return;
  }

  /* ethernet header */
  buf_ptr = rte_pktmbuf_append(buf, RTE_ETHER_HDR_LEN);
  eth_hdr = (struct rte_ether_hdr *)buf_ptr;

  rte_ether_addr_copy(&s_eth_hdr->s_addr, &eth_hdr->s_addr);
  rte_ether_addr_copy(&s_eth_hdr->d_addr, &eth_hdr->d_addr);
  eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4); // rte_cpu_to_be_16(RTE_ETHER_TYPE_VLAN);

  /* IPv4 header */
  buf_ptr = rte_pktmbuf_append(buf, sizeof(struct rte_ipv4_hdr));
  ipv4_hdr = (struct rte_ipv4_hdr *)buf_ptr;
  ipv4_hdr->version_ihl = 0x45;
  ipv4_hdr->type_of_service = 0x00;
  ipv4_hdr->total_length = rte_cpu_to_be_16(sizeof(struct rte_ipv4_hdr) +
                                            sizeof(struct rte_udp_hdr) + size);
  ipv4_hdr->packet_id =  0;
  ipv4_hdr->fragment_offset = 0;
  ipv4_hdr->time_to_live = 64;
  ipv4_hdr->next_proto_id = BKDRFT_PROTO_TYPE;  // IPPROTO_UDP;
  ipv4_hdr->hdr_checksum = 0;
  ipv4_hdr->src_addr = s_ipv4_hdr->src_addr;
  ipv4_hdr->dst_addr = s_ipv4_hdr->dst_addr;

  /* UDP header + data */
  buf_ptr = rte_pktmbuf_append(buf, sizeof(struct rte_udp_hdr) + size);
  udp_hdr = (struct rte_udp_hdr *)buf_ptr;
  udp_hdr->src_port = s_udp_hdr->src_port;
  udp_hdr->dst_port = s_udp_hdr->dst_port;
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
    tcp_hdr = (struct rte_tcp_hdr *)((uint8_t  *)ipv4_hdr + ihl);
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
