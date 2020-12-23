#ifndef _PACKET_H
#define _PACKET_H

#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
#include <rte_udp.h>

int prepare_packet(struct rte_mbuf *buf, unsigned char *payload,
                    struct rte_mbuf *sample_pkt, size_t size);
struct rte_ether_hdr *get_ethernet_header(struct rte_mbuf *pkt);
struct rte_vlan_hdr *get_vlan_header(struct rte_ether_hdr *eth_hdr);
struct rte_ipv4_hdr *get_ipv4_header(struct rte_ether_hdr *eth_hdr);
struct rte_udp_hdr *get_udp_header(struct rte_ipv4_hdr *ipv4_hdr);
struct rte_tcp_hdr *get_tcp_header(struct rte_ipv4_hdr *ipv4_hdr);
size_t get_payload(struct rte_mbuf *pkt, void **payload);

size_t create_bkdraft_ctrl_msg(uint16_t qid, uint32_t nb_bytes,
                               uint32_t nb_pkts, unsigned char **buf);

// end packet.h
#endif
