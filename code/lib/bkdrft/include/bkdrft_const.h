#ifndef _BKDRFT_CONST_H
#define _BKDRFT_CONST_H

struct cdq_payload {
  char type;
  uint16_t qid;
  uint16_t prio;
  uint32_t nb_pkts;
}__attribute__((__packed__));

struct bd_message {
  char type;
};

// TODO: ctrl queue is not constant due to static partitioning
#define BKDRFT_CTRL_QUEUE (0)
#define BKDRFT_MAX_MESSAGE_SIZE (128)
// TODO: overlay vlan id is not needed ?!
#define BKDRFT_OVERLAY_VLAN_ID (100)
// TODO: overlay prio ??!
#define BKDRFT_OVERLAY_PRIO (0)
#define BKDRFT_OVERLAY_VLAN_PRIO 3

#define BKDRFT_PROTO_TYPE (253)
#define BKDRFT_ARP_PROTO_TYPE (252)

// BKDRFT Message Types
#define BKDRFT_CTRL_MSG_TYPE ('1')
#define BKDRFT_OVERLAY_MSG_TYPE ('2')

#endif
