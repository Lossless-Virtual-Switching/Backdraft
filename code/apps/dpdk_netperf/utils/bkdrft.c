#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ethdev.h>

#include "bkdrft.h"

extern inline int send_pkt(int port, uint8_t qid,
	struct rte_mbuf **tx_pkts, uint16_t nb_pkts, bool send_ctrl_pkt,
	struct rte_mempool *tx_mbuf_pool)
{
	uint16_t nb_tx, ctrl_nb_tx;
	struct rte_mbuf *ctrl_pkt;
	char *buf_ptr;

	/* send data packet */
	nb_tx = rte_eth_tx_burst(port, qid, tx_pkts, nb_pkts);
	if (nb_tx != nb_pkts) {
//		printf("GODZILA: failed to send packet\n");
	} else {
//		printf("GODZILA: send data\n");
	}

	if (nb_tx == 0) {
		// no pkt was sent 
		return 0;
	}

	if (send_ctrl_pkt) {
		/* send control packet */
		ctrl_pkt = rte_pktmbuf_alloc(tx_mbuf_pool);
		if (ctrl_pkt == NULL)
			printf("(bkdrft) send_pkt: Failed to allocate mbuf for cntrl pkt\n");
		buf_ptr = rte_pktmbuf_append(ctrl_pkt, sizeof(uint8_t));
		struct ctrl_pkt *cpkt = (struct ctrl_pkt *)buf_ptr;
		cpkt->q = qid;
		ctrl_nb_tx = rte_eth_tx_burst(port, 0, &ctrl_pkt, 1);
		if (ctrl_nb_tx != 1) {
//			printf("failed to send ctrl_pkt\n");
		} else {
//			printf("send ctrl pkt\n");
		}
  }

	return nb_tx;
}
