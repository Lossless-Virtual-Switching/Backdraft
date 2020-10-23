#ifndef _BKDRFT_VPORT_H
#define _BKDRFT_VPORT_H
#include <rte_mbuf.h>
#include <stdint.h>
#include "vport.h"

int vport_send_pkt(struct vport *port, uint16_t qid,
                            struct rte_mbuf**pkts,
                            uint16_t nb_pkts, int send_ctrl_pkt,
                            uint16_t doorbell,
                            struct rte_mempool *ctrl_pool);

int vport_poll_ctrl_queue(struct vport *port,
                          uint16_t ctrl_qid, uint16_t burst,
                          struct rte_mbuf **recv_bufs, int blocking);

int vport_poll_ctrl_queue_expose_qid(struct vport *port, uint16_t ctrl_qid,
                               uint16_t burst, struct rte_mbuf **recv_bufs,
                               int blocking, uint16_t *data_qid);
#endif
