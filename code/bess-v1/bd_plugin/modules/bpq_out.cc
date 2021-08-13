#include "bpq_out.h"

#include "port.h"
#include "utils/format.h"
#include "utils/mcslock.h"
#include "utils/spin_lock.h"

#define DEFAULT_QUEUE_SIZE 256

CommandResponse BPQOut::Init(const bkdrft::pb::BPQOutArg &arg) {
    const char *port_name;
    int ret;
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
    node_constraints_ = port_->GetNodePlacementConstraint();
    ret = port_->AcquireQueues(reinterpret_cast<const module *>(this),
            PACKET_DIR_OUT, &qid_, 1);
    if (ret < 0) {
        return CommandFailure(-ret);
    }
    burst_ = bess::PacketBatch::kMaxBurst;
    // Register task
    task_id_t tid;
    tid = RegisterTask(nullptr);
    if (tid == INVALID_TASK_ID) {
        return CommandFailure(ENOMEM, "Task creation failed");
    }
    // Init queue_
    llring *new_queue;
    int slots = DEFAULT_QUEUE_SIZE;
    int bytes = llring_bytes_with_slots(slots);
    new_queue =
        static_cast<llring *>(std::aligned_alloc(alignof(llring), bytes));
    if (!new_queue) {
        return CommandFailure(ENOMEM);
    }
    ret = llring_init(new_queue, slots, 0, 0);
    if (ret) {
        std::free(new_queue);
        return CommandFailure(EINVAL);
    }
    queue_ = new_queue;
    size_ = slots;
    // Adjust Water Levels
    high_water_ = static_cast<uint64_t>(size_ * kHighWaterRatio);
    low_water_ = static_cast<uint64_t>(size_ * kLowWaterRatio);
    return CommandSuccess();
}

void BPQOut::DeInit() {
    if (port_) {
        port_->ReleaseQueues(reinterpret_cast<const module *>(this), PACKET_DIR_OUT,
                &qid_, 1);
    }
}

std::string BPQOut::GetDesc() const {
    return bess::utils::Format("%s:%hhu/%s(OL:%d)", port_->name().c_str(), qid_,
            port_->port_builder()->class_name().c_str(), overload_);
}

void BPQOut::Buffer(bess::Packet **pkts, int cnt)
{
    int queued = llring_mp_enqueue_burst(queue_, (void **)pkts, cnt);
    if (queued < cnt) {
        // Drop packets!
        bess::Packet::Free(pkts + queued, cnt - queued);
        Port *p = port_;
        if (!(p->GetFlags() & DRIVER_FLAG_SELF_OUT_STATS)) {
            p->queue_stats[PACKET_DIR_OUT][qid_].dropped += (cnt - queued);
        }
    }
    if (llring_count(queue_) > high_water_) {
        // LOG(INFO) << "Signal overload bpq_out: " << std::endl;
        SignalOverloadBP();
    }
    // size_t qlen = llring_count(queue_);
    // if (qlen > DEFAULT_QUEUE_SIZE) {
    //     LOG(ERROR) << "queue_ size is : " << qlen << std::endl;
    // }
}

void BPQOut::ProcessBatch(Context *, bess::PacketBatch *batch) {
    Port *p = port_;
    const queue_t qid = qid_;
    uint64_t sent_bytes = 0;
    int sent_pkts = 0;
    int cnt = batch->cnt();
    if (!p->conf().admin_up) {
        return;
    }
    if (llring_count(queue_) > 0) {
        // There are some packets already in the llring
        Buffer(batch->pkts(), batch->cnt());
        return;
    }
    sent_pkts = p->SendPackets(qid, batch->pkts(), cnt);
    if (!(p->GetFlags() & DRIVER_FLAG_SELF_OUT_STATS)) {
        const packet_dir_t dir = PACKET_DIR_OUT;
        for (int i = 0; i < sent_pkts; i++) {
            sent_bytes += batch->pkts()[i]->total_len();
        }
        p->queue_stats[dir][qid].packets += sent_pkts;
        p->queue_stats[dir][qid].bytes += sent_bytes;
    }
    if (sent_pkts < cnt) {
        // bess::Packet::Free(batch->pkts() + sent_pkts, batch->cnt() - sent_pkts);
        Buffer(batch->pkts() + sent_pkts,  cnt - sent_pkts);
    }
}

#define shift_array_left(arr, cnt, size) { \
    for(int i = cnt; i < size; i++) {      \
        arr[i-cnt] = arr[i];               \
    }                                      \
}

#define unused __attribute__((unused))
struct task_result BPQOut::RunTask(Context *, bess::PacketBatch *, void *) {
    static SpinLock lock;
    static bess::Packet *mbufs[bess::PacketBatch::kMaxBurst] = {};
    static int cnt_mbufs = 0;

    lock.lock();

    Port *p = port_;
    const queue_t qid = qid_;

    int sent_pkts;
    uint32_t total_sent_packets = 0;
    size_t total_sent_bytes = 0;
    const int pkt_overhead = 24;

    while (true) {
        if (cnt_mbufs > 0) {
            // If there are some packets in the mbufs ...
            sent_pkts = p->SendPackets(qid, (bess::Packet **)&mbufs, cnt_mbufs);

            for (int i = 0; i < sent_pkts; i++) {
                total_sent_bytes += mbufs[i]->total_len();
            }
            total_sent_packets += sent_pkts;

            if (sent_pkts < cnt_mbufs) {
                // EXIT
                // shift_array_left(mbufs, sent_pkts, cnt_mbufs);
                for(int i = sent_pkts; i < cnt_mbufs; i++) {
                    mbufs[i - sent_pkts] = mbufs[i];
                }
                cnt_mbufs -= sent_pkts;

                if (!(p->GetFlags() & DRIVER_FLAG_SELF_OUT_STATS)) {
                    const packet_dir_t dir = PACKET_DIR_OUT;
                    p->queue_stats[dir][qid].packets += total_sent_packets;
                    p->queue_stats[dir][qid].bytes += total_sent_bytes;
                }
                lock.unlock();
                return {.block = false,
                    .packets = total_sent_packets,
                    .bits = (total_sent_bytes + total_sent_packets * pkt_overhead) * 8};
            }
            cnt_mbufs = 0;
        }

        // when dequeueing there should be no packet in mbufs
        // otherwise they are overwritten!
        uint32_t cnt = llring_mc_dequeue_burst(queue_, (void **)mbufs, burst_);
        if (cnt == 0) {
            // EXIT
            // There are no packets left in the queue_
            if (!(p->GetFlags() & DRIVER_FLAG_SELF_OUT_STATS)) {
                const packet_dir_t dir = PACKET_DIR_OUT;
                p->queue_stats[dir][qid].packets += total_sent_packets;
                p->queue_stats[dir][qid].bytes += total_sent_bytes;
            }
            lock.unlock();
            return {.block = false,
                .packets = total_sent_packets,
                .bits = (total_sent_bytes + total_sent_packets * pkt_overhead) * 8};
        }
        cnt_mbufs = cnt;

        //
        if (llring_count(queue_) < low_water_) {
            SignalUnderloadBP();
        }
    }
}

ADD_MODULE(BPQOut, "bpq_out",
        "sends packets to a port via a specific queue")
