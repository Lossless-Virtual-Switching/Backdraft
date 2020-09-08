#ifndef _BKDRFT_FLOW_RULE_H
#define _BKDRFT_FLOW_RULE_H
#include <rte_flow.h>
#include <rte_byteorder.h>

namespace bkdrft {
// only filter by ip proto
struct rte_flow *filter_by_ip_proto(uint16_t port_id, uint8_t proto,
                                    uint16_t rx_q,
                                    struct rte_flow_error *error);

struct rte_flow *filter_by_ip_dst(uint16_t port_id, uint32_t dst_addr,
                                  uint32_t dst_mask, uint16_t rx_q,
                                  struct rte_flow_error *error);

// struct rte_flow *filter_by_ip_hdr_checksum(uint16_t port_id, uint16_t
// checksum,
//                                            uint16_t rx_q,
//                                            struct rte_flow_error *error);

struct rte_flow *filter_by_ipv4_bkdrft_opt(uint16_t port_id, uint8_t opt,
                                           uint16_t rx_q,
                                           struct rte_flow_error *error);

struct rte_flow *filter_by_ip_tos(uint16_t port_id, uint8_t tos,
                                  uint8_t tos_mask, uint16_t rx_q,
                                  struct rte_flow_error *error);

struct rte_flow *filter_by_ether_type(uint16_t port_id, rte_be16_t ether_type,
                                    uint16_t rx_q,
                                    struct rte_flow_error *error);

struct rte_flow *filter_by_ip_tos_with_vlan(uint16_t port_id, uint8_t tos,
                                  uint8_t tos_mask, uint8_t prio,
                                  uint16_t vlan_id, uint16_t vlan_id_mask, 
                                  uint16_t rx_q,
                                  struct rte_flow_error *error);

struct rte_flow *filter_by_vlan_type(uint16_t port_id, uint16_t prio,
                                      uint16_t vlan_id, uint16_t vlan_id_mask,
                                      uint16_t vlan_inner_type, uint16_t rx_q,
                                      struct rte_flow_error *error);

}  // namespace bkdrft
#endif  // _BKDRFT_FLOW_RULE_H
