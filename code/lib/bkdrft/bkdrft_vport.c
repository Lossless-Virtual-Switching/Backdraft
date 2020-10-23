#include <assert.h>
#include "bkdrft_vport.h"
#include "bkdrft_const.h"
#include "bkdrft_msg.pb-c.h" /* bkdrft message protobuf */
#include "packet.h"

extern inline int vport_send_pkt(struct vport *port, uint16_t qid,
                            struct rte_mbuf**pkts,
                            uint16_t nb_pkts, int send_ctrl_pkt,
                            uint16_t doorbell,
                            struct rte_mempool *ctrl_pool)
{
  uint16_t nb_tx; // number of packets successfully transmitted
  uint16_t nb_ctrl_tx;
  uint32_t total_bytes = 0;
  struct rte_mbuf *ctrl_pkt;
  struct rte_mbuf *sample_pkt;
  size_t packed_size;
  uint8_t *payload = NULL;


  nb_tx = send_packets_vport(port, qid, (void **)pkts, nb_pkts);

  if (unlikely(nb_tx == 0)) return nb_tx;

  // TODO: not calculating total bytes

  // if (send_ctrl_pkt) {
  {
    ctrl_pkt = rte_pktmbuf_alloc(ctrl_pool);
    if (unlikely(ctrl_pkt == NULL)) {
      // TODO: keep track of failed ctrl packets
      printf("(bkdrft) send_pkt: Failed to allocate mbuf for ctrl pkt\n");
      return nb_tx;
    }

    sample_pkt = pkts[0];
    assert(sample_pkt);

    // TODO: can optimize payload copying to packet
    // assert(qid != 0);
    packed_size = create_bkdraft_ctrl_msg(qid, total_bytes, nb_tx, &payload);
    assert(payload);
    prepare_packet(ctrl_pkt, payload, sample_pkt, packed_size);
    free(payload);

    // assert(doorbell == 0);
    nb_ctrl_tx = send_packets_vport(port, doorbell, (void **)&ctrl_pkt, 1);
    if (nb_ctrl_tx != 1) {
      // sending ctrl pkt failed
      rte_pktmbuf_free(ctrl_pkt);
      printf("failed to send ctrl_pkt\n");
    }
  }

  return nb_tx;
}

extern inline int vport_poll_ctrl_queue(struct vport *port,
                                        uint16_t ctrl_qid,
                                        uint16_t burst,
                                        struct rte_mbuf **recv_bufs,
                                        int blocking)
{
  return vport_poll_ctrl_queue_expose_qid(port, ctrl_qid, burst, recv_bufs,
                                          blocking, NULL);
}

int vport_poll_ctrl_queue_expose_qid(struct vport *port, uint16_t ctrl_qid,
                               uint16_t burst, struct rte_mbuf **recv_bufs,
                               int blocking, uint16_t *data_qid)
{
  // TODO: implement priority queue and keep track of multiple ctrl packet
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
    nb_ctrl_rx = recv_packets_vport(port, ctrl_qid, (void **)ctrl_rx_bufs, 1);
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

    // TODO: find a way to get rid of this memory copy
    memcpy(msg, data, size);
    msg[size] = '\0';
    rte_pktmbuf_free(buf); // free ctrl_pkt

    // first byte defines the bkdrft message type
    msg_type = msg[0];
    if (unlikely(msg_type == BKDRFT_OVERLAY_MSG_TYPE)) {
      printf("We found overlay message\n");
    }
    if (unlikely(msg_type != BKDRFT_CTRL_MSG_TYPE)) {
      // not a ctrl message
      // printf("poll_ctrl_queue...: not a bkdrft ctrl packet!\n");
      continue;
    }
    // unpacking protobuf
    DpdkNetPerf__Ctrl *ctrl_msg =
        dpdk_net_perf__ctrl__unpack(NULL, size - 1, msg + 1);
    if (unlikely(ctrl_msg == NULL)) {
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
    nb_data_rx = recv_packets_vport(port, dqid, (void **)recv_bufs, burst);
    if (unlikely(nb_data_rx == 0)) {
      // printf("Read data queue %d but no data\n", 1);
      continue;
    }

    if (data_qid != NULL) {
      *data_qid = dqid;
    }

    return nb_data_rx; // result is in recv_bufs
  }
}
