#ifndef BD_MODULES_BPQ_INC_H_
#define BD_MODULES_BPQ_INC_H_

#include "module.h"
#include "pb/bkdrft_module_msg.pb.h"
#include "port.h"

class BPQInc final : public Module {
 public:
  static const Commands cmds;
  static const gate_idx_t kNumIGates = 0;

  BPQInc() : Module(), port_(), qid_(), prefetch_(), burst_() {}

  CommandResponse Init(const bkdrft::pb::BPQIncArg &arg);
  void DeInit() override;

  struct task_result RunTask(Context *ctx, bess::PacketBatch *batch,
                             void *arg) override;

  std::string GetDesc() const override;

  CommandResponse CommandGetSummary(const bess::pb::EmptyArg &arg);
  CommandResponse CommandClear(const bess::pb::EmptyArg &arg);

 private:
  Port *port_;
  queue_t qid_;
  int prefetch_;
  int burst_;
};

#endif
