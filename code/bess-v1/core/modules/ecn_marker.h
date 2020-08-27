#ifndef BESS_MODULES_ECNMARKER_H_
#define BESS_MODULES_ECNMARKER_H_

#include "../kmod/llring.h"
#include "../module.h"
#include "../pb/module_msg.pb.h"
#include "../utils/ether.h"
#include "../utils/ip.h"

static inline void ecnMark(bess::Packet *pkt) {
  using bess::utils::Ethernet;
  using bess::utils::Ipv4;

  // TODO: make sure pkt is for a TCP/IP  stack
  Ethernet *eth = pkt->head_data<Ethernet *>();
  Ipv4 *ip = reinterpret_cast<Ipv4 *>(eth + 1);

  if (ip->protocol == Ipv4::Proto::kTcp)
    ip->type_of_service = 0x03;
}

class ECNMarker : public Module {
 public:
  static const Commands cmds;

  ECNMarker()
      : Module(),
        queue_(),
        prefetch_(),
        backpressure_(),
        burst_(),
        size_(),
        high_water_(),
        low_water_(),
        stats_() {
    is_task_ = true;
    propagate_workers_ = false;
    max_allowed_workers_ = Worker::kMaxWorkers;
  }

  CommandResponse Init(const bess::pb::ECNMarkerArg &arg);
  CommandResponse GetInitialArg(const bess::pb::EmptyArg &);
  CommandResponse GetRuntimeConfig(const bess::pb::EmptyArg &arg);
  CommandResponse SetRuntimeConfig(const bess::pb::ECNMarkerArg &arg);

  void DeInit() override;

  struct task_result RunTask(Context *ctx, bess::PacketBatch *batch,
                             void *arg) override;
  void ProcessBatch(Context *ctx, bess::PacketBatch *batch) override;

  std::string GetDesc() const override;

  CommandResponse CommandSetBurst(const bess::pb::ECNMarkerCommandSetBurstArg &arg);
  CommandResponse CommandSetSize(const bess::pb::ECNMarkerCommandSetSizeArg &arg);
  CommandResponse CommandGetStatus(
      const bess::pb::ECNMarkerCommandGetStatusArg &arg);

  CheckConstraintResult CheckModuleConstraints() const override;

 private:
  const double kHighWaterRatio = 0.90;
  const double kLowWaterRatio = 0.15;

  int Resize(int slots);

  // Readjusts the water level according to `size_`.
  void AdjustWaterLevels();

  CommandResponse SetSize(uint64_t size);

  struct llring *queue_;
  bool prefetch_;

  // Whether backpressure should be applied or not
  bool backpressure_;

  int burst_;

  // ECNMarker capacity
  uint64_t size_;

  // High water occupancy
  uint64_t high_water_;

  // Low water occupancy
  uint64_t low_water_;

  // Accumulated statistics counters
  struct {
    uint64_t enqueued;
    uint64_t dequeued;
    uint64_t dropped;
  } stats_;

  bess::pb::ECNMarkerArg init_arg_;
  uint64_t max_queue_experienced;
};

#endif  // BESS_MODULES_ECNMARKER_H_
