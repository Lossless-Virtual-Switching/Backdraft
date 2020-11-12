#include <stdint.h>

#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_mbuf.h>

#include "bkdrft.h"
#include "bkdrft_const.h"
#include "packet.h" /* helper functions for creating packets */
#include "bkdrft_msg.pb-c.h" /* bkdrft message protobuf */

#define COUNT_PRIORITY 2
struct queue_status {
  int64_t nb_pkts;
  int32_t prio;
};

extern inline int mark_data_queue(struct rte_mbuf *pkt, uint8_t qid) {
	char *new_head;
	__m128i ethh;
  uint16_t tpid;
  uint32_t tag;
  uint16_t tci = BKDRFT_OVERLAY_VLAN_PRIO << 13 | 0 << 12 | qid;

  new_head = rte_pktmbuf_prepend(pkt, 4);
	if (unlikely(new_head == NULL)) {
    return  -1; // failed
  }

	ethh = _mm_loadu_si128((__m128i *)(new_head + 4));
	tpid = rte_be_to_cpu_16(_mm_extract_epi16(ethh, 6));

  if (tpid == RTE_ETHER_TYPE_VLAN) {
    tag =  RTE_ETHER_TYPE_QINQ << 16 | tci;
  } else {
    tag =  RTE_ETHER_TYPE_VLAN << 16 | tci;
  }
  tag = rte_cpu_to_be_32(tag);

	ethh = _mm_insert_epi32(ethh, tag, 3);

	_mm_storeu_si128((__m128i *)(new_head), ethh);
  return 0;
}

extern inline void bkdrft_mark_send_pkt(uint8_t qid,
                                    struct rte_mbuf **pkts, uint16_t nb_pkts)
{
  /* mark the packets to be placed on the mentioned queue by the recv nic */
  for (uint16_t i = 0; i < nb_pkts; i++)
    mark_data_queue(pkts[i], qid);
}

/*
 * This function works only with DPDK ports
 * */
extern inline int send_pkt(int port, uint8_t qid, struct rte_mbuf **tx_pkts,
                           uint16_t nb_pkts, int send_ctrl_pkt,
                           struct rte_mempool *tx_mbuf_pool)
{
  //TODO: this code needs to be updated to supprt static partioning concept
  uint16_t nb_tx, ctrl_nb_tx;
  struct rte_mbuf *ctrl_pkt;
  // uint16_t i;
  uint32_t bytes = 0;
  struct rte_mbuf *sample_pkt;
  size_t packed_size;
  unsigned char *payload = NULL;
  const size_t payload_offset = sizeof(struct rte_ether_hdr) + \
                                sizeof(struct rte_ipv4_hdr) + \
                                sizeof(struct rte_udp_hdr);

  // if (send_ctrl_pkt) {
  //   /* mark the packets to be placed on the mentioned queue by the recv nic */
  //   for (i = 0; i < nb_pkts; i++)
  //     mark_data_queue(tx_pkts[i], qid);
  // }

  /* send data packet */
  nb_tx = rte_eth_tx_burst(port, qid, tx_pkts, nb_pkts);

  if (unlikely(nb_tx == 0))
    return nb_tx;

  // TODO: not calculating total bytes
  // for (i = 0; i < nb_tx; i++)
  //   bytes += tx_pkts[i]->pkt_len;

  if (send_ctrl_pkt) {
    /* send control packet */
    ctrl_pkt = rte_pktmbuf_alloc(tx_mbuf_pool);
    if (likely(ctrl_pkt != NULL)) {
      payload = rte_pktmbuf_mtod_offset(ctrl_pkt, uint8_t *, payload_offset);
      packed_size = create_bkdraft_ctrl_msg(qid, bytes, nb_tx, &payload);

      // assert(payload != NULL);
      sample_pkt = tx_pkts[0];
      // assert(sample_pkt);
      prepare_packet(ctrl_pkt, NULL, sample_pkt, packed_size);
      // free(payload);

      ctrl_nb_tx = rte_eth_tx_burst(port, BKDRFT_CTRL_QUEUE, &ctrl_pkt, 1);
      if (ctrl_nb_tx != 1) {
        // sending ctrl pkt failed
        rte_pktmbuf_free(ctrl_pkt);
#ifdef DEBUG
        printf("failed to send ctrl_pkt\n");
#endif
      }
    }
#ifdef DEBUG
    else {
      printf("(bkdrft) send_pkt: Failed to allocate mbuf for ctrl pkt\n");
    }
#endif
  }

  return nb_tx;
}

int poll_ctrl_queue(const int port, const int ctrl_qid, uint16_t burst,
                    struct rte_mbuf **recv_bufs, int blocking)
{
  return poll_ctrl_queue_expose_qid(port, ctrl_qid, burst, recv_bufs, blocking,
                                    NULL);
}

static inline int _select_queue(const struct queue_status queue_status[],
                                int count_queue, uint16_t *selected_q,
                                uint32_t *cnt_pkt)
{
  // TODO: use a better data structure like heap for finding high priorities
  int prio;
  int i;
  for (prio = 0; prio < COUNT_PRIORITY; prio++) {
    for (i = 0; i < count_queue; i++) {
      if (queue_status[i].prio == prio && queue_status[i].nb_pkts > 0) {
        *selected_q = i;
        *cnt_pkt = queue_status[i].nb_pkts;
        return 1;
      }
    }
  }

  // static int i = 0;
  // // register int i = 0;
  // register int begin = i;
  // do {
  //   if (queue_status[i]. > 0) {
  //     // there are some packets
  //     *selected_q = i;
  //     *cnt_pkt = queue_status[i];
  //     return 1;
  //   }
  //   // next queue
  //   i += 1;
  //   i %= count_queue;
  // } while(i != begin);
  return 0;
}


int poll_ctrl_queue_expose_qid(const int port, const int ctrl_qid,
                               uint16_t burst, struct rte_mbuf **recv_bufs,
                               int blocking, uint16_t *data_qid)
{
  // Notice: only supporting 8 queues here
  const int count_queue = 8;
  static struct queue_status queue_status[8] = {};
  uint32_t ctrl_burst = 512;
  uint16_t nb_ctrl_rx;
  uint16_t nb_data_rx;
  struct rte_mbuf *ctrl_rx_bufs[ctrl_burst];
  struct rte_mbuf *buf;
  uint16_t dqid;
  uint32_t dq_burst;
  uint8_t *msg;
  char msg_type;
  size_t size;
  int i;
  int queue_found;

poll_doorbell:
  queue_found = 0;
  // if (queue_found)
  //   goto fetch_packets;

  for (;;) {
    // read a ctrl packet
    nb_ctrl_rx = rte_eth_rx_burst(port, ctrl_qid, ctrl_rx_bufs, ctrl_burst);
    if (nb_ctrl_rx == 0) {
      if (blocking) {
        continue;
      } else {
        return 0;
      }
    }
    // printf("bkdrft.c: received some ctrl packet (%d)\n", nb_ctrl_rx);

    for (i = 0; i < nb_ctrl_rx; i++) {
      buf = ctrl_rx_bufs[i];
      size = get_payload(buf, (void **)(&msg));
      if (unlikely(msg == NULL)) {
        printf("bkdrft.c: payload is null\n");
        // assume it was a corrupt packet
        rte_pktmbuf_free(buf); // free ctrl_pkt
        continue;
      }

      // first byte defines the bkdrft message type
      msg_type = msg[0];
#ifdef DEBUG
      if (unlikely(msg_type == BKDRFT_OVERLAY_MSG_TYPE)) {
        printf("We found overlay message\n");
      }
#endif
      if (unlikely(msg_type != BKDRFT_CTRL_MSG_TYPE)) {
        // not a ctrl message
        // printf("poll_ctrl_queue...: not a bkdrft ctrl packet! (msg type: %d)\n",
        //        msg_type);
        rte_pktmbuf_free(buf); // free ctrl_pkt
        continue;
      }

      // unpacking protobuf
      DpdkNetPerf__Ctrl *ctrl_msg =
          dpdk_net_perf__ctrl__unpack(NULL, size - 2, msg + 1);
      if (unlikely(ctrl_msg == NULL)) {
        // printf("Failed to parse ctrl message\n");
        // assume it was a corrupt packet
        // printf("poll_ctrl_queue...: corrupt ctrl message!\n");
        rte_pktmbuf_free(buf); // free ctrl_pkt
        continue;
      }
      dqid = (uint8_t)ctrl_msg->qid;
      dq_burst = (uint16_t)ctrl_msg->nb_pkts;
      // TODO: msg->total_bytes is not used yet!
      // printf("data qid: %d\n", (int)dqid);
      // fflush(stdout);
      dpdk_net_perf__ctrl__free_unpacked(ctrl_msg, NULL);
      rte_pktmbuf_free(buf); // free ctrl_pkt

      // keep track of packets on each queue
      queue_status[dqid].nb_pkts += dq_burst;
      // printf("q %d cnt %ld\n", dqid, queue_status[dqid].nb_pkts);
    }

    queue_found = _select_queue(queue_status, count_queue, &dqid, &dq_burst);
    if (likely(queue_found))
      break;
  }

// fetch_packets:
  if (dq_burst > burst)
    dq_burst = burst; // recv_bufs are limited

  // read data queue
  nb_data_rx = rte_eth_rx_burst(port, dqid, recv_bufs, burst);
  if (unlikely(nb_data_rx == 0)) {
    // printf("Read data queue %d but no data\n", 1);
    goto poll_doorbell;
  }

  queue_status[dqid].nb_pkts -= nb_data_rx;

  if (data_qid != NULL) {
    *data_qid = dqid;
  }

  return nb_data_rx; // result is in recv_bufs
}
