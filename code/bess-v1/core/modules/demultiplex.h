#ifndef BESS_MODULES_DEMULTIPLEX_H
#define BESS_MODULES_DEMULTIPLEX_H

#include "../module.h"
#include "../pb/module_msg.pb.h"

struct Range {
  uint16_t start;
  uint16_t end;
  gate_idx_t gate;
};

class Demultiplex final : public Module {
 public:
  static const gate_idx_t kNumOGates = MAX_GATES;
  
  Demultiplex(): count_range_(), ranges_(), count_gates_(), gates_() {
    max_allowed_workers_ = Worker::kMaxWorkers;
  }

  CommandResponse Init(const bess::pb::DemultiplexArg &arg);
  void ProcessBatch(Context *ctx, bess::PacketBatch *batch) override;

 private:
  static const gate_idx_t default_gate = 0; 
  static const int max_count_range = 32;
  uint32_t count_range_;
  Range ranges_[Demultiplex::max_count_range];
  uint32_t count_gates_;
  gate_idx_t gates_[MAX_GATES];


  void Broadcast(Context *ctx, bess::Packet *pkt);
};

#endif // BESS_MODULES_DEMULTIPLEX_H

