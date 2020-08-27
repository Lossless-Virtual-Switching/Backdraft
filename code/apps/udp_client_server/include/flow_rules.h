#ifndef _FLOW_RULE_H
#define _FLOW_RULE_H

#include <rte_flow.h>

#define full_mask (0xffffffff)
#define no_mask (0x0)

#define eth_addr_full_mask ((struct rte_ether_addr){{0xff,0xff,0xff,0xff,0xff,0xff}})
#define eth_addr_no_mask ((struct rte_ether_addr){{0x00,0x00,0x00,0x00,0x00,0x00}})

struct rte_flow *generate_ipv4_flow(uint16_t port_id, uint16_t rx_q,
                                    uint32_t src_ip, uint32_t src_mask,
                                    uint32_t dest_ip, uint32_t dest_mask,
                                    struct rte_flow_error *error); 

struct rte_flow *generate_eth_flow(uint16_t port_id, uint16_t rx_q,
                                    struct rte_ether_addr src, struct rte_ether_addr src_mask,
                                    struct rte_ether_addr dst, struct rte_ether_addr dst_mask,
                                    struct rte_flow_error *error);

#endif // _FLOW_RULE_H
