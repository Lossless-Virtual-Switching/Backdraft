#ifndef _BKDRFT_H
#define _BKDRFT_H

#include <rte_mbuf.h>

/*
 * Send a packet, aware of control queue mechanism
 * */
int send_pkt(int port, uint8_t qid, struct rte_mbuf **tx_pkts, uint16_t nb_pkts,
             int send_ctrl_pkt, struct rte_mempool *tx_mbuf_pool);

int poll_ctrl_queue(const int port, const int ctrl_qid, const uint16_t burst,
                    struct rte_mbuf **recv_bufs, int blocking);

int poll_ctrl_queue_expose_qid(const int port, const int ctrl_qid,
                               const uint16_t burst,
                               struct rte_mbuf **recv_bufs, int blocking,
                               uint16_t *data_qid);

#endif
