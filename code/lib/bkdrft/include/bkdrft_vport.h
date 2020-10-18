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
#endif
