#include "ecn_marker.h"
#include <cstdlib>
#include "../utils/format.h"

#define DEFAULT_QUEUE_SIZE 1024

const Commands ECNMarker::cmds = {
    {"set_burst", "ECNMarkerCommandSetBurstArg",
     MODULE_CMD_FUNC(&ECNMarker::CommandSetBurst), Command::THREAD_SAFE},
    {"set_size", "ECNMarkerCommandSetSizeArg",
     MODULE_CMD_FUNC(&ECNMarker::CommandSetSize), Command::THREAD_UNSAFE},
    {"get_status", "ECNMarkerCommandGetStatusArg",
     MODULE_CMD_FUNC(&ECNMarker::CommandGetStatus), Command::THREAD_SAFE},
    {"get_initial_arg", "EmptyArg", MODULE_CMD_FUNC(&ECNMarker::GetInitialArg),
     Command::THREAD_SAFE},
    {"get_runtime_config", "EmptyArg",
     MODULE_CMD_FUNC(&ECNMarker::GetRuntimeConfig), Command::THREAD_SAFE},
    {"set_runtime_config", "ECNMarkerArg",
     MODULE_CMD_FUNC(&ECNMarker::SetRuntimeConfig), Command::THREAD_UNSAFE}};

int ECNMarker::Resize(int slots) {
  struct llring *old_queue = queue_;
  struct llring *new_queue;

  int bytes = llring_bytes_with_slots(slots);

  new_queue =
      reinterpret_cast<llring *>(std::aligned_alloc(alignof(llring), bytes));
  if (!new_queue) {
    return -ENOMEM;
  }

  int ret = llring_init(new_queue, slots, 0, 1);
  if (ret) {
    std::free(new_queue);
    return -EINVAL;
  }

  /* migrate packets from the old queue */
  if (old_queue) {
    bess::Packet *pkt;

    while (llring_sc_dequeue(old_queue, (void **)&pkt) == 0) {
      ret = llring_sp_enqueue(new_queue, pkt);
      if (ret == -LLRING_ERR_NOBUF) {
        bess::Packet::Free(pkt);
      }
    }

    std::free(old_queue);
  }

  queue_ = new_queue;
  size_ = slots;

  if (backpressure_) {
    AdjustWaterLevels();
  }

  return 0;
}

CommandResponse ECNMarker::Init(const bess::pb::ECNMarkerArg &arg) {
  task_id_t tid;
  CommandResponse err;

  tid = RegisterTask(nullptr);
  if (tid == INVALID_TASK_ID) {
    return CommandFailure(ENOMEM, "Task creation failed");
  }

  burst_ = bess::PacketBatch::kMaxBurst;

  if (arg.backpressure()) {
    VLOG(1) << "Backpressure enabled for " << name() << "::ECNMarker";
    // backpressure_ = true;
  }

  if (arg.size() != 0) {
    err = SetSize(arg.size());
    if (err.error().code() != 0) {
      return err;
    }
  } else {
    int ret = Resize(DEFAULT_QUEUE_SIZE);
    if (ret) {
      return CommandFailure(-ret);
    }
  }

  if (arg.prefetch()) {
    prefetch_ = true;
  }

  if(arg.high_water()) {
    high_water_ = arg.high_water();
  } else {
    VLOG(1) << "High water should be set!";
    return CommandFailure(-1);
  }

  init_arg_ = arg;
  return CommandSuccess();
}

CommandResponse ECNMarker::GetInitialArg(const bess::pb::EmptyArg &) {
  return CommandSuccess(init_arg_);
}

CommandResponse ECNMarker::GetRuntimeConfig(const bess::pb::EmptyArg &) {
  bess::pb::ECNMarkerArg ret;
  ret.set_size(size_);
  ret.set_prefetch(prefetch_);
  ret.set_backpressure(backpressure_);
  return CommandSuccess(ret);
}

CommandResponse ECNMarker::SetRuntimeConfig(const bess::pb::ECNMarkerArg &arg) {
  if (size_ != arg.size() && arg.size() != 0) {
    CommandResponse err = SetSize(arg.size());
    if (err.error().code() != 0) {
      return err;
    }
  }
  prefetch_ = arg.prefetch();
  backpressure_ = arg.backpressure();
  return CommandSuccess();
}

void ECNMarker::DeInit() {
  bess::Packet *pkt;

  LOG(INFO) << "MAX Queue Size : " << max_queue_experienced << "\n";

  if (queue_) {
    while (llring_sc_dequeue(queue_, (void **)&pkt) == 0) {
      bess::Packet::Free(pkt);
    }
    std::free(queue_);
  }
}

std::string ECNMarker::GetDesc() const {
  const struct llring *ring = queue_;

  return bess::utils::Format("%u/%u", llring_count(ring), ring->common.slots);
}

/* from upstream */
void ECNMarker::ProcessBatch(Context *, bess::PacketBatch *batch) {
  int i;
  unsigned int current_size;
  unsigned int prev_size = llring_count(queue_);
  int queued =
      llring_mp_enqueue_burst(queue_, (void **)batch->pkts(), batch->cnt());
  // if (backpressure_ && llring_count(queue_) > high_water_) {
  //   SignalOverload();
  // }
  //

  current_size = llring_count(queue_);
  if (current_size > high_water_) {
    // TODO: check if the packets are TCP/IP
    // mark packets that exceed the high water limit
    for (i = high_water_ - prev_size; i < queued; i++) {
      ecnMark(batch->pkts()[i]);
    }
  }

  // TODO: what to do for the drop.
  // BKDRFT backpressure mechanism will not prevent drop in this module

  stats_.enqueued += queued;

  if (queued < batch->cnt()) {
    int to_drop = batch->cnt() - queued;
    stats_.dropped += to_drop;
    bess::Packet::Free(batch->pkts() + queued, to_drop);
  }

  if (current_size > max_queue_experienced) {
    max_queue_experienced = current_size;
    LOG(INFO) << "MAX Queue Size : " << max_queue_experienced << "\n";
  }
}

/* to downstream */
struct task_result ECNMarker::RunTask(Context *ctx, bess::PacketBatch *batch,
                                  void *) {
  if (children_overload_ > 0) {
    return {
        .block = true,
        .packets = 0,
        .bits = 0,
    };
  }

  const int burst = ACCESS_ONCE(burst_);
  const int pkt_overhead = 24;

  uint64_t total_bytes = 0;

  uint32_t cnt = llring_sc_dequeue_burst(queue_, (void **)batch->pkts(), burst);

  if (cnt == 0) {
    return {.block = true, .packets = 0, .bits = 0};
  }

  stats_.dequeued += cnt;
  batch->set_cnt(cnt);

  if (prefetch_) {
    for (uint32_t i = 0; i < cnt; i++) {
      total_bytes += batch->pkts()[i]->total_len();
      rte_prefetch0(batch->pkts()[i]->head_data());
    }
  } else {
    for (uint32_t i = 0; i < cnt; i++) {
      total_bytes += batch->pkts()[i]->total_len();
    }
  }

  RunNextModule(ctx, batch);

  if (backpressure_ && llring_count(queue_) < low_water_) {
    SignalUnderload();
  }

  return {.block = false,
          .packets = cnt,
          .bits = (total_bytes + cnt * pkt_overhead) * 8};
}

CommandResponse ECNMarker::CommandSetBurst(
    const bess::pb::ECNMarkerCommandSetBurstArg &arg) {
  uint64_t burst = arg.burst();

  if (burst > bess::PacketBatch::kMaxBurst) {
    return CommandFailure(EINVAL, "burst size must be [0,%zu]",
                          bess::PacketBatch::kMaxBurst);
  }

  burst_ = burst;
  return CommandSuccess();
}

CommandResponse ECNMarker::SetSize(uint64_t size) {
  if (size < 4 || size > 16384) {
    return CommandFailure(EINVAL, "must be in [4, 16384]");
  }

  if (size & (size - 1)) {
    return CommandFailure(EINVAL, "must be a power of 2");
  }

  int ret = Resize(size);
  if (ret) {
    return CommandFailure(-ret);
  }

  return CommandSuccess();
}

CommandResponse ECNMarker::CommandSetSize(
    const bess::pb::ECNMarkerCommandSetSizeArg &arg) {
  return SetSize(arg.size());
}

CommandResponse ECNMarker::CommandGetStatus(
    const bess::pb::ECNMarkerCommandGetStatusArg &) {
  bess::pb::ECNMarkerCommandGetStatusResponse resp;
  resp.set_count(llring_count(queue_));
  resp.set_size(size_);
  resp.set_enqueued(stats_.enqueued);
  resp.set_dequeued(stats_.dequeued);
  resp.set_dropped(stats_.dropped);
  return CommandSuccess(resp);
}

void ECNMarker::AdjustWaterLevels() {
  high_water_ = static_cast<uint64_t>(size_ * kHighWaterRatio);
  low_water_ = static_cast<uint64_t>(size_ * kLowWaterRatio);
}

CheckConstraintResult ECNMarker::CheckModuleConstraints() const {
  CheckConstraintResult status = CHECK_OK;
  if (num_active_tasks() - tasks().size() < 1) {  // Assume multi-producer.
    LOG(ERROR) << "ECNMarker has no producers";
    status = CHECK_NONFATAL_ERROR;
  }

  if (tasks().size() > 1) {  // Assume single consumer.
    LOG(ERROR) << "More than one consumer for the queue" << name();
    return CHECK_FATAL_ERROR;
  }

  return status;
}

ADD_MODULE(ECNMarker, "ecn_marker",
           "ECN marks the packets. terminates current task.")
