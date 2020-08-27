#ifndef BESS_MODULES_BKDRFTQUEUEOUT_H_
#define BESS_MODULES_BKDRFTQUEUEOUT_H_

// #include <string>
#include <stdint.h>
#include <vector>
#include <rte_hash_crc.h>

#include "../module.h"
#include "../packet_pool.h"
#include "../pb/module_msg.pb.h"
#include "../port.h"

#include "../utils/flow.h"
#include "../utils/cuckoo_map.h"


const queue_t MAX_QUEUES = 8;
const int drop_high_water = 30; // assumming batch size is 32
const int buffer_len_high_water = 256;
const int buffer_len_low_water = 64;

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

struct flow_state {
  queue_t qid; // the flow is mapped to this qid
  OverlayState overlay_state; 
  uint64_t ts_last_overlay; // when was last overlay sent
  uint64_t no_overlay_duration;
};

class BKDRFTQueueOut final : public Module {
public:
  static const gate_idx_t kNumOGates = 0;
  static const Commands cmds;

  BKDRFTQueueOut()
      : Module(), port_(), count_queues_(), lossless_(), backpressure_(),
        log_(), cdq_(), per_flow_buffering_(), overlay_(),
        pause_call_total(),
        pause_call_begin_ts_() {}

  CommandResponse Init(const bess::pb::BKDRFTQueueOutArg &arg);

  void DeInit() override;

  void ProcessBatch(Context *ctx, bess::PacketBatch *batch) override;

  struct task_result RunTask(Context *ctx, bess::PacketBatch *batch,
                             void *arg) override;

  std::string GetDesc() const override;

  CommandResponse CommandPauseCalls(const bess::pb::EmptyArg &);

private:
  // place not sent packets in the buffer for the given flow
  void BufferBatch(bess::bkdrft::Flow &flow, flow_state *fstate,
                             bess::PacketBatch *batch, uint16_t sent_pkt);

  // returns the size of assigned buffer for the given flow
  uint64_t BufferSize(const Flow &flow);

  // try to send packets in the buffer
  void TrySendBuffer(Context *cntx);

  // try to send packets in the buffer
  void TrySendBufferPFQ(Context *cntx);

  // send ctrl packet for the batch of packets sent on qid
  uint16_t SendCtrlPkt(Port *p, queue_t qid, uint16_t sent_pkts,
                       uint32_t sent_bytes);

  void UpdatePortStats(queue_t qid, uint16_t sent_pkts, uint16_t droped,
                       bess::PacketBatch *batch);

  // routine for the lossless mode ( mode with buffering)
  void ProcessBatchWithBuffer(Context *ctx, bess::PacketBatch *batch);

  // routine for the mode with out buffering
  void ProcessBatchLossy(Context *ctx, bess::PacketBatch *batch);

  // try to send ctrl packets which were not sent
  bool TryFailedCtrlPackets();

  // performe a pause cal for the given flow
  void Pause(Context *cntx, const Flow &flow, const queue_t qid,
                       uint64_t buffer_size);

  // map a flow to a queue (or get the queue it was mapped before)
  flow_state *GetFlowState(Context *cntx, Flow &flow);

  void MeasureForPolicy(Context *cntx, queue_t qid, const Flow &flow,
                        uint16_t sent_pkts, uint32_t sent_bytes,
                        uint16_t tx_burst, uint32_t total_bytes);

  /*
   * returns the number of sent packets
   * the total sent bytes and the status of ctrl packet can be checked from
   * exposed pointers. Pointers are ignored if they are null.
   */
  int SendPacket(Port *p, queue_t qid, bess::Packet **pkts, uint32_t cnt,
                 uint64_t *tx_bytes, bool *ctrl_pkt_sent);

  // send an overlay message for the given flow
  // qid: the queue the flow is mapped to
  // (or the queue it is going to be send on)
  int SendOverlay(const Flow &flow, queue_t qid, OverlayState state, uint64_t *duration=nullptr);

private:
  using bufferHashTable =
      bess::utils::CuckooMap<Flow, std::vector<bess::Packet *> *, Flow::Hash,
                             Flow::EqualTo>;
  
  Port *port_;
  queue_t qid_;

  uint16_t count_queues_;
  bool lossless_;
  bool backpressure_;
  bool log_;
  bool cdq_;
  bool per_flow_buffering_;
  bool multiqueue_;
  bool overlay_;
  uint32_t ecn_threshold_;

  // This buffer is useful when per flow queueing is active
  // we buffer each packet in its seperate queue
# ifdef HOLB
  static bufferHashTable buffers_;

  // The limmited_buffers_ are used for buffering packets when
  // per flow queueing is not active.
  static std::vector<bess::Packet *> limited_buffers_[MAX_QUEUES];

  static uint64_t count_packets_in_buffer_;
  static uint64_t bytes_in_buffer_;
# else
  bufferHashTable buffers_;

  // The limmited_buffers_ are used for buffering packets when
  // per flow queueing is not active.
  std::vector<bess::Packet *> limited_buffers_[MAX_QUEUES];

  uint64_t count_packets_in_buffer_;
  uint64_t bytes_in_buffer_;
# endif

  // This map keeps track of which buffer a flow has been assigned to
  bess::utils::CuckooMap<Flow, struct flow_state *, Flow::Hash, Flow::EqualTo>
      flow_buffer_mapping_;

  uint64_t failed_ctrl_packets[MAX_QUEUES];
  uint64_t pause_call_total;
  uint64_t pause_call_begin_ts_;
  uint64_t pause_call_begin_ts_dummy;
  // data structure for holding pause call per sec values
  std::vector<int> pcps;

  // a  name given to this module. currently used for loggin pause per sec
  // statistics.
  std::string name_;
  queue_flow_info q_info_[MAX_QUEUES];
  // used for determining the flow for which overlay should be generated
  Flow sample_pkt_flow_;
  // used for estimating the drop rate of each flow.
  // when in lossy mode, from this information we decide
  // if pause or overlay should be emmited
  bess::utils::CuckooMap<Flow, double, Flow::Hash, Flow::EqualTo> flow_drop_est;

  // A pointer to ProcessBatch context
  // This is used to avoid passing context to other functions
  // TODO: this is no thread safe
  Context *context_;
};

#endif // BESS_MODULES_BKDRFTQUEUEOUT_H_
