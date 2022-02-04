#include <stdio.h>
#include <unistd.h>
#include "exp.h"
#include "arp.h"
#include "bkdrft.h"
#include "bkdrft_const.h"

#include <rte_ether.h>
#include <rte_arp.h>
#include <rte_ip.h>

int str_to_ip(const char *str, uint32_t *addr)
{
  uint8_t a, b, c, d;
  if(sscanf(str, "%hhu.%hhu.%hhu.%hhu", &a, &b, &c, &d) != 4) {
    return -1;
  }

  *addr = MAKE_IP_ADDR(a, b, c, d);
  return 0;
}

void ip_to_str(uint32_t addr, char *str, uint32_t size) {
  uint8_t *bytes = (uint8_t *)&addr; 
  snprintf(str, size, "%hhu.%hhu.%hhu.%hhu", bytes[3], bytes[2], bytes[1], bytes[0]);
}

/*
* Check if the packet is for this host
* Also send reply for ARP requests
*/
int check_eth_hdr(uint32_t my_ip, struct rte_ether_addr *host_mac,
    struct rte_mbuf *buf, struct rte_mempool *tx_mbuf_pool,
    uint8_t cdq, uint16_t port, uint16_t queue)
{
  uint16_t ether_type;
  struct rte_ether_hdr *ptr_mac_hdr;
  struct rte_arp_hdr *a_hdr;
  struct rte_ipv4_hdr *ipv4_hdr;
  int res;

  ptr_mac_hdr = rte_pktmbuf_mtod(buf, struct rte_ether_hdr *);
  if (!rte_is_same_ether_addr(&ptr_mac_hdr->d_addr, host_mac) &&
      !rte_is_broadcast_ether_addr(&ptr_mac_hdr->d_addr)) {
    /* packet not to our ethernet addr */
    // printf("not our ether, here\n");
    return 0;
  }

  ether_type = rte_be_to_cpu_16(ptr_mac_hdr->ether_type);
  if (ether_type == RTE_ETHER_TYPE_ARP) {
    /* reply to ARP if necessary */
    a_hdr = rte_pktmbuf_mtod_offset(buf, struct rte_arp_hdr *,
        sizeof(struct rte_ether_hdr));
    if (a_hdr->arp_opcode == rte_cpu_to_be_16(RTE_ARP_OP_REQUEST)
        && a_hdr->arp_data.arp_tip == rte_cpu_to_be_32(my_ip)) {
      send_arp(RTE_ARP_OP_REPLY, my_ip, a_hdr->arp_data.arp_sha,
          rte_be_to_cpu_32(a_hdr->arp_data.arp_sip), tx_mbuf_pool, cdq);
      printf("answering arp \n");
    }
    return 0;
  } else if (ether_type == RTE_ETHER_TYPE_IPV4) {
    ipv4_hdr = (struct rte_ipv4_hdr *)(ptr_mac_hdr + 1);
    if (ipv4_hdr->next_proto_id == BKDRFT_ARP_PROTO_TYPE) {
      // reply to arp request
      // TODO: should get ip header length and use that
      a_hdr = (struct rte_arp_hdr *)(ipv4_hdr + 1);
      if (a_hdr->arp_opcode == rte_cpu_to_be_16(RTE_ARP_OP_REQUEST)
          && a_hdr->arp_data.arp_tip == rte_cpu_to_be_32(my_ip)) {
          /* make sure arp is pushed to network */
          res = send_bkdrft_arp(port, queue, RTE_ARP_OP_REPLY, my_ip,
                                a_hdr->arp_data.arp_sha,
                                rte_be_to_cpu_32(a_hdr->arp_data.arp_sip),
                                tx_mbuf_pool, cdq);
          if (res == 0) {
            printf("answering arp (cdq: %d) \n", cdq);
          } else {
            printf("arp response failed (cdq: %d) \n", cdq);
          }
      }
      return 0;
    }
  }

  return 1;
}

/*
* Check if the packet is for this host
* Also send reply for ARP requests
*/
int check_eth_hdr_vport(uint32_t my_ip,
                        struct rte_ether_addr *host_mac, struct rte_mbuf *buf,
                        struct rte_mempool *tx_mbuf_pool, uint8_t cdq,
                        struct vport *port, uint16_t queue)
{
  uint16_t ether_type;
  struct rte_ether_hdr *ptr_mac_hdr;
  struct rte_arp_hdr *a_hdr;
  struct rte_ipv4_hdr *ipv4_hdr;
  int res;

  ptr_mac_hdr = rte_pktmbuf_mtod(buf, struct rte_ether_hdr *);
  if (!rte_is_same_ether_addr(&ptr_mac_hdr->d_addr, host_mac) &&
      !rte_is_broadcast_ether_addr(&ptr_mac_hdr->d_addr)) {
    /* packet not to our ethernet addr */
    // printf("not our ether, here\n");
    return 0;
  }

  ether_type = rte_be_to_cpu_16(ptr_mac_hdr->ether_type);
  if (ether_type == RTE_ETHER_TYPE_ARP) {
    /* reply to ARP if necessary */
    a_hdr = rte_pktmbuf_mtod_offset(buf, struct rte_arp_hdr *,
        sizeof(struct rte_ether_hdr));
    if (a_hdr->arp_opcode == rte_cpu_to_be_16(RTE_ARP_OP_REQUEST)
        && a_hdr->arp_data.arp_tip == rte_cpu_to_be_32(my_ip)) {
      send_arp_vport(port, RTE_ARP_OP_REPLY, my_ip, a_hdr->arp_data.arp_sha,
          rte_be_to_cpu_32(a_hdr->arp_data.arp_sip), tx_mbuf_pool, cdq);
      printf("answering arp \n");
    }
    return 0;
  } else if (ether_type == RTE_ETHER_TYPE_IPV4) {
    ipv4_hdr = (struct rte_ipv4_hdr *)(ptr_mac_hdr + 1);
    if (ipv4_hdr->next_proto_id == BKDRFT_ARP_PROTO_TYPE) {
      // reply to arp request
      // TODO: should get ip header length and use that
      a_hdr = (struct rte_arp_hdr *)(ipv4_hdr + 1);
      if (a_hdr->arp_opcode == rte_cpu_to_be_16(RTE_ARP_OP_REQUEST)
          && a_hdr->arp_data.arp_tip == rte_cpu_to_be_32(my_ip)) {
          /* make sure arp is pushed to network */
          res = send_bkdrft_arp_vport(port, queue, RTE_ARP_OP_REPLY, my_ip,
                                a_hdr->arp_data.arp_sha,
                                rte_be_to_cpu_32(a_hdr->arp_data.arp_sip),
                                tx_mbuf_pool, cdq);
          if (res == 0) {
            printf("answering arp (cdq: %d) \n", cdq);
          } else {
            printf("arp response failed (cdq: %d) \n", cdq);
          }
      }
      return 0;
    }
  }

  return 1;
}

void wait(uint64_t ns) {
  uint64_t start , now, d;
  start = rte_get_timer_cycles();
  now = start;
  d = (now - start) * (1000000000.0 / rte_get_timer_hz());
  while (d < ns) {
    now = rte_get_timer_cycles();
    d = (now - start) * (1000000000.0 / rte_get_timer_hz());
  }
}
