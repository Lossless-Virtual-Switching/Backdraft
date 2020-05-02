// Copyright (c) 2014-2016, The Regents of the University of California.
// Copyright (c) 2016-2017, Nefeli Networks, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// * Neither the names of the copyright holders nor the names of their
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#ifndef BESS_MODULES_BKDRFTQUEUEOUT_H_
#define BESS_MODULES_BKDRFTQUEUEOUT_H_

#include <rte_hash_crc.h>

#include "../module.h"
#include "../pb/module_msg.pb.h"
#include "../port.h"

#include "../utils/cuckoo_map.h"

// TODO: alignas(8)
// TODO: random field
struct BKDRFTFlow {
	uint32_t ipsrc;
	uint32_t ipdsts;
	uint16_t portsrc;
	uint16_t portdst;
	uint32_t rnd;

	struct Hash {
		std::size_t operator()(const BKDRFTFlow &f) const {
			// TODO: In nat example it is more complecated
			return rte_hash_crc(&f, sizeof(uint64_t), 0);
		}
	};

	struct EqualTo {
		bool operator()(const BKDRFTFlow &lhs, const BKDRFTFlow &rhs) const {
			const union {
        BKDRFTFlow endpoint;
        uint64_t u64;
      } &left = {.endpoint = lhs}, &right = {.endpoint = rhs};

      return left.u64 == right.u64;
		}
	};
};

// static_assert(sizeof(BKDRFTFlow) == 16, "Size of bkdrtf flow not good");

class BKDRFTQueueOut final : public Module {
 public:
  static const gate_idx_t kNumOGates = 0;

  BKDRFTQueueOut() : Module(), port_(), qid_() {}

  CommandResponse Init(const bess::pb::BKDRFTQueueOutArg &arg);

  void DeInit() override;

  void ProcessBatch(Context *ctx, bess::PacketBatch *batch) override;

  std::string GetDesc() const override;

 private:
	using HashTable = bess::utils::CuckooMap<BKDRFTFlow, uint16_t, 
															BKDRFTFlow::Hash, BKDRFTFlow::EqualTo>;
  Port *port_;
  queue_t qid_;
	HashTable map_;
};

#endif  // BESS_MODULES_BKDRFTQUEUEOUT_H_