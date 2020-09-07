#include "bkdrft_flow_rules.h"

namespace bkdrft {

struct rte_flow *filter_by_ip_proto(uint16_t port_id, uint8_t proto,
                                    uint16_t rx_q,
                                    struct rte_flow_error *error) {
  const int pattern_count = 3;
  const int action_count = 2;
  struct rte_flow_attr attr;
  struct rte_flow_item pattern[pattern_count];
  struct rte_flow_action action[action_count];
  struct rte_flow *flow = NULL;
  struct rte_flow_action_queue queue = {.index = rx_q};
  struct rte_flow_item_ipv4 ip_spec;
  struct rte_flow_item_ipv4 ip_mask;
  int res;
  memset(pattern, 0, sizeof(pattern));
  memset(action, 0, sizeof(action));

  memset(&attr, 0, sizeof(struct rte_flow_attr));
  attr.ingress = 1;

  action[0].type = RTE_FLOW_ACTION_TYPE_QUEUE;
  action[0].conf = &queue;
  action[1].type = RTE_FLOW_ACTION_TYPE_END;

  pattern[0].type = RTE_FLOW_ITEM_TYPE_ETH;
  memset(&ip_spec, 0, sizeof(struct rte_flow_item_ipv4));
  memset(&ip_mask, 0, sizeof(struct rte_flow_item_ipv4));
  ip_spec.hdr.next_proto_id = proto;
  ip_mask.hdr.next_proto_id = 0xff;   // should be exact match
  ip_mask.hdr.src_addr = 0x00000000;  // src ip is not important
  ip_mask.hdr.dst_addr = 0x00000000;  // dst ip is not important
  pattern[1].type = RTE_FLOW_ITEM_TYPE_IPV4;
  pattern[1].spec = &ip_spec;
  pattern[1].mask = &ip_mask;
  pattern[2].type = RTE_FLOW_ITEM_TYPE_END;

  res = rte_flow_validate(port_id, &attr, pattern, action, error);
  if (!res)
    flow = rte_flow_create(port_id, &attr, pattern, action, error);
  return flow;
}

struct rte_flow *filter_by_ip_dst(uint16_t port_id, uint32_t dst_addr,
                                  uint32_t dst_mask, uint16_t rx_q,
                                  struct rte_flow_error *error) {
  const int pattern_count = 3;
  const int action_count = 2;
  struct rte_flow_attr attr;
  struct rte_flow_item pattern[pattern_count];
  struct rte_flow_action action[action_count];
  struct rte_flow *flow = NULL;
  struct rte_flow_action_queue queue = {.index = rx_q};
  struct rte_flow_item_ipv4 ip_spec;
  struct rte_flow_item_ipv4 ip_mask;
  int res;
  memset(pattern, 0, sizeof(pattern));
  memset(action, 0, sizeof(action));

  memset(&attr, 0, sizeof(struct rte_flow_attr));
  attr.ingress = 1;

  action[0].type = RTE_FLOW_ACTION_TYPE_QUEUE;
  action[0].conf = &queue;
  action[1].type = RTE_FLOW_ACTION_TYPE_END;

  pattern[0].type = RTE_FLOW_ITEM_TYPE_ETH;
  memset(&ip_spec, 0, sizeof(struct rte_flow_item_ipv4));
  memset(&ip_mask, 0, sizeof(struct rte_flow_item_ipv4));
  ip_spec.hdr.dst_addr = htonl(dst_addr);
  ip_mask.hdr.dst_addr = htonl(dst_mask);
  ip_mask.hdr.src_addr = 0x00000000;  // any source ip
  pattern[1].type = RTE_FLOW_ITEM_TYPE_IPV4;
  pattern[1].spec = &ip_spec;
  pattern[1].mask = &ip_mask;
  pattern[2].type = RTE_FLOW_ITEM_TYPE_END;

  res = rte_flow_validate(port_id, &attr, pattern, action, error);
  if (!res)
    flow = rte_flow_create(port_id, &attr, pattern, action, error);
  return flow;
}

// struct rte_flow *filter_by_ip_hdr_checksum(uint16_t port_id, uint16_t
// checksum,
//                                            uint16_t rx_q,
//                                            struct rte_flow_error *error)
// {
//   const int pattern_count  = 3;
//   const int action_count = 2;
//   struct rte_flow_attr attr;
//   struct rte_flow_item pattern[pattern_count];
//   struct rte_flow_action action[action_count];
//   struct rte_flow *flow = NULL;
//   struct rte_flow_action_queue queue = {.index = rx_q};
//   struct rte_flow_item_ipv4 ip_spec;
//   struct rte_flow_item_ipv4 ip_mask;
//   int res;
//   memset(pattern, 0, sizeof(pattern));
//   memset(action, 0, sizeof(action));
//
//   memset(&attr, 0, sizeof(struct rte_flow_attr));
//   attr.ingress = 1;
//
//   action[0].type = RTE_FLOW_ACTION_TYPE_QUEUE;
//   action[0].conf = &queue;
//   action[1].type = RTE_FLOW_ACTION_TYPE_END;
//
//   pattern[0].type = RTE_FLOW_ITEM_TYPE_ETH;
//   memset(&ip_spec, 0, sizeof(struct rte_flow_item_ipv4));
//   memset(&ip_mask, 0, sizeof(struct rte_flow_item_ipv4));
//   ip_spec.hdr.hdr_checksum = RTE_BE16(checksum);
//   ip_mask.hdr.dst_addr = 0x00000000; // any destination ip
//   ip_mask.hdr.src_addr = 0x00000000; // any source ip
//   ip_mask.hdr.hdr_checksum = 0xffff; // exact match for hdr_checksum
//   pattern[1].type = RTE_FLOW_ITEM_TYPE_IPV4;
//   pattern[1].spec = &ip_spec;
//   pattern[1].mask = &ip_mask;
//   pattern[2].type = RTE_FLOW_ITEM_TYPE_END;
//
//   res = rte_flow_validate(port_id, &attr, pattern, action, error);
//   if (!res)
//     flow = rte_flow_create(port_id, &attr, pattern, action, error);
//   else
//     printf( "data mapping rule is not valid\n");
//   return flow;
// }

struct rte_flow *filter_by_ipv4_bkdrft_opt(uint16_t port_id,
                                           uint8_t opt_pattern, uint16_t rx_q,
                                           struct rte_flow_error *error) {
  const int pattern_count = 4;
  const int action_count = 2;
  struct rte_flow_attr attr;
  struct rte_flow_item pattern[pattern_count];
  struct rte_flow_action action[action_count];
  struct rte_flow *flow = NULL;
  struct rte_flow_item_raw raw_spec;
  struct rte_flow_action_queue queue = {.index = rx_q};
  int res;
  memset(pattern, 0, sizeof(pattern));
  memset(action, 0, sizeof(action));

  memset(&attr, 0, sizeof(struct rte_flow_attr));
  attr.ingress = 1;

  action[0].type = RTE_FLOW_ACTION_TYPE_QUEUE;
  action[0].conf = &queue;
  action[1].type = RTE_FLOW_ACTION_TYPE_END;

  pattern[0].type = RTE_FLOW_ITEM_TYPE_ETH;
  pattern[1].type = RTE_FLOW_ITEM_TYPE_IPV4;

  memset(&raw_spec, 0, sizeof(struct rte_flow_item_raw));
  raw_spec.relative = 1;
  raw_spec.offset = 16;
  raw_spec.length = 8;
  raw_spec.pattern = &opt_pattern;
  pattern[2].type = RTE_FLOW_ITEM_TYPE_RAW;
  pattern[2].spec = &raw_spec;
  pattern[3].type = RTE_FLOW_ITEM_TYPE_END;

  res = rte_flow_validate(port_id, &attr, pattern, action, error);
  if (!res)
    flow = rte_flow_create(port_id, &attr, pattern, action, error);
  return flow;
}

struct rte_flow *filter_by_ip_tos(uint16_t port_id, uint8_t tos,
                                  uint8_t tos_mask, uint16_t rx_q,
                                  struct rte_flow_error *error) {
  const int pattern_count = 3;
  const int action_count = 2;
  struct rte_flow_attr attr;
  struct rte_flow_item pattern[pattern_count];
  struct rte_flow_action action[action_count];
  struct rte_flow *flow = NULL;
  struct rte_flow_action_queue queue = {.index = rx_q};
  struct rte_flow_item_ipv4 ip_spec;
  struct rte_flow_item_ipv4 ip_mask;
  int res;
  memset(pattern, 0, sizeof(pattern));
  memset(action, 0, sizeof(action));

  memset(&attr, 0, sizeof(struct rte_flow_attr));
  attr.ingress = 1;

  action[0].type = RTE_FLOW_ACTION_TYPE_QUEUE;
  action[0].conf = &queue;
  action[1].type = RTE_FLOW_ACTION_TYPE_END;

  pattern[0].type = RTE_FLOW_ITEM_TYPE_ETH;
  memset(&ip_spec, 0, sizeof(struct rte_flow_item_ipv4));
  memset(&ip_mask, 0, sizeof(struct rte_flow_item_ipv4));
  ip_spec.hdr.type_of_service = tos;
  ip_mask.hdr.type_of_service = tos_mask;
  ip_mask.hdr.src_addr = 0x00000000;  // src ip is not important
  ip_mask.hdr.dst_addr = 0x00000000;  // dst ip is not important
  pattern[1].type = RTE_FLOW_ITEM_TYPE_IPV4;
  pattern[1].spec = &ip_spec;
  pattern[1].mask = &ip_mask;
  pattern[2].type = RTE_FLOW_ITEM_TYPE_END;

  res = rte_flow_validate(port_id, &attr, pattern, action, error);
  if (!res)
    flow = rte_flow_create(port_id, &attr, pattern, action, error);
  return flow;
}

struct rte_flow *filter_by_ip_tos_with_vlan(uint16_t port_id, uint8_t tos,
                                  uint8_t tos_mask, uint8_t prio,
                                  uint16_t vlan_id, uint16_t vlan_id_mask, 
                                  uint16_t rx_q,
                                  struct rte_flow_error *error) {
  const int pattern_count = 4;
  const int action_count = 2;
  struct rte_flow_attr attr;
  struct rte_flow_item pattern[pattern_count];
  struct rte_flow_action action[action_count];
  struct rte_flow *flow = NULL;
  struct rte_flow_action_queue queue = {.index = rx_q};
  struct rte_flow_item_vlan vlan_spec;
  struct rte_flow_item_vlan vlan_mask;
  struct rte_flow_item_ipv4 ip_spec;
  struct rte_flow_item_ipv4 ip_mask;
  int res;
  memset(pattern, 0, sizeof(pattern));
  memset(action, 0, sizeof(action));

  memset(&attr, 0, sizeof(struct rte_flow_attr));
  attr.ingress = 1;

  action[0].type = RTE_FLOW_ACTION_TYPE_QUEUE;
  action[0].conf = &queue;
  action[1].type = RTE_FLOW_ACTION_TYPE_END;

  pattern[0].type = RTE_FLOW_ITEM_TYPE_ETH;
  memset(&vlan_spec, 0, sizeof(struct rte_flow_item_vlan));
  memset(&vlan_mask, 0, sizeof(struct rte_flow_item_vlan));
  vlan_spec.inner_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
  vlan_spec.tci = rte_cpu_to_be_16(prio << 13 | 0 << 12 | (vlan_id & 0x0fff));
  vlan_mask.tci = rte_cpu_to_be_16(vlan_id_mask);
  vlan_mask.inner_type = 0xffff;
  pattern[1].type = RTE_FLOW_ITEM_TYPE_VLAN; 
  pattern[1].spec = &vlan_spec;
  pattern[1].mask = &vlan_mask;
  memset(&ip_spec, 0, sizeof(struct rte_flow_item_ipv4));
  memset(&ip_mask, 0, sizeof(struct rte_flow_item_ipv4));
  ip_spec.hdr.type_of_service = tos;
  ip_mask.hdr.type_of_service = tos_mask;
  ip_mask.hdr.src_addr = 0x00000000;  // src ip is not important
  ip_mask.hdr.dst_addr = 0x00000000;  // dst ip is not important
  pattern[2].type = RTE_FLOW_ITEM_TYPE_IPV4;
  pattern[2].spec = &ip_spec;
  pattern[2].mask = &ip_mask;
  pattern[3].type = RTE_FLOW_ITEM_TYPE_END;

  res = rte_flow_validate(port_id, &attr, pattern, action, error);
  if (!res)
    flow = rte_flow_create(port_id, &attr, pattern, action, error);
  return flow;
}

struct rte_flow *filter_by_ether_type(uint16_t port_id, rte_be16_t ether_type,
                                    uint16_t rx_q,
                                    struct rte_flow_error *error) {
  const int pattern_count = 3;
  const int action_count = 2;
  struct rte_flow_attr attr;
  struct rte_flow_item pattern[pattern_count];
  struct rte_flow_action action[action_count];
  struct rte_flow *flow = NULL;
  struct rte_flow_action_queue queue = {.index = rx_q};
  struct rte_flow_item_eth eth_spec;
  struct rte_flow_item_eth eth_mask;
  int res;
  memset(pattern, 0, sizeof(pattern));
  memset(action, 0, sizeof(action));

  memset(&attr, 0, sizeof(struct rte_flow_attr));
  attr.ingress = 1;

  action[0].type = RTE_FLOW_ACTION_TYPE_QUEUE;
  action[0].conf = &queue;
  action[1].type = RTE_FLOW_ACTION_TYPE_END;

  memset(&eth_spec, 0, sizeof(struct rte_flow_item_eth));
  memset(&eth_mask, 0, sizeof(struct rte_flow_item_eth));
  eth_spec.type = rte_cpu_to_be_16(ether_type);
  eth_mask.type = rte_cpu_to_be_16(0xffff);
  pattern[0].type = RTE_FLOW_ITEM_TYPE_ETH;
  pattern[0].spec = &eth_spec;
  pattern[0].mask = &eth_mask;
  pattern[1].type = RTE_FLOW_ITEM_TYPE_END;

  res = rte_flow_validate(port_id, &attr, pattern, action, error);
  if (!res)
    flow = rte_flow_create(port_id, &attr, pattern, action, error);
  return flow;
}

struct rte_flow *filter_by_vlan_type(uint16_t port_id, uint16_t prio,
                                      uint16_t vlan_id, uint16_t vlan_id_mask,
                                      uint16_t vlan_inner_type, uint16_t rx_q,
                                      struct rte_flow_error *error) {
  const int pattern_count = 4;
  const int action_count = 2;
  struct rte_flow_attr attr;
  struct rte_flow_item pattern[pattern_count];
  struct rte_flow_action action[action_count];
  struct rte_flow *flow = NULL;
  struct rte_flow_action_queue queue = {.index = rx_q};
  struct rte_flow_item_vlan vlan_spec;
  struct rte_flow_item_vlan vlan_mask;
  int res;
  memset(pattern, 0, sizeof(pattern));
  memset(action, 0, sizeof(action));

  memset(&attr, 0, sizeof(struct rte_flow_attr));
  attr.ingress = 1;

  action[0].type = RTE_FLOW_ACTION_TYPE_QUEUE;
  action[0].conf = &queue;
  action[1].type = RTE_FLOW_ACTION_TYPE_END;

  pattern[0].type = RTE_FLOW_ITEM_TYPE_ETH;

  memset(&vlan_spec, 0, sizeof(struct rte_flow_item_vlan));
  memset(&vlan_mask, 0, sizeof(struct rte_flow_item_vlan));
  vlan_spec.inner_type = rte_cpu_to_be_16(vlan_inner_type);
  vlan_spec.tci = rte_cpu_to_be_16(prio << 13 | 0 << 12 | (vlan_id & 0x0fff));
  vlan_mask.inner_type = rte_cpu_to_be_16(0xffff);
  vlan_mask.tci = rte_cpu_to_be_16(vlan_id_mask);
  pattern[1].type = RTE_FLOW_ITEM_TYPE_VLAN; 
  pattern[1].spec = &vlan_spec;
  pattern[1].mask = &vlan_mask;

  pattern[2].type = RTE_FLOW_ITEM_TYPE_END;

  res = rte_flow_validate(port_id, &attr, pattern, action, error);
  if (!res)
    flow = rte_flow_create(port_id, &attr, pattern, action, error);
  return flow;
}

}  // namespace bkdrft

