#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_mbuf.h>

#include "bkdrft.h"
#include "packet.h"
#include "bkdrft_msg.pb-c.h"

extern inline int send_pkt(int port, uint8_t qid, struct rte_mbuf **tx_pkts,
                           uint16_t nb_pkts, bool send_ctrl_pkt,
                           struct rte_mempool *tx_mbuf_pool) {
  uint16_t nb_tx, ctrl_nb_tx;
  struct rte_mbuf *ctrl_pkt;
  uint16_t i;
  uint32_t bytes = 0;
  struct rte_mbuf *sample_pkt;
  size_t packed_size;
  unsigned char *payload = NULL;

  /* send data packet */
  // printf("before sending packet\n");
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
      sample_pkt = tx_pkts[0];
      assert(sample_pkt);

      packed_size = create_bkdraft_ctrl_msg(qid, bytes, nb_tx, &payload);
      assert(payload);
      prepare_packet(ctrl_pkt, payload, sample_pkt, packed_size);
      free(payload);
      ////
      // printf("before sending ctrl pkt\n");
      ctrl_nb_tx = rte_eth_tx_burst(port, BKDRFT_CTRL_QUEUE, &ctrl_pkt, 1);
      // printf("try to send ctrl pkt qid: %d\n", qid);
      if (ctrl_nb_tx != 1) {
        // sending ctrl pkt failed
        rte_pktmbuf_free(ctrl_pkt);
        printf("failed to send ctrl_pkt\n");
      }
    }
  }

  return nb_tx;
}

size_t create_bkdraft_ctrl_msg(uint8_t qid, uint32_t nb_bytes, uint32_t nb_pkts,
                               unsigned char **buf) {
  size_t size = 0;
  // size_t packed_size = 0;
  unsigned char *payload;

  // memset(ctrl_msg, 0, sizeof(DpdkNetPerf__Ctrl));

  DpdkNetPerf__Ctrl ctrl_msg = DPDK_NET_PERF__CTRL__INIT;
  // dpdk_net_perf__ctrl__init(&ctrl_msg);
  ctrl_msg.qid = qid;
  ctrl_msg.total_bytes = nb_bytes;
  ctrl_msg.nb_pkts = nb_pkts;

  size = dpdk_net_perf__ctrl__get_packed_size(&ctrl_msg);
  size = size + 1; // one byte for adding message type tag

  // TODO: check if malloc has performance hit.
  payload = malloc(size);
  assert(payload);

  // first byte defines the bkdrft message type
  payload[0] = BKDRFT_CTRL_MSG_TYPE;
  dpdk_net_perf__ctrl__pack(&ctrl_msg, payload + 1);

  *buf = payload;
  return size;
}

int poll_ctrl_queue(const int port, const int ctrl_qid, uint16_t burst,
                    struct rte_mbuf **recv_bufs, bool blocking) {
  return poll_ctrl_queue_expose_qid(port, ctrl_qid, burst, recv_bufs, blocking,
                                    NULL);
}

int poll_ctrl_queue_expose_qid(const int port, const int ctrl_qid,
                               uint16_t burst, struct rte_mbuf **recv_bufs,
                               bool blocking, uint16_t *data_qid) {
  uint16_t nb_ctrl_rx;
  uint16_t nb_data_rx;
  struct rte_mbuf *ctrl_rx_bufs[burst];
  struct rte_mbuf *buf;
  uint8_t dqid;
  uint8_t msg[BKDRFT_MAX_MESSAGE_SIZE];
  char msg_type;
  void *data;
  size_t size;
  uint16_t dq_burst;
  uint16_t filled_packets = 0;  // packets set in recv_bufs

  uint8_t found_data_queue;
  static const uint16_t max_queue = 8; // supporting 8 queues
  static int64_t outstanding_pkts[] = {0,0,0,0,0,0,0,0};

  for (;;) {
    // read a ctrl packet
    nb_ctrl_rx = rte_eth_rx_burst(port, ctrl_qid, ctrl_rx_bufs, burst);
    
    if (nb_ctrl_rx == 0) {
      if (blocking) {
        continue;
      } else {
        return filled_packets;
      }
    }

    for (uint32_t i = 0; i < nb_ctrl_rx; i++) {
      buf = ctrl_rx_bufs[i];

      // try to parse bkdrft message
      size = get_payload(buf, &data);
      if (data == NULL) {
        // assume it was a corrupt packet
        // rte_pktmbuf_free(buf);
        // continue;
        
        // pass the packet to the engine
        recv_bufs[filled_packets++] = buf; 
        if (data_qid != NULL)
          *data_qid = 0;
      }

      memcpy(msg, data, size);
      msg[size] = '\0';
      // first byte defines the bkdrft message type
      msg_type = msg[0];
      if (msg_type != BKDRFT_CTRL_MSG_TYPE) {
        // not a ctrl message
        // printf("poll_ctrl_queue...: not a bkdrft ctrl packet!\n");
        // rte_pktmbuf_free(buf); // free ctrl_pkt
        // continue;

        // pass the packet to the engine
        recv_bufs[filled_packets++] = buf; 
        if (data_qid != NULL)
          *data_qid = 0;
      }

      // we do not need mbuf any more
      rte_pktmbuf_free(buf); // free ctrl_pkt

      // unpacking protobuf
      DpdkNetPerf__Ctrl *ctrl_msg =
          dpdk_net_perf__ctrl__unpack(NULL, size - 1, msg + 1);
      if (ctrl_msg == NULL) {
        // assume it was a corrupt packet
        printf("poll_ctrl_queue: corrupt ctrl message!\n");
        continue;
      }
      dqid = (uint8_t)ctrl_msg->qid;
      dq_burst = (uint16_t)ctrl_msg->nb_pkts;
      // TODO: msg->total_bytes is not used yet!
      dpdk_net_perf__ctrl__free_unpacked(ctrl_msg, NULL);

      outstanding_pkts[dqid] += dq_burst;
    }

    // select a data queue
    found_data_queue = 0;
    for (uint16_t i = 0; i < max_queue; i++) {
      if (outstanding_pkts[i] > 0) {
        dqid = i;
        dq_burst = outstanding_pkts[i];
        found_data_queue = 1;
        break;
      }
    }

    if (!found_data_queue)
      continue;

    // read data queue
    if (burst - filled_packets < dq_burst) {
      dq_burst = burst - filled_packets;
    }

    nb_data_rx = rte_eth_rx_burst(port, dqid, recv_bufs + filled_packets,
                                  dq_burst);
    outstanding_pkts[dqid] -= nb_data_rx;
    filled_packets += nb_data_rx;

    if (filled_packets == 0)
      continue;

    if (nb_data_rx > 0 && data_qid != NULL) {
      *data_qid = dqid;
    }

    return filled_packets; // result is in recv_bufs
  }
}
