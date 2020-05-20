#ifndef BESS_MODULES_BKDRFTQUEUEOUT_H_
#define BESS_MODULES_BKDRFTQUEUEOUT_H_

#include <rte_hash_crc.h>

#include "../module.h"
#include "../pb/module_msg.pb.h"
#include "../port.h"
#include "../packet_pool.h"

#include "../utils/cuckoo_map.h"
#include "../utils/flow.h"
#include "../utils/lock_less_queue.h"


class BKDRFTQueueOut final : public Module {
	public:
		static const gate_idx_t kNumOGates = 0;

		BKDRFTQueueOut() : Module(), port_(), qid_() {}

		CommandResponse Init(const bess::pb::BKDRFTQueueOutArg &arg);

		void DeInit() override;

		void ProcessBatch(Context *ctx, bess::PacketBatch *batch) override;

		std::string GetDesc() const override;

	private:
		using Flow = bess::bkdrft::Flow;

		using bufferHashTable = bess::utils::CuckooMap<Flow,
		      bess::utils::LockLessQueue<bess::Packet *> *,
		      Flow::Hash, Flow::EqualTo>;

		void BufferBatch(bess::bkdrft::Flow &flow, bess::PacketBatch *batch,
				uint16_t sent_pkt);

		void TrySendBuffer();

		uint16_t SendCtrlPkt(Port *p, queue_t qid, uint16_t sent_pkts, uint32_t sent_bytes);

		void UpdatePortStats(queue_t qid, uint16_t sent_pkts, uint16_t droped,
				bess::PacketBatch *batch);

		Port *port_;
		queue_t qid_;
		bool backpressure_ = 0;
		bool log_ = 0;
		bufferHashTable buffers_;
		uint64_t count_packets_in_buffer_;
		uint64_t bytes_in_buffer_;
		bess::DpdkPacketPool *ctrl_pool_;
};

#endif  // BESS_MODULES_BKDRFTQUEUEOUT_H_
