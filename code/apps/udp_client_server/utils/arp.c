#include "../include/arp.h"

#include <unistd.h>
#include <stdbool.h>

#include <rte_ip.h>
#include <rte_arp.h>
#include <rte_ether.h>
#include <rte_ethdev.h>

#include "../include/bkdrft.h"


void send_arp(uint16_t op, uint32_t src_ip,
    struct rte_ether_addr dst_eth, uint32_t dst_ip,
    struct rte_mempool *tx_mbuf_pool, uint8_t cdq)
{
  struct rte_ether_addr src_mac;
  struct rte_mbuf *buf;
  char *buf_ptr;
  struct rte_ether_hdr *eth_hdr;
  struct rte_arp_hdr *a_hdr;
  int nb_tx;
  uint8_t qid = cdq ? 1 : 0;

  rte_eth_macaddr_get(0, &src_mac);

  buf = rte_pktmbuf_alloc(tx_mbuf_pool);
  if (buf == NULL)
    printf("error allocating arp mbuf\n");

  // ethernet header
  buf_ptr = rte_pktmbuf_append(buf, RTE_ETHER_HDR_LEN);
  eth_hdr = (struct rte_ether_hdr *) buf_ptr;

  rte_ether_addr_copy(&src_mac, &eth_hdr->s_addr);
  rte_ether_addr_copy(&dst_eth, &eth_hdr->d_addr);
  eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_ARP);

  // arp header
  buf_ptr = rte_pktmbuf_append(buf, sizeof(struct rte_arp_hdr));
  a_hdr = (struct rte_arp_hdr *) buf_ptr;
  // a_hdr->arp_hrd = rte_cpu_to_be_16(RTE_ARP_HRD_ETHER);
  a_hdr->arp_protocol = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
  a_hdr->arp_hlen = RTE_ETHER_ADDR_LEN;
  a_hdr->arp_plen = 4;
  a_hdr->arp_opcode = rte_cpu_to_be_16(op);

  rte_ether_addr_copy(&src_mac, &a_hdr->arp_data.arp_sha);
  a_hdr->arp_data.arp_sip = rte_cpu_to_be_32(src_ip);
  rte_ether_addr_copy(&dst_eth, &a_hdr->arp_data.arp_tha);
  a_hdr->arp_data.arp_tip = rte_cpu_to_be_32(dst_ip);

  // nb_tx = rte_eth_tx_burst(0, 0, &buf, 1);
  nb_tx = send_pkt(0, qid, &buf, 1, cdq, tx_mbuf_pool);
  if (unlikely(nb_tx != 1)) {
    printf("error: could not send arp packet\n");
  }
}

void send_bkdrft_arp(uint16_t port, uint16_t queue, uint16_t op, uint32_t src_ip,
    struct rte_ether_addr dst_eth, uint32_t dst_ip,
    struct rte_mempool *tx_mbuf_pool, uint8_t cdq)
{
  struct rte_ether_addr src_mac;
  struct rte_mbuf *buf;
  char *buf_ptr;
  struct rte_ether_hdr *eth_hdr;
  struct rte_ipv4_hdr *ipv4_hdr;
  struct rte_arp_hdr *a_hdr;
  int nb_tx;

  rte_eth_macaddr_get(0, &src_mac);

  buf = rte_pktmbuf_alloc(tx_mbuf_pool);
  if (buf == NULL)
    printf("error allocating arp mbuf\n");

  // ethernet header
  buf_ptr = rte_pktmbuf_append(buf, RTE_ETHER_HDR_LEN);
  eth_hdr = (struct rte_ether_hdr *) buf_ptr;

  rte_ether_addr_copy(&src_mac, &eth_hdr->s_addr);
  rte_ether_addr_copy(&dst_eth, &eth_hdr->d_addr);
  eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

  // ipv4 header
  buf_ptr = rte_pktmbuf_append(buf, sizeof(struct rte_ipv4_hdr));
  ipv4_hdr = (struct rte_ipv4_hdr *)buf_ptr;
  ipv4_hdr->version_ihl = 0x45;
  ipv4_hdr->type_of_service = 0;
  ipv4_hdr->total_length =
  rte_cpu_to_be_16(sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_arp_hdr));
  ipv4_hdr->packet_id = 0;
  ipv4_hdr->fragment_offset = 0;
  ipv4_hdr->time_to_live = 64;
  // ipv4_hdr->next_proto_id = IPPROTO_UDP;
  ipv4_hdr->next_proto_id = BKDRFT_ARP_PROTO_TYPE;
  ipv4_hdr->hdr_checksum = 0;
  ipv4_hdr->src_addr = rte_cpu_to_be_32(src_ip);
  ipv4_hdr->dst_addr = rte_cpu_to_be_32(dst_ip);

  // udp header
  // buf_ptr = rte_pktmbuf_append(buf, sizeof(struct rte_udp_hdr) +
  //                                  sizeof(struct rte_arp_hdr));
  // udp_hdr = (struct rte_udp_hdr *)buf_ptr;
  // udp_hdr->src_port = rte_cpu_to_be_16(src_port);
  // udp_hdr->dst_port = rte_cpu_to_be_16(dst_port);
  // udp_hdr->dgram_len =
  //     rte_cpu_to_be_16(sizeof(struct rte_udp_hdr) + payload_length);
  // udp_hdr->dgram_cksum = 0;

  // arp header
  buf_ptr = rte_pktmbuf_append(buf, sizeof(struct rte_arp_hdr));
  a_hdr = (struct rte_arp_hdr *) buf_ptr;
  // a_hdr->arp_hrd = rte_cpu_to_be_16(RTE_ARP_HRD_ETHER);
  a_hdr->arp_protocol = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
  a_hdr->arp_hlen = RTE_ETHER_ADDR_LEN;
  a_hdr->arp_plen = 4;
  a_hdr->arp_opcode = rte_cpu_to_be_16(op);

  rte_ether_addr_copy(&src_mac, &a_hdr->arp_data.arp_sha);
  a_hdr->arp_data.arp_sip = rte_cpu_to_be_32(src_ip);
  rte_ether_addr_copy(&dst_eth, &a_hdr->arp_data.arp_tha);
  a_hdr->arp_data.arp_tip = rte_cpu_to_be_32(dst_ip);

  // send pkt
  // nb_tx = rte_eth_tx_burst(0, 0, &buf, 1);
  nb_tx = send_pkt(port, queue, &buf, 1, cdq, tx_mbuf_pool);
  if (unlikely(nb_tx != 1)) {
    printf("error: could not send arp packet\n");
  }
}


/*
 * port: which port to send on
 * src_ip
 * src_mac
 * dst_ip
 * broadcast_mac
 * tx_mbuf_pool
 * returns destination ether address
 * */
struct rte_ether_addr get_dst_mac(uint16_t port, uint16_t queue,
    uint32_t src_ip, struct rte_ether_addr src_mac,
    uint32_t dst_ip, struct rte_ether_addr broadcast_mac,
    struct rte_mempool *tx_mbuf_pool, uint8_t cdq, uint16_t count_queue) 
{
  const uint32_t burst_size = 32;
  uint32_t nb_rx;
  struct rte_ether_addr dst_mac;
  struct rte_mbuf *bufs[burst_size];
  struct rte_mbuf *buf;
  struct rte_ether_hdr *ptr_mac_hdr;
  struct rte_arp_hdr *a_hdr;
  uint16_t ether_type;
  uint8_t found_res = 0;
  uint16_t rx_q;

  while (!found_res) {
    send_bkdrft_arp(port, queue, RTE_ARP_OP_REQUEST, src_ip, broadcast_mac,
               dst_ip, tx_mbuf_pool, cdq);
    sleep(1);

    for (rx_q = 0; !found_res && rx_q < count_queue; rx_q++) {
      if (cdq) {
        nb_rx = poll_ctrl_queue(port, BKDRFT_CTRL_QUEUE, burst_size, bufs, false); 
      } else {
        nb_rx = rte_eth_rx_burst(port, rx_q, bufs, burst_size);
      }
   
      if (nb_rx == 0)
        continue;

      for (uint32_t i = 0; i < nb_rx; i++) {
        buf = bufs[i];

        if (found_res)
          rte_pktmbuf_free(buf);

        ptr_mac_hdr = rte_pktmbuf_mtod(buf, struct rte_ether_hdr *);
        if (!rte_is_same_ether_addr(&ptr_mac_hdr->d_addr, &src_mac)) {
          /* packet not to our ethernet addr */
          rte_pktmbuf_free(buf);
          continue;
        }

        ether_type = rte_be_to_cpu_16(ptr_mac_hdr->ether_type);
        if (ether_type == RTE_ETHER_TYPE_ARP) {
          /* this is an ARP */
          a_hdr = rte_pktmbuf_mtod_offset(buf, struct rte_arp_hdr *,
              sizeof(struct rte_ether_hdr));
          if (a_hdr->arp_opcode == rte_cpu_to_be_16(RTE_ARP_OP_REPLY) &&
              rte_is_same_ether_addr(&a_hdr->arp_data.arp_tha, &src_mac) &&
              a_hdr->arp_data.arp_tip == rte_cpu_to_be_32(src_ip)) {
            /* got a response */
            rte_ether_addr_copy(&a_hdr->arp_data.arp_sha, &dst_mac);
            found_res = 1;
          }
        } else if (ether_type == RTE_ETHER_TYPE_IPV4) {
          struct rte_ipv4_hdr *ipv4_hdr;
          ipv4_hdr = (struct rte_ipv4_hdr *)(ptr_mac_hdr + 1);
          if (ipv4_hdr->next_proto_id == BKDRFT_ARP_PROTO_TYPE) {
            // TODO: should get ip header length and use that
            a_hdr = (struct rte_arp_hdr *)(ipv4_hdr + 1);
            if (a_hdr->arp_opcode == rte_cpu_to_be_16(RTE_ARP_OP_REPLY) &&
                rte_is_same_ether_addr(&a_hdr->arp_data.arp_tha, &src_mac) &&
                a_hdr->arp_data.arp_tip == rte_cpu_to_be_32(src_ip)) {
              /* got a response */
              rte_ether_addr_copy(&a_hdr->arp_data.arp_sha, &dst_mac);
              found_res = 1;
            }
          }
        }

        // free the packet
        rte_pktmbuf_free(buf);
      }

      // all read packets are freed if the result is found then return it
      if (found_res) break;
    }
  }
  return dst_mac;
}
