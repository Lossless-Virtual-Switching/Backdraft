#ifndef BESS_MODULES_MEASURE_PPS_H
#define BESS_MODULES_MEASURE_PPS_H

#include <vector>
#include "module.h"
#include "pb/module_measure_pps_msg.pb.h"

class MeasurePPS final : public Module
{
public:
  static const Commands cmds;

  MeasurePPS()
      : Module(),
        pkt_cnt_(),
        bytes_cnt_(),
	last_record_ts_()
  {
    max_allowed_workers_ = Worker::kMaxWorkers;
  }

  CommandResponse Init(const bkdrft::pb::MeasurePPSArg &arg);
  void ProcessBatch(Context *ctx, bess::PacketBatch *batch) override;
  CommandResponse CommandGetSummary(const bess::pb::EmptyArg &arg);
  CommandResponse CommandClear(const bess::pb::EmptyArg &arg);

private:
  struct record {
	  uint64_t pps;
	  uint64_t bps;
	  uint64_t timestamp;
  };

  uint64_t pkt_cnt_;
  uint64_t bytes_cnt_;
  uint64_t last_record_ts_;
  std::vector<struct record> book_;
};
#endif

