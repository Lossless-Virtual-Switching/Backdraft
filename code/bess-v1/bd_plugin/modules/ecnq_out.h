#ifndef BD_MODULES_ECNQ_OUT_H_
#define BD_MODULES_ECNQ_OUT_H_

#include "module.h"
#include "port.h"

#include "kmod/llring.h"
#include "utils/spin_lock.h"
#include "pb/bkdrft_module_msg.pb.h"

class ECNQOut final : public Module {
public:
    static const gate_idx_t kNumOGates = 0;

    ECNQOut() :
        Module(),
        port_(),
        qid_(),
        low_water_(),
        high_water_(),
        queue_(),
        size_(),
        burst_()
    {
        is_task_ = true;
        // propagate_workers_ = false;
    }

    CommandResponse Init(const bkdrft::pb::ECNQOutArg &arg);

    void DeInit() override;

    void ProcessBatch(Context *ctx, bess::PacketBatch *batch) override;
    struct task_result RunTask(Context *ctx, bess::PacketBatch *batch,
            void *arg) override;

    std::string GetDesc() const override;

private:
    void Buffer(bess::Packet **pkts, int cnt);

private:
    const double kHighWaterRatio = 0.00;
    const double kLowWaterRatio = 0.00;

    Port *port_;
    queue_t qid_;
    size_t low_water_;
    size_t high_water_;
    struct llring *queue_;
    size_t size_;
    int burst_;

    int cnt_mbufs_;
    bess::Packet *mbufs_[bess::PacketBatch::kMaxBurst];

};

#endif
