#ifndef BESS_MODULES_BKDRFTQUEUEINC_H_
#define BESS_MODULES_BKDRFTQUEUEINC_H_

#include <stdint.h>

#include "../module.h"
#include "../pb/module_msg.pb.h"
#include "../port.h"

#include "../utils/flow.h"
using Flow = bess::bkdrft::Flow;

#define MAX_NUMBER_OF_QUEUE 8

struct queue_pause_status {
	uint64_t until;
	Flow flow;
};

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

 private:
  Port *port_;
  queue_t qid_;
  int prefetch_;
  int burst_;

  // time stamp until when queue is paused
  // it is a cache for what BKDRFTSwDpCtrl knows
  // if this array says it should not wait then 
  // we should check BKDRFTSwDpCtrl
  queue_pause_status q_status_[MAX_NUMBER_OF_QUEUE];
  bool backpressure_;
};

#endif  // BESS_MODULES_BKDRFQUEUEINC_H_
