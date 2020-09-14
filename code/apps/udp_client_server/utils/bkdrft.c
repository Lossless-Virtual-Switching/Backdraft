#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_mbuf.h>

#include "../include/bkdrft.h"
#include "../include/packet.h"
#include "../include/protobuf/bkdrft_msg.pb-c.h"

extern inline int mark_data_queue(struct rte_mbuf *pkt, uint8_t qid) {
  struct rte_ether_hdr *eth = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);
  uint16_t ether_type = rte_be_to_cpu_16(eth->ether_type);

  /* make sure the packet has an ipv4 header */
  if (ether_type != RTE_ETHER_TYPE_IPV4)
    return -1;

  struct rte_ipv4_hdr *ip = (struct rte_ipv4_hdr *)(eth + 1);
  ip->type_of_service = (qid << 2); // ip->type_of_service | (qid << 2);
  ip->hdr_checksum = 0;
  ip->hdr_checksum = rte_ipv4_cksum(ip);
  return 0;
}

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

  if (send_ctrl_pkt) {
    /* mark the packets to be placed on the mentioned queue by the recv nic */
    for (i = 0; i < nb_pkts; i++)
      mark_data_queue(tx_pkts[i], qid);
  }

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
      sample_pkt = tx_pkts[0];
      assert(sample_pkt);

      packed_size = create_bkdraft_ctrl_msg(qid, bytes, nb_tx, &payload);
      assert(payload);
      prepare_packet(ctrl_pkt, payload, sample_pkt, packed_size);
      free(payload);
      ////
      ctrl_nb_tx = rte_eth_tx_burst(port, BKDRFT_CTRL_QUEUE, &ctrl_pkt, 1);
      // printf("sent ctrl pkt qid: %d\n", cpkt->q);
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
  struct rte_mbuf *ctrl_rx_bufs[1];
  struct rte_mbuf *buf;
  uint8_t dqid;
  uint16_t dq_burst;
  uint8_t msg[BKDRFT_MAX_MESSAGE_SIZE];
  char msg_type;
  void *data;
  size_t size;
  for (;;) {
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
    size = get_payload(buf, &data);
    if (data == NULL) {
      printf("bkdrft.c: payload is null\n");
      // assume it was a corrupt packet
      rte_pktmbuf_free(buf); // free ctrl_pkt
      continue;
    }

    memcpy(msg, data, size);
    msg[size] = '\0';
    rte_pktmbuf_free(buf); // free ctrl_pkt

    // first byte defines the bkdrft message type
    msg_type = msg[0];
    if (msg_type == BKDRFT_OVERLAY_MSG_TYPE) {
      printf("We found overlay message\n");
    }
    if (msg_type != BKDRFT_CTRL_MSG_TYPE) {
      // not a ctrl message
      // printf("poll_ctrl_queue...: not a bkdrft ctrl packet!\n");
      continue;
    }
    // unpacking protobuf
    DpdkNetPerf__Ctrl *ctrl_msg =
        dpdk_net_perf__ctrl__unpack(NULL, size - 1, msg + 1);
    if (ctrl_msg == NULL) {
      // printf("Failed to parse ctrl message\n");
      // assume it was a corrupt packet
      // printf("poll_ctrl_queue...: corrupt ctrl message!\n");
      continue;
    }
    dqid = (uint8_t)ctrl_msg->qid;
    dq_burst = (uint16_t)ctrl_msg->nb_pkts;
    // TODO: msg->total_bytes is not used yet!
    // printf("data qid: %d\n", (int)dqid);
    // fflush(stdout);
    dpdk_net_perf__ctrl__free_unpacked(ctrl_msg, NULL);

    // TODO: keep track of packets on each queue
    if (dq_burst > burst)
      dq_burst = burst; // recv_bufs are limited

    // read data queue
    nb_data_rx = rte_eth_rx_burst(port, dqid, recv_bufs, burst);
    if (nb_data_rx == 0) {
      // printf("Read data queue %d but no data\n", 1);
      continue;
    }

    if (data_qid != NULL) {
      *data_qid = dqid;
    }

    return nb_data_rx; // result is in recv_bufs
  }
}
