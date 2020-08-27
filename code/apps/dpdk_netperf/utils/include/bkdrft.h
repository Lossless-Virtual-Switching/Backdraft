#ifndef _BKDRFT_H
#define _BKDRFT_H

#include <rte_mbuf.h>
#include <stdbool.h>

#define BKDRFT_CTRL_QUEUE (0)
#define BKDRFT_MAX_MESSAGE_SIZE (128)
#define BKDRFT_OVERLAY_VLAN_ID (100)
#define BKDRFT_OVERLAY_PRIO (0)
#define BKDRFT_PROTO_TYPE (253)

#define BKDRFT_CTRL_MSG_TYPE ('1')
#define BKDRFT_OVERLAY_MSG_TYPE ('2')

struct __attribute__((__packed__)) bkdrft_ipv4_opt {
  uint8_t cpy_optclass_optnum;  // cpy: 1 bit , opt_class: 2 bit, opt_num: 5 bit
  uint8_t opt_length;
  uint8_t queue_number;  // currently only this field is used to determine the
                         // queues
  uint8_t reserved;
};

#define init_bkdrft_ipv4_opt ((struct bkdrft_ipv4_opt){0x03, 4, 0, 0})

/*
 * Send a packet, aware of control queue mechanism
 * */
int send_pkt(int port, uint8_t qid, struct rte_mbuf **tx_pkts, uint16_t nb_pkts,
             bool send_ctrl_pkt, struct rte_mempool *tx_mbuf_pool);

size_t create_bkdraft_ctrl_msg(uint8_t qid, uint32_t nb_bytes, uint32_t nb_pkts,
                               unsigned char **buf);

int poll_ctrl_queue(const int port, const int ctrl_qid, const uint16_t burst,
                    struct rte_mbuf **recv_bufs, bool blocking);

int poll_ctrl_queue_expose_qid(const int port, const int ctrl_qid,
                               const uint16_t burst,
                               struct rte_mbuf **recv_bufs, bool blocking,
                               uint16_t *data_qid);

#endif
