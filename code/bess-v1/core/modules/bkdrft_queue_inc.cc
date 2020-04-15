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

#include "bkdrft_queue_inc.h"

#include "../port.h"
#include "../utils/format.h"

const Commands BKDRFTQueueInc::cmds = {{"set_burst", "BKDRFTQueueIncCommandSetBurstArg",
	MODULE_CMD_FUNC(&BKDRFTQueueInc::CommandSetBurst),
	Command::THREAD_SAFE}};

CommandResponse BKDRFTQueueInc::Init(const bess::pb::BKDRFTQueueIncArg &arg) {
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
	if (qid_ == 0) {
		tid = RegisterTask((void *)(uintptr_t)qid_);
		if (tid == INVALID_TASK_ID)
			return CommandFailure(ENOMEM, "Context creation failed");
	}

	int ret = port_->AcquireQueues(reinterpret_cast<const module *>(this),
			PACKET_DIR_INC, &qid_, 1);
	if (ret < 0) {
		return CommandFailure(-ret);
	}

	return CommandSuccess();
}

void BKDRFTQueueInc::DeInit() {
	if (port_) {
		port_->ReleaseQueues(reinterpret_cast<const module *>(this), PACKET_DIR_INC,
				&qid_, 1);
	}
}

std::string BKDRFTQueueInc::GetDesc() const {
	return bess::utils::Format("%s:%hhu/%s", port_->name().c_str(), qid_,
			port_->port_builder()->class_name().c_str());
}

struct task_result BKDRFTQueueInc::RunTask(Context *ctx, bess::PacketBatch *batch,
		void *arg) {
	/*
	* Pool both ctrl queue and data queue (queue 1 for example).
	* if ctrl packet found update the pooling queue to the what
	* queue mentioned in the packet and pool the data queue until
	* it is empty. afterward pool the data queue and control queue.
	*/

	Port *p = port_;
	static bool ctrlQ = false;

	if (!p->conf().admin_up) {
		return {.block = true, .packets = 0, .bits = 0};
	}

	// TODO: qid was declared const, I changed it
	queue_t qid = (queue_t)(uintptr_t)arg;
	static queue_t dqid = 1;

	uint64_t received_bytes = 0;

	const int burst = ACCESS_ONCE(burst_);
	const int pkt_overhead = 24;

	if(ctrlQ == false) {
		batch->set_cnt(p->RecvPackets(qid, batch->pkts(), 1));
		uint32_t cnt = batch->cnt();
		p->queue_stats[PACKET_DIR_INC][qid].requested_hist[1]++;
		p->queue_stats[PACKET_DIR_INC][qid].actual_hist[cnt]++;
		p->queue_stats[PACKET_DIR_INC][qid].diff_hist[1 - cnt]++;

		//     if (cnt == 0) {
		//       return {.block = true, .packets = 0, .bits = 0};
		//     }

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

		if (cnt >= 1) {
			// bess:Packet ~ rte_mbuf
			uint8_t val = *(uint8_t *)(batch->pkts()[0]->head_data()) ;
			dqid = val;
			//LOG(INFO) << "BKDRFTQueueInc: " << (int)cnt << " should read queue " << ((unsigned int)dqid) << "\n";
			//LOG(INFO) << "BKDRFTQueueInc: " << "packet length: " << (int)batch->pkts()[0]->total_len() << "\n";
			bess::Packet::Free(batch->pkts()[0]); // free ctrl pkt
		}

		ctrlQ = true;
	}

	/// Data queue section
	received_bytes = 0;
	qid = dqid;
	batch->set_cnt(p->RecvPackets(qid, batch->pkts(), burst));
	uint32_t cnt2 = batch->cnt();
//	LOG(INFO) << "BKDRFTQueueInc: fetched: " << cnt2 << " from q: " << (int) qid << "\n";
	p->queue_stats[PACKET_DIR_INC][qid].requested_hist[burst]++;
	p->queue_stats[PACKET_DIR_INC][qid].actual_hist[cnt2]++;
	p->queue_stats[PACKET_DIR_INC][qid].diff_hist[burst - cnt2]++;
	// TODO: should revisit the case (cnt == 0)
	if (cnt2== 0) {
		ctrlQ = false;
		return {.block = true, .packets = 0, .bits = 0};
	} else {
		ctrlQ = true;
  }

	// NOTE: we cannot skip this step since it might be used by scheduler.
	if (prefetch_) {
		for (uint32_t i = 0; i < cnt2; i++) {
			received_bytes += batch->pkts()[i]->total_len();
			rte_prefetch0(batch->pkts()[i]->head_data());
		}
	} else {
		for (uint32_t i = 0; i < cnt2; i++) {
			received_bytes += batch->pkts()[i]->total_len();
		}
	}

	if (!(p->GetFlags() & DRIVER_FLAG_SELF_INC_STATS)) {
		p->queue_stats[PACKET_DIR_INC][qid].packets += cnt2;
		p->queue_stats[PACKET_DIR_INC][qid].bytes += received_bytes;
	}

	RunNextModule(ctx, batch);

	return {.block = false,
		.packets = cnt2,
		.bits = (received_bytes + cnt2 * pkt_overhead) * 8};
}

CommandResponse BKDRFTQueueInc::CommandSetBurst(
		const bess::pb::BKDRFTQueueIncCommandSetBurstArg &arg) {
	if (arg.burst() > bess::PacketBatch::kMaxBurst) {
		return CommandFailure(EINVAL, "burst size must be [0,%zu]",
				bess::PacketBatch::kMaxBurst);
	} else {
		burst_ = arg.burst();
		return CommandSuccess();
	}
}

ADD_MODULE(BKDRFTQueueInc, "bkdrft_queue_inc",
		"receives packets from a port via a specific queue")
