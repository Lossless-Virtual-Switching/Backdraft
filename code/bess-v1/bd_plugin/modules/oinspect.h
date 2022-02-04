#ifndef BKDRFT_MODULES_OINSPECT_H_
#define BKDRFT_MODULES_OINSPECT_H_

#include "module.h"
#include "pb/bkdrft_module_msg.pb.h"
#include "obroker.h"

class OInspect final : public Module {
 public:
  static const Commands cmds;

  OInspect() : Module(), overlay_broker_(nullptr) { max_allowed_workers_ = Worker::kMaxWorkers; }

  void ProcessBatch(Context *ctx, bess::PacketBatch *batch) override;
  CommandResponse CommandSetOverlayBroker(const bkdrft::pb::CommandSetOverlayBrokerArg &arg);

 private:
 OBroker *overlay_broker_;
};

#endif  // BKDRFT_MODULES_OINSPECT_H_
