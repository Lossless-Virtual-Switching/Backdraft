#ifndef BD_MODULES_BPQ_INC_H_
#define BD_MODULES_BPQ_INC_H_

#include "module.h"
#include "pb/bkdrft_module_msg.pb.h"
#include "port.h"

class BPQInc final : public Module {
 public:
  static const Commands cmds;
  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 5;
  static const int kPauseGeneratorGate = 1;

  BPQInc() : Module(), port_(), qid_(), prefetch_(), burst_() {}

  CommandResponse Init(const bkdrft::pb::BPQIncArg &arg);
  void DeInit() override;

  struct task_result RunTask(Context *ctx, bess::PacketBatch *batch,
                             void *arg) override;

  std::string GetDesc() const override;

  CommandResponse CommandGetSummary(const bess::pb::EmptyArg &arg);
  CommandResponse CommandClear(const bess::pb::EmptyArg &arg);
  CommandResponse CommandSetOverlayPort(const bkdrft::pb::CommandSetOverlayPortArg &arg);

  void SignalOverloadBP(bess::Packet *pkt) override {
    int tx = 0;
    rx_pause_frame_++; // We have just received a pause frame message
    if (overload_) {
      return;
    }

    overload_ = true;

    if (pkt == nullptr) {
      LOG(INFO) << "Send Over Packet: qid: " << (int)overlay_qid_ << " (" << tx << ")\n";
      return;
    }

    tx = overlay_port_->SendPackets(overlay_qid_, &pkt, 1);
    if (tx != 1) {
        bess::Packet::Free(pkt);
    }

    LOG(INFO) << "Send Over Packet: qid: " << (int)overlay_qid_ << " (" << tx << ")\n";
  }

  void SignalUnderloadBP(bess::Packet *pkt) override {
    int tx = 0;
    rx_resume_frame_++; // We have just received a pause frame message
    if (!overload_) {
      return;
    }

    overload_ = false;

    if (pkt == nullptr) {
      LOG(INFO) << "Send Under Packet: qid: " << (int)overlay_qid_ << " (" << tx << ")\n";
      return;
    }

    tx = overlay_port_->SendPackets(overlay_qid_, &pkt, 1);
    if (tx != 1) {
        bess::Packet::Free(pkt);
    }

    LOG(INFO) << "Send Under Packet: qid: " << (int)overlay_qid_ << " (" << tx << ")\n";
  }

 private:
  Port *port_;
  queue_t qid_;
  int prefetch_;
  int burst_;
  // pause packet generation stuff
  // bess::Packet *overload_pkt_sample;
  // bess::Packet *underload_pkt_sample;
  // bool may_signal_overlay;
  // bool may_signal_underload;
  Port *overlay_port_;
  queue_t overlay_qid_;
};

#endif
