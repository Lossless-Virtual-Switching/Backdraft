#ifndef BESS_MODULES_BKDRFTQUEUEOUT_H_
#define BESS_MODULES_BKDRFTQUEUEOUT_H_

// #include <string>
#include <stdint.h>
#include <vector>
#include <rte_hash_crc.h>
#include <rte_ring.h>
#include <rte_malloc.h>
#include <rte_lcore.h>
#include <rte_errno.h>

#include "../module.h"
#include "../packet_pool.h"
#include "../pb/module_msg.pb.h"
#include "../port.h"

#include "../utils/flow.h"
#include "../utils/cuckoo_map.h"


// const queue_t MAX_QUEUES = 10000; // used MAX_QUEUES_PER_DIR from /core/port.h
// const int buffer_len_high_water = 256;
// const uint64_t buffer_len_high_water = 15000; // bytes
// const int buffer_len_low_water = 64;
// const uint64_t buffer_len_low_water = 6000; // bytes

const uint64_t overlay_max_pause_duration = 10000000;
const uint32_t max_availabe_flows = 256;
const uint64_t flow_dealloc_limit = 100000000; // ns
// const int bp_buffer_len_high_water = 2700;
const uint64_t max_overlay_pause_duration = 5000000; // ns
// TODO: may use HEAP, RB-Tree or ... for priority queue
const int COUNT_PRIORITY = 2;
const int PRIORITY_QUEUE_LEN = 64; // flow in prio
const int ctrl_batch_size = 1024;

using bess::bkdrft::Flow;
struct queue_flow_info {
  // TODO: remove the assumption of one flow per queue
  // [probably an array of flows should be used]
  Flow flow;
  uint64_t last_visit; // when was the last timestamp of seeing this flow
};

enum OverlayState {
  SAFE,
  TRIGGERED,
};

/* pktbuffer_t ====================== */
const static int32_t peek_size = 32;
struct bkdrft_buffer {
  struct rte_ring *ring_queue;
  uint32_t tail; // next index to insert packet
  uint32_t pkts; // number of packets in peek and ring
  bess::Packet *peek[peek_size];  // same size as max burst
};

typedef struct bkdrft_buffer pktbuffer_t;

/* create a new buffer for keeping packet pointers */
pktbuffer_t *new_pktbuffer(char *name, uint32_t max_buffer_size)
{
  pktbuffer_t *buf = reinterpret_cast<pktbuffer_t *> (
      rte_malloc(NULL, sizeof(pktbuffer_t), 0));
  if (buf == nullptr)
    return nullptr;

  // in dpdk20: add RING_F_MP_RTS_ENQ|RING_F_MC_RTS_DEQ to flag.
  buf->ring_queue = rte_ring_create(name, max_buffer_size, rte_socket_id(), 0);
  if (buf->ring_queue == nullptr) {
    LOG(INFO) << "failed rte_ring_create() for pktbuffer_t\n";
    LOG(INFO) << "errno: " << rte_errno << ": " << rte_strerror(rte_errno) << "\n";
    rte_free(buf);
    return nullptr;
  }

  buf->tail = 0;
  buf->pkts = 0;
  return buf;
}

/* free memory alloccated for a pktbuffer */
void free_pktbuffer(pktbuffer_t *buf)
{
  rte_ring_free(buf->ring_queue);
  rte_free(buf);
}

/* enqueues pointer of packets to the pktbuffer_t
 * returns number of enqueued elements
 * */
int pktbuffer_enqueue(pktbuffer_t *buf, bess::Packet **pkts, uint32_t count)
{
  uint32_t ret;
  uint32_t cur_index = 0; // keep track of index of the external buffer

  // find out how many packets are in the peek
  // int32_t peek_pkts = (buf->tail - buf->head) % peek_size;
  int32_t peek_pkts = buf->tail;

  // how many packets should be moved to the peek
  uint32_t to_the_peek = peek_size - peek_pkts;
  if (count < to_the_peek)
    to_the_peek = count; // min(count, free_peek_space);

  // move packets to peek array if there is space
  for (;cur_index < to_the_peek; cur_index++)
    buf->peek[buf->tail++] = pkts[cur_index];
  buf->pkts += to_the_peek;

  // how many packets should be moved to the ring
  uint32_t to_the_ring = count - to_the_peek;
  if (to_the_ring == 0)
    return to_the_peek;

  // move packets to ring
  ret = rte_ring_enqueue_bulk(buf->ring_queue, (void **)(pkts + cur_index),
                              to_the_ring, NULL);
  ret = ret & 0x7fffffff;
  // if ret == 0 then failed to enqueue
  buf->pkts += ret;
  return ret + to_the_peek;
}

/* remove packets from the queue and if **pkts is not null
 * copies the pointers to the buffer.
 * returns number of packets dequeued.
 * */
int pktbuffer_dequeue(pktbuffer_t *buf, bess::Packet **pkts, uint32_t count) {
  // count should not be more than pkts available
  if (unlikely(count > buf->pkts))
    count = buf->pkts;

  // find how many packets are in the peek
  int32_t peek_pkts = buf->tail;

  // how many to dequeue from peek
  int32_t from_peek = count;
  if (unlikely(from_peek > peek_pkts))
    from_peek = peek_pkts;

  // dequeue from peek
  if (unlikely(pkts != nullptr)) {
    // copy pointers from peek to the external buffer
    for (int32_t i = 0; i < from_peek; i++)
      pkts[i] = buf->peek[i];
  }

  // move packet pointers to the 0 index
  for (int32_t i = 0, j = from_peek; j < peek_pkts; i++, j++)
    buf->peek[i] = buf->peek[j];

  buf->tail -= from_peek;
  buf->pkts -= from_peek;

  // dequeue from ring
  int32_t ret;
  bess::Packet *tmp_arr[peek_size];
  // how many to dequeue from ring
  int32_t from_ring = count - from_peek;
  if (unlikely(from_ring != 0)) {
    if (unlikely(pkts != nullptr)) {
      ret = rte_ring_dequeue_bulk(buf->ring_queue, (void **)(pkts + from_peek),
                                  from_ring, nullptr);
    } else {
      ret = rte_ring_dequeue_bulk(buf->ring_queue, (void **)(tmp_arr),
                                  from_ring, nullptr);
    }
    if (unlikely(!ret)) {
      LOG(FATAL) << "pktbuffer_dequeue: failed to dequee from ring\n";
    }
    buf->pkts -= from_ring;
  }

  // update number of packets in the peek
  peek_pkts = peek_pkts - from_peek;
  // how much free space is on the peek
  int32_t pull_to_peek = peek_size - peek_pkts;
  // how many packets in the ring
  int32_t ring_pkts = buf->pkts - peek_pkts;
  if (pull_to_peek > ring_pkts)
    pull_to_peek = ring_pkts;

  if (pull_to_peek > 0) {
    // ret = rte_ring_dequeue_bulk(buf->ring_queue, (void **)(tmp_arr),
    //                             pull_to_peek, nullptr);
    ret = rte_ring_dequeue_bulk(buf->ring_queue,
                                (void **)(buf->peek + buf->tail),
                                pull_to_peek, nullptr);
    if (unlikely(!ret)) {
      LOG(FATAL) << "pktbuffer_dequeue: failed to pull packets to peek\n";
    }
    buf->tail += ret; // mail tail forward

    // copy packet pointeres to peek
    // for (int32_t i=0; i < ret; i++)
    //   buf->peek[buf->tail++] = tmp_arr[i];
  }

  return from_peek + from_ring;
}
/* pktbuffer_t ====================== */

struct flow_state {
  uint32_t flow_id;
  queue_t qid; // the flow is mapped to this qid
  uint32_t prio;
  OverlayState overlay_state; // overlay state (TRIGGERED, SAFE)
  uint64_t ts_last_overlay; // when was last overlay sent
  uint64_t no_overlay_duration; // for what duration no overlay should be sent
  pktbuffer_t *buffer; // pointer to queue buffer
  bool in_use; // indicating if the object is being used by a flow
  uint64_t last_used; // keeps track of the usage
} __attribute__((aligned(64)));

class BKDRFTQueueOut final : public Module {
public:
  static const gate_idx_t kNumOGates = 0;
  static const Commands cmds;

  BKDRFTQueueOut()
      : Module(), port_(), count_queues_(), buffering_(), backpressure_(),
        log_(), cdq_(), per_flow_buffering_(), overlay_(),
        pause_call_total() {}

  CommandResponse Init(const bess::pb::BKDRFTQueueOutArg &arg);

  void DeInit() override;

  void ProcessBatch(Context *ctx, bess::PacketBatch *batch) override;

  struct task_result RunTask(Context *ctx, bess::PacketBatch *batch,
                             void *arg) override;

  std::string GetDesc() const override;

  CommandResponse CommandPauseCalls(const bess::pb::EmptyArg &);
  CommandResponse CommandGetCtrlMsgTp(const bess::pb::EmptyArg &);
  CommandResponse CommandGetOverlayTp(const bess::pb::EmptyArg &);
  CommandResponse CommandSetOverlayThreshold(
    const bess::pb::BKDRFTQueueOutCommandSetOverlayThresholdArg &args);
  CommandResponse CommandSetBackpressureThreshold(
    const bess::pb::BKDRFTQueueOutCommandSetBackpressureThresholdArg &arg);

private:
  int SetupFlowControlBlockPool();

  // place not sent packets in the buffer for the given flow
  void BufferBatch(Flow *flow, struct flow_state *fstate,
                             bess::PacketBatch *batch, uint16_t sent_pkt);

  // try to send packets in the buffer
  void TrySendBuffer(Context *cntx);

  // try to send packets in the buffer
  void TrySendBufferPFQ(Context *cntx);

  int TryFlushCtrlBatch();

  // send ctrl packet for the batch of packets sent on qid
  uint16_t SendCtrlPkt(Port *p, queue_t qid, uint32_t prio, uint16_t sent_pkts,
                       uint32_t sent_bytes);

  void UpdatePortStats(queue_t qid, uint16_t sent_pkts, uint16_t droped,
                       bess::PacketBatch *batch);

  // routine for the lossless mode ( mode with buffering)
  void ProcessBatchWithBuffer(Context *ctx, bess::PacketBatch *batch);

  // routine for the mode with out buffering
  void ProcessBatchLossy(Context *ctx, bess::PacketBatch *batch);

  // performe a pause cal for the given flow
  void Pause(Context *cntx, const Flow &flow, const queue_t qid,
                       uint64_t buffer_size);

  // map a flow to a queue (or get the queue it was mapped before)
  struct flow_state *GetFlowState(Context *cntx, Flow &flow);
  void DeallocateFlowState(Context *cntx, Flow &flow);

  /*
   * returns the number of sent packets
   * the total sent bytes and the status of ctrl packet can be checked from
   * exposed pointers. Pointers are ignored if they are null.
   */
  int SendPacket(Port *p, struct flow_state *fstate, bess::Packet **pkts,
                 uint32_t cnt, uint64_t *tx_bytes, bool *ctrl_pkt_sent);

  // send an overlay message for the given flow
  // qid: the queue the flow is mapped to
  // (or the queue it is going to be send on)
  int SendOverlay(const Flow &flow, const struct flow_state *fstate,
                  OverlayState state, uint64_t *duration=nullptr);

private:
  Port *port_;
  uint32_t max_buffer_size_; // the size of output discriptors

  uint16_t count_queues_;
  bool buffering_;
  bool backpressure_;
  bool log_;
  bool cdq_;
  bool per_flow_buffering_;
  bool overlay_;
  uint32_t ecn_threshold_;

  // The limmited_buffers_ are used for buffering packets when
  // per flow queueing is not active.
  pktbuffer_t *limited_buffers_[MAX_QUEUES_PER_DIR];

  uint64_t count_packets_in_buffer_;
  uint64_t bytes_in_buffer_;

  // This map keeps track of which buffer a flow has been assigned to
  bess::utils::CuckooMap<Flow, struct flow_state *, Flow::Hash, Flow::EqualTo>
      flow_buffer_mapping_;

  uint64_t failed_ctrl_packets[MAX_QUEUES_PER_DIR];
  uint64_t pause_call_total;
  // data structure for holding pause call per sec values
  std::vector<int> pcps;
  // control message throughput
  uint64_t ctrl_msg_tp_;
  uint64_t ctrl_msg_tp_last_;
  // overlay throughput
  uint64_t overlay_tp_;
  std::vector<uint64_t> overlay_per_sec;
  uint64_t stats_begin_ts_;

  uint64_t buffer_len_high_water = 6;
  uint64_t buffer_len_low_water = 16;
  uint64_t bp_buffer_len_high_water = 6;

  // a  name given to this module. currently used for loggin pause per sec
  // statistics.
  std::string name_;
  queue_flow_info q_info_[MAX_QUEUES_PER_DIR];
  // used for determining the flow for which overlay should be generated
  Flow sample_pkt_flow_;

  uint16_t doorbell_queue_number_;
  queue_t *data_queues_;

  struct flow_state *flow_state_pool_;
  Flow *flow_state_flow_id_;
  uint32_t fsp_top_; // flow state pool top of the stack
  bool initialized_;

  uint32_t num_filled_buffers_;
  std::set<struct flow_state *> filled_buffers_;

  uint32_t prio_queue_[COUNT_PRIORITY][PRIORITY_QUEUE_LEN];
  int prio_queue_len_[COUNT_PRIORITY];

  bess::Packet *ctrl_batch_[ctrl_batch_size];
  int ctrl_batch_used_;
  uint64_t first_ctrl_pkt_ts_;
  uint64_t current_ns_;
};

#endif // BESS_MODULES_BKDRFTQUEUEOUT_H_
