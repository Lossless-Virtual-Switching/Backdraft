#include "flow_rules.h"

struct rte_flow *generate_ipv4_flow(uint16_t port_id, uint16_t rx_q,
                                    uint32_t src_ip, uint32_t src_mask,
                                    uint32_t dest_ip, uint32_t dest_mask,
                                    struct rte_flow_error *error) {
  const int count_patterns = 4;
  const int count_actions = 2;
  struct rte_flow_attr attr;
  struct rte_flow_item pattern[count_patterns];
  struct rte_flow_action action[count_actions];
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
  vlan_spec.tci = RTE_BE16(0x6064);
  // vlan_spec.tci = RTE_BE16(0x8064);
  vlan_spec.inner_type = RTE_BE16(RTE_ETHER_TYPE_IPV4);
  vlan_mask.tci = 0x0fff, vlan_mask.inner_type = 0x0,

  pattern[1].type = RTE_FLOW_ITEM_TYPE_VLAN;
  pattern[1].spec = &vlan_spec;
  pattern[1].mask = &vlan_mask;

  memset(&ip_spec, 0, sizeof(struct rte_flow_item_ipv4));
  memset(&ip_mask, 0, sizeof(struct rte_flow_item_ipv4));
  ip_spec.hdr.dst_addr = htonl(dest_ip);
  ip_mask.hdr.dst_addr = htonl(dest_mask);
  ip_spec.hdr.src_addr = htonl(src_ip);
  ip_mask.hdr.src_addr = htonl(src_mask);

  pattern[2].type = RTE_FLOW_ITEM_TYPE_IPV4;
  pattern[2].spec = &ip_spec;
  pattern[2].mask = &ip_mask;

  pattern[3].type = RTE_FLOW_ITEM_TYPE_END;

  res = rte_flow_validate(port_id, &attr, pattern, action, error);
  if (!res)
    flow = rte_flow_create(port_id, &attr, pattern, action, error);
  return flow;
}

struct rte_flow *generate_eth_flow(uint16_t port_id, uint16_t rx_q,
                                   struct rte_ether_addr src,
                                   struct rte_ether_addr src_mask,
                                   struct rte_ether_addr dst,
                                   struct rte_ether_addr dst_mask,
                                   struct rte_flow_error *error) {
  const int count_patterns = 2;
  const int count_actions = 2;
  struct rte_flow_attr attr;
  struct rte_flow_item pattern[count_patterns];
  struct rte_flow_action action[count_actions];
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
  eth_spec.src = src;
  eth_spec.dst = dst;
  eth_mask.src = src_mask;
  eth_mask.dst = dst_mask;
  pattern[0].type = RTE_FLOW_ITEM_TYPE_ETH;
  pattern[0].spec = &eth_spec;
  pattern[0].mask = &eth_mask;

  pattern[1].type = RTE_FLOW_ITEM_TYPE_END;

  res = rte_flow_validate(port_id, &attr, pattern, action, error);
  if (!res)
    flow = rte_flow_create(port_id, &attr, pattern, action, error);
  return flow;
}
