#include "bkdrft_queue_inc.h"

#include "../port.h"
#include "../utils/time.h"
#include "../utils/format.h"
#include "../utils/endian.h"
#include "../utils/bkdrft.h"
#include "../utils/bkdrft_sw_drop_control.h"

using bess::utils::be32_t;
using bess::utils::be16_t;
using namespace bess::bkdrft;

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
	if (qid_ == BKDRFT_CTRL_QUEUE) {
		tid = RegisterTask((void *)(uintptr_t)qid_);
		if (tid == INVALID_TASK_ID)
			return CommandFailure(ENOMEM, "Context creation failed");
	}

	int ret = port_->AcquireQueues(reinterpret_cast<const module *>(this),
			PACKET_DIR_INC, nullptr, 0);
	if (ret < 0) {
		return CommandFailure(-ret);
	}

	// check if module backpressure is active
	if (arg.backpressure()) {
		backpressure_ = 1;
	} else {
		backpressure_ = 0;	
	}

	// initialize q_status
	for (int i = 0; i < MAX_NUMBER_OF_QUEUE; i++)
		q_status_[i] = (queue_pause_status) {
			.until = 0,
			.flow = {
				.addr_src = (be32_t)0,
				.addr_dst = (be32_t)0,
				.port_src = (be16_t)0,
				.port_dst = (be16_t)0,
				.protocol = 0,
			},
		};

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
	Port *p = port_;

	if (!p->conf().admin_up) {
		return {.block = true, .packets = 0, .bits = 0};
	}

	const queue_t qid = (queue_t)(uintptr_t)arg;
	queue_t dqid = 1;
	uint32_t cnt2;
	bess::Packet * ctrlpkt;
	ctrl_pkt *ptr;
	uint32_t nb_pkts;
	uint64_t wait_until;
	uint64_t received_bytes = 0;

	const int burst = ACCESS_ONCE(burst_);
	const int pkt_overhead = 24;

	const int cnt = p->RecvPackets(qid, &ctrlpkt, 1);
	p->queue_stats[PACKET_DIR_INC][qid].requested_hist[1]++;
	p->queue_stats[PACKET_DIR_INC][qid].actual_hist[cnt]++;
	p->queue_stats[PACKET_DIR_INC][qid].diff_hist[1 - cnt]++;

	if (cnt == 0) {
		// There is no ctrl pkt
		return {.block = true, .packets = 0, .bits = 0};
	}

	// NOTE: we cannot skip this step since it might be used by scheduler.
	if (prefetch_) {
		received_bytes += ctrlpkt->total_len();
		rte_prefetch0(ctrlpkt->head_data());
	} else {
		received_bytes += ctrlpkt->total_len();
	}

	if (!(p->GetFlags() & DRIVER_FLAG_SELF_INC_STATS)) {
		p->queue_stats[PACKET_DIR_INC][qid].packets += cnt;
		p->queue_stats[PACKET_DIR_INC][qid].bytes += received_bytes;
	}

	// bess:Packet ~ rte_mbuf
	ptr = static_cast<ctrl_pkt *>(ctrlpkt->head_data());
	dqid = ptr->q;
	nb_pkts = ptr->nb_pkts;
	bess::Packet::Free(ctrlpkt); // free ctrl pkt

	/// Data queue section
	
	if (backpressure_) {
		// check if data queue is paused
		uint64_t now;
		now = rdtsc();
		now  = tsc_to_ns(now);  // nano seconds

		if (q_status_[dqid].until > now) {
			LOG(INFO) << "Queue " << dqid << " is paused (" << q_status_[dqid].until << " < " << now << ").\n";
			// this queue is paused
			return {.block = true, .packets = 0, .bits = 0};
		}
		// TODO: we can directly check BKDRFTSwDpCtrl (we have flow detail in q_status)
		// Is this good?
		LOG(INFO) << "Outside the if\n";
	}

	/*
	 * We are here because we know there is (nb_pkts) of 
	 * data waiting in the queue so try to get them
	 * (Do not ignore ctrl pkts)
	 */

	batch->set_cnt(p->RecvPackets(dqid, batch->pkts(), burst));
	cnt2 = batch->cnt();
	p->queue_stats[PACKET_DIR_INC][dqid].requested_hist[burst]++;
	p->queue_stats[PACKET_DIR_INC][dqid].actual_hist[cnt2]++;
	p->queue_stats[PACKET_DIR_INC][dqid].diff_hist[burst - cnt2]++;

	if (cnt2 < nb_pkts) {
		// TODO: what to do?
	}

	// NOTE: we cannot skip this step since it might be used by scheduler.
	received_bytes = 0;
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
		p->queue_stats[PACKET_DIR_INC][dqid].packets += cnt2;
		p->queue_stats[PACKET_DIR_INC][dqid].bytes += received_bytes;
	}

	if (cnt2 > 0) {
		if (backpressure_) {
			// assume all the batch belongs to the same flow
			bess::bkdrft::Flow flow = PacketToFlow(*(batch->pkts()[0]));

			// check if the data queue is paused
			// TODO: move this in class attributes
			BKDRFTSwDpCtrl &dropMan =
				bess::bkdrft::BKDRFTSwDpCtrl::GetInstance();
			wait_until = dropMan.GetFlowStatus(flow);
			q_status_[dqid].until = wait_until;
			q_status_[dqid].flow = flow;

			// Note: We have fetched the packets so we send it down the
			// path. but if it is paused we remeber not to fetch this
			// queue for a while.
		}

		RunNextModule(ctx, batch);

		return {.block = false,
			.packets = cnt2,
			.bits = (received_bytes + cnt2 * pkt_overhead) * 8};
	} else {
		// There is no  data pkts
		LOG(INFO) << "No data in recv queue " << (int)dqid << "\n";
		return {.block = true, .packets = 0, .bits = 0};
	}
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
