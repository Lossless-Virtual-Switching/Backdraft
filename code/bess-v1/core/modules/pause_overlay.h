#ifndef BESS_MODULES_PAUSEOVERLAY_H_
#define BESS_MODULES_PAUSEOVERLAY_H_

#include "../module.h"
#include "../pb/module_msg.pb.h"
#include "../utils/bkdrft_overlay_ctrl.h"

class PauseOverlay final : public Module {
 public:
  static const gate_idx_t kNumOGates = 1;

  PauseOverlay() : Module() {}

  CommandResponse Init(const bess::pb::PauseOverlayArg &arg);

  void DeInit() override;

  void ProcessBatch(Context *ctx, bess::PacketBatch *batch) override;

  std::string GetDesc() const override;

};

#endif //  BESS_MODULES_PAUSEOVERLAY_H_
