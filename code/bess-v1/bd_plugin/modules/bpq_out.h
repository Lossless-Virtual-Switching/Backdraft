#ifndef BD_MODULES_BPQ_OUT_H_
#define BD_MODULES_BPQ_OUT_H_

#include "module.h"
#include "port.h"

#include "kmod/llring.h"
#include "utils/spin_lock.h"
#include "pb/bkdrft_module_msg.pb.h"

class BPQOut final : public Module {
public:
    static const Commands cmds;
    static const gate_idx_t kNumOGates = 0;
    // static const gate_idx_t kNumIGates = 32;

    BPQOut() :
        Module(),
        port_(),
        qid_(),
        low_water_(),
        high_water_(),
        queue_(),
        size_(),
        burst_(),
	rx_pause_(0),
	tx_pause_(0),
	rx_resume_(0),
	tx_resume_(0)
    {
        is_task_ = true;
        max_allowed_workers_ = Worker::kMaxWorkers;
        // propagate_workers_ = false;
    }

    CommandResponse Init(const bkdrft::pb::BPQOutArg &arg);
    CommandResponse CommandGetSummary(const bess::pb::EmptyArg &arg);
    CommandResponse CommandClear(const bess::pb::EmptyArg &arg);


    void DeInit() override;

    void ProcessBatch(Context *ctx, bess::PacketBatch *batch) override;
    struct task_result RunTask(Context *ctx, bess::PacketBatch *batch,
            void *arg) override;

    std::string GetDesc() const override;

private:
    void Buffer(bess::Packet **pkts, int cnt);

private:
    const double kHighWaterRatio = 0.70;
    const double kLowWaterRatio = 0.50;

    Port *port_;
    queue_t qid_;
    size_t low_water_;
    size_t high_water_;
    struct llring *queue_;
    size_t size_;
    int burst_; 

    int cnt_mbufs_;
    // Spinglock is not necessary now
    // SpinLock *lock_;
    bess::Packet *mbufs_[bess::PacketBatch::kMaxBurst];

public:
//-------
uint64_t rx_pause_;
uint64_t tx_pause_;
uint64_t rx_resume_;
uint64_t tx_resume_;

};

#endif
