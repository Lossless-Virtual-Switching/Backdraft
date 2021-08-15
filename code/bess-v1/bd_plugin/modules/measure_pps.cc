#include "measure_pps.h"
#include "timestamp.h"

#define unused __attribute__((unused))

#define scale_to_1sec(counter, delta) counter*NSEC_IN_SEC/delta

const uint64_t NSEC_IN_SEC = 1000000000;
const uint64_t WINDOW = 500000000;

const Commands MeasurePPS::cmds = {
    {"get_summary", "EmptyArg",
     MODULE_CMD_FUNC(&MeasurePPS::CommandGetSummary), Command::THREAD_SAFE},
    {"clear", "EmptyArg", MODULE_CMD_FUNC(&MeasurePPS::CommandClear),
     Command::THREAD_SAFE},
};


CommandResponse MeasurePPS::Init(const bkdrft::pb::MeasurePPSArg &) {
  return CommandSuccess();
}

void MeasurePPS::ProcessBatch(Context *ctx, bess::PacketBatch *batch) {
  // We don't use ctx->current_ns here for better accuracy
  uint64_t now_ns = tsc_to_ns(rdtsc());
  uint32_t  cnt = batch->cnt();
  pkt_cnt_ += cnt;
  for (uint32_t i = 0; i < cnt; i++) {
    bytes_cnt_ += batch->pkts()[i]->total_len();
  }
  if (last_record_ts_ == 0) {
    last_record_ts_ = now_ns;
    RunNextModule(ctx, batch);
    return;
  }

  uint64_t delta;
  if (now_ns > last_record_ts_) {
    delta = now_ns - last_record_ts_;
  } else {
    delta = now_ns + (ULONG_MAX - last_record_ts_);
  }

  if (delta < NSEC_IN_SEC && delta > NSEC_IN_SEC - WINDOW) {
    struct record rc = {
      .pps = scale_to_1sec(pkt_cnt_, delta),
      .bps = scale_to_1sec((bytes_cnt_ + pkt_cnt_ * 24) * 8, delta),
      .timestamp = now_ns,
    };

    book_.push_back(rc);
    pkt_cnt_ = 0;
    bytes_cnt_ = 0;
    last_record_ts_ = tsc_to_ns(rdtsc());
  }

  if (delta > NSEC_IN_SEC) {
    pkt_cnt_ = 0;
    bytes_cnt_ = 0;
    last_record_ts_ = tsc_to_ns(rdtsc());
  }

  RunNextModule(ctx, batch);
}

CommandResponse MeasurePPS::CommandGetSummary(const bess::pb::EmptyArg &) {
  bkdrft::pb::MeasurePPSCommandGetSummaryResponse r;
  for (const auto &rc : book_) {
    bkdrft::pb::MeasurePPSCommandGetSummaryResponse::Record *item;
    item = r.add_records();
    item->set_pps(rc.pps);
    item->set_bps(rc.bps);
    item->set_timestamp(rc.timestamp);
  }
  return CommandSuccess(r);
}

CommandResponse MeasurePPS::CommandClear(const bess::pb::EmptyArg &) {
  pkt_cnt_ = 0;
  bytes_cnt_ = 0;
  last_record_ts_ = 0;
  return CommandResponse();
}

ADD_MODULE(MeasurePPS, "measure_pps", "measures throughput")
