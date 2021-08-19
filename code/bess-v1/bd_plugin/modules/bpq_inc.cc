#include "bpq_inc.h"

#include "port.h"
#include "utils/format.h"

const Commands BPQInc::cmds = {
    {"get_summary", "EmptyArg",
     MODULE_CMD_FUNC(&BPQInc::CommandGetSummary), Command::THREAD_SAFE},
    {"clear", "EmptyArg", MODULE_CMD_FUNC(&BPQInc::CommandClear),
     Command::THREAD_SAFE},
};

CommandResponse BPQInc::Init(const bkdrft::pb::BPQIncArg &arg) {
    const char *port_name;
    task_id_t tid;
    CommandResponse err;
    burst_ = bess::PacketBatch::kMaxBurst;
    if (!arg.port().length()) {
        return CommandFailure(EINVAL, "Field 'port' must be specified");
    }
    port_name = arg.port().c_str();
    qid_ = arg.qid();

    const auto &it = PortBuilder::all_ports().find(port_name);
    if (it == PortBuilder::all_ports().end()) {
        return CommandFailure(ENODEV, "Port %s not found", port_name);
    }
    port_ = it->second;
    burst_ = bess::PacketBatch::kMaxBurst;

    if (arg.prefetch()) {
        prefetch_ = 1;
    }
    node_constraints_ = port_->GetNodePlacementConstraint();
    tid = RegisterTask((void *)(uintptr_t)qid_);
    if (tid == INVALID_TASK_ID)
        return CommandFailure(ENOMEM, "Context creation failed");

    int ret = port_->AcquireQueues(reinterpret_cast<const module *>(this),
            PACKET_DIR_INC, &qid_, 1);
    if (ret < 0) {
        return CommandFailure(-ret);
    }

    return CommandSuccess();
}

void BPQInc::DeInit() {
    if (port_) {
        port_->ReleaseQueues(reinterpret_cast<const module *>(this), PACKET_DIR_INC,
                &qid_, 1);
    }
}

std::string BPQInc::GetDesc() const {
    return bess::utils::Format("%s:%hhu/%s(%d)", port_->name().c_str(), qid_,
            port_->port_builder()->class_name().c_str(), (int)children_overload_);
}

struct task_result BPQInc::RunTask(Context *ctx, bess::PacketBatch *batch,
        void *arg) {
    if (children_overload_ > 0) {
        if (may_signal_overlay) {
            may_signal_overlay = false;
            LOG(INFO) << "EmitPacket for Overlay Generation OVER" << std::endl;
            EmitPacket(ctx, pause_pkt_sample, kPauseGeneratorGate);
        }
        return {.block = true, .packets = 0, .bits = 0};
    } else {
        if (may_signal_underload) {
            may_signal_underload = false;
            LOG(INFO) << "EmitPacket for Overlay Generation UNDER" << std::endl;
            EmitPacket(ctx, pause_pkt_sample, kPauseGeneratorGate);
        }

    }

    

    Port *p = port_;
    if (!p->conf().admin_up) {
        return {.block = true, .packets = 0, .bits = 0};
    }

    const queue_t qid = (queue_t)(uintptr_t)arg;

    uint64_t received_bytes = 0;

    const int burst = ACCESS_ONCE(burst_);
    const int pkt_overhead = 24;

    batch->set_cnt(p->RecvPackets(qid, batch->pkts(), burst));
    uint32_t cnt = batch->cnt();
    p->queue_stats[PACKET_DIR_INC][qid].requested_hist[burst]++;
    p->queue_stats[PACKET_DIR_INC][qid].actual_hist[cnt]++;
    p->queue_stats[PACKET_DIR_INC][qid].diff_hist[burst - cnt]++;
    if (cnt == 0) {
        return {.block = true, .packets = 0, .bits = 0};
    }

    // NOTE: we cannot skip this step since it might be used by scheduler.
    if (prefetch_) {
        for (uint32_t i = 0; i < cnt; i++) {
            received_bytes += batch->pkts()[i]->total_len();
            rte_prefetch0(batch->pkts()[i]->head_data());
        }
    } else {
        for (uint32_t i = 0; i < cnt; i++) {
            received_bytes += batch->pkts()[i]->total_len();
        }
    }

    if (!(p->GetFlags() & DRIVER_FLAG_SELF_INC_STATS)) {
        p->queue_stats[PACKET_DIR_INC][qid].packets += cnt;
        p->queue_stats[PACKET_DIR_INC][qid].bytes += received_bytes;
    }

    RunNextModule(ctx, batch);

    return {.block = false,
        .packets = cnt,
        .bits = (received_bytes + cnt * pkt_overhead) * 8};
}

CommandResponse BPQInc::CommandGetSummary(const bess::pb::EmptyArg &) {
  bkdrft::pb::BPQIncCommandGetSummaryResponse r;
  r.set_rx_pause_frame(rx_pause_frame_);
  r.set_tx_pause_frame(tx_pause_frame_);
  r.set_rx_resume_frame(rx_resume_frame_);
  r.set_tx_resume_frame(tx_resume_frame_);

  return CommandSuccess(r);
}

CommandResponse BPQInc::CommandClear(const bess::pb::EmptyArg &) {
  return CommandResponse();
}

ADD_MODULE(BPQInc, "bpq_inc",
        "receives packets from a port via a specific queue")
