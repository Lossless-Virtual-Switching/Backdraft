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
  int res;
  register uint32_t nb_tx; // number of packets successfully transmitted
  uint32_t nb_ctrl_tx;
  uint32_t total_bytes = 0;
  struct rte_mbuf *ctrl_pkt;
  struct rte_mbuf *sample_pkt;
  size_t packed_size;
  uint8_t *payload = NULL;
  const size_t payload_offset = sizeof(struct rte_ether_hdr) + \
                                sizeof(struct rte_ipv4_hdr) + \
                                sizeof(struct rte_udp_hdr);

  nb_tx = send_packets_vport(port, qid, (void **)pkts, nb_pkts);

  if (unlikely(nb_tx == 0)) return nb_tx;

  // TODO: not calculating total bytes

  if (send_ctrl_pkt) {
    ctrl_pkt = rte_pktmbuf_alloc(ctrl_pool);
    if (unlikely(ctrl_pkt == NULL)) {
      // TODO: keep track of failed ctrl packets
#ifdef DEBUG
      printf("(bkdrft) send_pkt: Failed to allocate mbuf for ctrl pkt\n");
#endif
      return nb_tx;
    }

    // TODO: can optimize payload copying to packet
    payload = rte_pktmbuf_mtod_offset(ctrl_pkt, uint8_t *, payload_offset);
    packed_size = create_bkdraft_ctrl_msg(qid, total_bytes, nb_tx, &payload);
    // assert(payload);

    sample_pkt = pkts[0];
    // assert(sample_pkt);

    // prepare_packet(ctrl_pkt, payload, sample_pkt, packed_size);
    res = prepare_packet(ctrl_pkt, NULL, sample_pkt, packed_size);
    if (unlikely(res != 0)) {
      printf("failed to prepare ctrl packet\n");
      return nb_tx; 
    }
    // free(payload);

    nb_ctrl_tx = send_packets_vport(port, doorbell, (void **)&ctrl_pkt, 1);
    if (unlikely(nb_ctrl_tx != 1)) {
      // sending ctrl pkt failed
      rte_pktmbuf_free(ctrl_pkt);
#ifdef DEBUG
      printf("failed to send ctrl_pkt\n");
#endif
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

static inline int _select_queue(const int32_t queue_status[], int count_queue,
                                uint16_t *selected_q, uint32_t *cnt_pkt)
{
  static int i = 0;
  // register int i = 0;
  register int begin = i;
  do {
    if (queue_status[i] > 0) {
      // there are some packets
      *selected_q = i;
      *cnt_pkt = queue_status[i];
      return 1;
    }
    // next queue
    i += 1;
    i %= count_queue;
  } while(i != begin);
  return 0;
}

int vport_poll_ctrl_queue_expose_qid(struct vport *port, uint16_t ctrl_qid,
                               uint16_t burst, struct rte_mbuf **recv_bufs,
                               int blocking, uint16_t *data_qid)
{
  // TODO: implement ffs priority queue
  // TODO: move queue_status to the vport struct (maybe)
  static int32_t queue_status[MAX_QUEUES_PER_DIR] = {}; // from vport.h
  const int ctrl_burst = 512;
  uint16_t nb_ctrl_rx;
  uint16_t nb_data_rx;
  struct rte_mbuf *ctrl_rx_bufs[ctrl_burst];
  struct rte_mbuf *buf;
  uint16_t dqid;
  uint32_t dq_burst;
  // uint8_t msg[BKDRFT_MAX_MESSAGE_SIZE];
  uint8_t *msg;
  char msg_type;
  void *data;
  size_t size;
  int i;
  int queue_found;

poll_doorbell:
  // queue_found = _select_queue(queue_status, MAX_QUEUES_PER_DIR,
  //                             &dqid, &dq_burst);
  queue_found = 0;

  if (queue_found)
    goto fetch_packets;

  for (;;) {
    // read a ctrl packet
    nb_ctrl_rx = recv_packets_vport(port, ctrl_qid,
                                    (void **)ctrl_rx_bufs, ctrl_burst);
    if (nb_ctrl_rx == 0) {
      if (blocking) {
        continue;
      } else {
        return 0;
      }
    }

    for (i = 0; i < nb_ctrl_rx; i++) {
      buf = ctrl_rx_bufs[i];
      size = get_payload(buf, &data);
      if (unlikely(data == NULL)) {
#ifdef DEBUG
        printf("bkdrft.c: payload is null\n");
#endif
        // assume it was a corrupt packet
        rte_pktmbuf_free(buf); // free ctrl_pkt
        continue;
      }

      // TODO: find a way to get rid of this memory copy
      // memcpy(msg, data, size);
      // msg[size] = '\0';
      msg = (uint8_t *)data;

      // first byte defines the bkdrft message type
      msg_type = msg[0];
#ifdef DEBUG
      if (unlikely(msg_type == BKDRFT_OVERLAY_MSG_TYPE)) {
        printf("We found overlay message\n");
      }
#endif
      if (unlikely(msg_type != BKDRFT_CTRL_MSG_TYPE)) {
        // not a ctrl message
        // printf("poll_ctrl_queue...: not a bkdrft ctrl packet!\n");
        rte_pktmbuf_free(buf); // free ctrl_pkt
        continue;
      }
      // unpacking protobuf
      DpdkNetPerf__Ctrl *ctrl_msg =
          dpdk_net_perf__ctrl__unpack(NULL, size - 2, msg + 1);
      if (unlikely(ctrl_msg == NULL)) {
        // printf("Failed to parse ctrl message\n");
        // assume it was a corrupt packet
#ifdef DEBUG
        printf("poll_ctrl_queue...: corrupt ctrl message!\n");
#endif
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
      queue_status[dqid] += dq_burst;
    }

    queue_found = _select_queue(queue_status, MAX_QUEUES_PER_DIR,
                                &dqid, &dq_burst);
    if (likely(queue_found))
      break;
  }

fetch_packets:
  if (dq_burst > burst)
    dq_burst = burst; // recv_bufs are limited

  // read data queue
  nb_data_rx = recv_packets_vport(port, dqid, (void **)recv_bufs, burst);
  if (unlikely(nb_data_rx == 0)) {
#ifdef DEBUG
    printf("Read data queue %d but no data\n", 1);
#endif
    // Warning: chance of infinite loop if corupted ctrl packet notify
    // wrong number of packets in a queue.
    goto poll_doorbell;
  }

  queue_status[dqid] -= nb_data_rx;

  if (data_qid != NULL) {
    *data_qid = dqid;
  }

  return nb_data_rx; // result is in recv_bufs
}
