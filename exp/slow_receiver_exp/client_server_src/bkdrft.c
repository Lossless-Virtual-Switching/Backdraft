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
	uint16_t i;
	uint32_t bytes = 0;

	/* send data packet */
	nb_tx = rte_eth_tx_burst(port, qid, tx_pkts, nb_pkts);

	if (nb_tx == 0)
		return nb_tx;

	for (i = 0; i < nb_tx; i++)
		bytes += tx_pkts[i]->pkt_len;

	if (send_ctrl_pkt) {
		/* send control packet */
		ctrl_pkt = rte_pktmbuf_alloc(tx_mbuf_pool);
		if (ctrl_pkt == NULL) {
			printf("(bkdrft) send_pkt: Failed to allocate mbuf for ctrl pkt\n");
		} else {
			buf_ptr = rte_pktmbuf_append(ctrl_pkt, sizeof(struct ctrl_pkt));
			if (buf_ptr == NULL) {
				printf("(bkdrft) send_pkt: There is not enough tail romm\n");
			} else {
				struct ctrl_pkt *cpkt = (struct ctrl_pkt *)buf_ptr;
				cpkt->q = qid;
				cpkt->nb_pkts = nb_tx;
				cpkt->bytes = bytes;
				ctrl_nb_tx = rte_eth_tx_burst(port, BKDRFT_CTRL_QUEUE, &ctrl_pkt, 1);
				// printf("sent ctrl pkt qid: %d\n", cpkt->q);
				if (ctrl_nb_tx != 1) {
					// sending ctrl pkt failed
					rte_pktmbuf_free(ctrl_pkt);
					printf("(bkdrft) failed to send ctrl_pkt\n");
				}
			}
		}
	}

	return nb_tx;
}

extern inline int poll_ctrl_queue(const int port, const int ctrl_qid, const uint16_t burst,
	struct rte_mbuf **recv_bufs, bool blocking)
{
	uint16_t nb_ctrl_rx;
	uint16_t nb_data_rx;
	struct rte_mbuf *ctrl_rx_bufs[1];
	struct rte_mbuf *buf;
	struct ctrl_pkt *ctrlpkt;
	uint8_t dqid;
	for(;;) {
		// read a ctrl packet
		nb_ctrl_rx = rte_eth_rx_burst(port, ctrl_qid, ctrl_rx_bufs, 1);
		if (nb_ctrl_rx == 0) {
			if (blocking) {
				continue;
			} else {
				return 0;
			}
		}

		buf = ctrl_rx_bufs[0];
		ctrlpkt = rte_pktmbuf_mtod(buf, struct ctrl_pkt *);
		dqid = ctrlpkt->q;
		rte_pktmbuf_free(buf); // free ctrl_pkt

		// read data queue
		nb_data_rx = rte_eth_rx_burst(port, dqid, recv_bufs, burst);
		// nb_data_rx = rte_eth_rx_burst(port, 1, recv_bufs, burst);
		if (nb_data_rx == 0) {
			// printf("Read data queue %d but no data\n", 1);
			continue;
		}
		return nb_data_rx; // result is in recv_bufs
	}
}
