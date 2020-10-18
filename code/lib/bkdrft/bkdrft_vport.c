#include <assert.h>
#include "bkdrft_vport.h"
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

  if (send_ctrl_pkt) {
    ctrl_pkt = rte_pktmbuf_alloc(ctrl_pool);
    if (unlikely(ctrl_pkt == NULL)) {
      printf("(bkdrft) send_pkt: Failed to allocate mbuf for ctrl pkt\n");
    }

    sample_pkt = pkts[0];
    assert(sample_pkt);

    // TODO: can optimize payload copying to packet
    packed_size = create_bkdraft_ctrl_msg(qid, total_bytes, nb_tx, &payload);
    assert(payload);
    prepare_packet(ctrl_pkt, payload, sample_pkt, packed_size);
    free(payload);

    nb_ctrl_tx = send_packets_vport(port, doorbell, (void **)&ctrl_pkt, 1);
    if (nb_ctrl_tx != 1) {
      // sending ctrl pkt failed
      rte_pktmbuf_free(ctrl_pkt);
      printf("failed to send ctrl_pkt\n");
    }
  }
  return nb_tx;
}
