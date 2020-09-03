#ifndef _ARP_H
#define _ARP_H
#include <stdint.h>
#include <rte_ether.h>
#include <rte_mbuf.h>

void send_arp(uint16_t op, uint32_t src_ip, struct rte_ether_addr dst_eth,
    uint32_t dst_ip, struct rte_mempool *tx_mbuf_pool, uint8_t cdq);

void send_bkdrft_arp(uint16_t port, uint16_t queue, uint16_t op, uint32_t src_ip,
    struct rte_ether_addr dst_eth, uint32_t dst_ip,
    struct rte_mempool *tx_mbuf_pool, uint8_t cdq);

struct rte_ether_addr get_dst_mac(uint16_t port, uint16_t queue,
    uint32_t src_ip, struct rte_ether_addr src_mac,
    uint32_t dst_ip, struct rte_ether_addr broadcast_mac,
    struct rte_mempool *tx_mbuf_pool, uint8_t cdq, uint16_t count_queue);
#endif
