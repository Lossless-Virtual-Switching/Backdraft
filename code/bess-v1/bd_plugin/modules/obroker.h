#ifndef BKDRFT_MODULES_OBROKER_H_
#define BKDRFT_MODULES_OBROKER_H_

#include "module.h"
#include "pb/bkdrft_module_msg.pb.h"
#include "utils/endian.h"

using bess::utils::be32_t;
using ParsedPrefix = std::tuple<int, std::string, be32_t>;

class OBroker final : public Module {
 public:
  static const gate_idx_t kNumOGates = MAX_GATES;

  static const Commands cmds;

  static std::map<std::string, OBroker*> all_brokers_;

  OBroker() : Module(), lpm_(), default_gate_() {
    max_allowed_workers_ = Worker::kMaxWorkers;
    propagate_workers_ = false;
  }

  CommandResponse Init(const bkdrft::pb::OBrokerArg &arg);

  void broker(be32_t addr);

  void DeInit() override;

  void ProcessBatch(Context *ctx, bess::PacketBatch *batch) override;

  CommandResponse CommandAdd(const bkdrft::pb::OBrokerCommandAddArg &arg);
  CommandResponse CommandDelete(const bkdrft::pb::OBrokerCommandDeleteArg &arg);
  CommandResponse CommandClear(const bess::pb::EmptyArg &arg);

 private:
  struct rte_lpm *lpm_;
  gate_idx_t default_gate_;
  ParsedPrefix ParseIpv4Prefix(const std::string &prefix, uint64_t prefix_len);
};

#endif  // BDRFT_MODULES_OBROKER_H_
