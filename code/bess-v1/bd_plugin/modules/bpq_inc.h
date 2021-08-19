#ifndef BD_MODULES_BPQ_INC_H_
#define BD_MODULES_BPQ_INC_H_

#include "module.h"
#include "pb/bkdrft_module_msg.pb.h"
#include "port.h"

class BPQInc final : public Module {
 public:
  static const Commands cmds;
  static const gate_idx_t kNumIGates = 0;
  static const gate_idx_t kNumOGates = 2;
  static const int kPauseGeneratorGate = 1;

  BPQInc() : Module(), port_(), qid_(), prefetch_(), burst_(), pause_pkt_sample(nullptr), may_signal_overlay(false) {}

  CommandResponse Init(const bkdrft::pb::BPQIncArg &arg);
  void DeInit() override;

  struct task_result RunTask(Context *ctx, bess::PacketBatch *batch,
                             void *arg) override;

  std::string GetDesc() const override;

  CommandResponse CommandGetSummary(const bess::pb::EmptyArg &arg);
  CommandResponse CommandClear(const bess::pb::EmptyArg &arg);

  void SignalOverloadBP() override {
    rx_pause_frame_++; // We have just received a pause frame message
    if (overload_) {
      return;
    }
    overload_ = true;
    pause_pkt_sample = nullptr;
    may_signal_overlay = true;
  }

 private:
  Port *port_;
  queue_t qid_;
  int prefetch_;
  int burst_;
  // pause packet generation stuff
  bess::Packet *pause_pkt_sample;
  bool may_signal_overlay;
};

#endif
