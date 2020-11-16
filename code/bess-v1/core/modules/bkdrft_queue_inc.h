#ifndef BESS_MODULES_BKDRFTQUEUEINC_H_
#define BESS_MODULES_BKDRFTQUEUEINC_H_

#include <stdint.h>

#include "../module.h"
#include "../pb/module_msg.pb.h"
#include "../pb/bkdrft_msg.pb.h"
#include "../port.h"

#include "../utils/flow.h"
using Flow = bess::bkdrft::Flow;

#define MAX_QUEUES 128

struct queue_pause_status {
	uint64_t until;
	uint64_t failed_ctrl;
  int64_t remaining_dpkt;
}__attribute__((__aligned__(32)));

class BKDRFTQueueInc final : public Module {
 public:
  static const gate_idx_t kNumIGates = 0;
  static const Commands cmds;

  BKDRFTQueueInc() : Module(), port_(), qid_(), prefetch_(), burst_() {}

  CommandResponse Init(const bess::pb::BKDRFTQueueIncArg &arg);

  void DeInit() override;

  struct task_result RunTask(Context *ctx, bess::PacketBatch *batch,
                             void *arg) override;

  std::string GetDesc() const override;

  CommandResponse CommandSetBurst(
      const bess::pb::BKDRFTQueueIncCommandSetBurstArg &arg);

  CommandResponse CommandGetOverlayStats(const bess::pb::EmptyArg &);

 private:
  uint32_t  CDQ(Context *ctx, bess::PacketBatch *batch, queue_t &_qid);
  bool CheckQueuedCtrlMessages(Context *ctx, queue_t *qid, uint32_t *burst);
  uint32_t ReadBatch(queue_t qid, bess::PacketBatch *batch, uint32_t burst);
  bool IsQueuePausedInCache(Context *ctx, queue_t qid);
  bool isManagedQueue(queue_t qid);


 private:
  Port *port_;
  queue_t qid_;
  int prefetch_;
  int burst_;

  // time stamp until when queue is paused
  // it is a cache for what BKDRFTSwDpCtrl knows
  // if this array says it should not wait then
  // we should check BKDRFTSwDpCtrl
  queue_pause_status q_status_[MAX_QUEUES];
  Flow q_status_flows_[MAX_QUEUES];
  std::queue<std::pair<uint16_t, uint32_t>> doorbell_service_queue_;
  int count_overview_seg_;
  uint64_t *overview_mask_;
  bool backpressure_;
  bool cdq_;
  bool overlay_;

  uint16_t count_managed_queues;
  uint16_t doorbell_queue;
  queue_t *managed_queues;

  bess::pb::Ctrl ctrl_pkt_pb_tmp_;
};

#endif  // BESS_MODULES_BKDRFQUEUEINC_H_
