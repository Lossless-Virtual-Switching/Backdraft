#ifndef BD_MODULES_DPFQ_H_
#define BD_MODULES_DPFQ_H_

#include "kmod/llring.h"
#include "module.h"
#include "pb/bkdrft_module_msg.pb.h"
#include "port.h"

class DPFQ final : public Module {
 public:
  // static const Commands cmds;

  DPFQ() : Module(), burst_(), size_()
  {
    is_task_ = true;
    max_allowed_workers_ = Worker::kMaxWorkers;
    // TODO: add parameter for count queue
    for (int q = 0; q < 2; q++) {
      queues_[q] = nullptr;
      bp_state[q] = false;
    }

    propagate_workers_ = false;

  }

  CommandResponse Init(const bkdrft::pb::DPFQArg &arg);
  void DeInit() override;

  struct task_result RunTask(Context *ctx, bess::PacketBatch *batch,
      void *arg) override;

  void ProcessBatch(Context *ctx, bess::PacketBatch *batch) override;

  std::string GetDesc() const override;

  void SignalOverloadBP(bess::Packet *pkt) override;

  void SignalUnderloadBP(bess::Packet *pkt) override;

  int Resize(int slots);
  int HashPacket(bess::Packet *pkt);

 private:
  // // Queue capacity
  int burst_;
  uint64_t size_;
  struct llring *queues_[2];

  bool bp_state[2];

  // Accumulated statistics counters
  struct {
    uint64_t enqueued;
    uint64_t dequeued;
    uint64_t dropped;
  } stats_;
};

#endif
