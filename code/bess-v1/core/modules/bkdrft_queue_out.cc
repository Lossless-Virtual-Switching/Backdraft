#include "bkdrft_queue_out.h"

#include <rte_ethdev.h>

#include "../port.h"
#include "../utils/format.h"
#include "../utils/bkdrft.h"
#include "../utils/bkdrft_sw_drop_control.h"
#include "../utils/flow.h"

#define MAX_PAUSE_DURATION (20) // (ns)
#define PAUSE_DURATION_GAMMA (2) // (ns)
#define MAX_BUFFER_SIZE (8257536) // 64KB * 126 (bytes) 

CommandResponse BKDRFTQueueOut::Init(const bess::pb::BKDRFTQueueOutArg &arg) {
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
			PACKET_DIR_OUT, nullptr, 0);
	if (ret < 0) {
		return CommandFailure(-ret);
	}

	if (arg.backpressure()) {
		backpressure_ = 1;
	} else {
		backpressure_ = 0;
	}

	if (arg.log()) {
		log_ = 1;
		LOG(INFO) << "[BKDRFTQueueOut] backpressure: " << backpressure_ << "\n";
	} else {
		log_ = 0;
	}

	count_packets_in_buffer_ = 0;
	bytes_in_buffer_ = 0;

	return CommandSuccess();
}

void BKDRFTQueueOut::DeInit() {
	if (port_) {
		port_->ReleaseQueues(reinterpret_cast<const module *>(this), PACKET_DIR_OUT,
				nullptr, 0);
	}
	// free buffers
	const int max_queue_capacity = 256;
	auto iter = buffers_.begin();
	int size = 0;
	bess::Packet *pkt[max_queue_capacity];
	for (; iter != buffers_.end(); iter++) {
		size = iter->second->Pop(pkt, max_queue_capacity);
		bess::Packet::Free(pkt, size);  // free packets
		delete(iter->second);  // free lock less queue
	}
}

std::string BKDRFTQueueOut::GetDesc() const {
	return bess::utils::Format("%s:%hhu/%s(bp:%d)", port_->name().c_str(), qid_,
			port_->port_builder()->class_name().c_str(), backpressure_);
}

void BKDRFTQueueOut::BufferBatch(bess::bkdrft::Flow &flow,
		bess::PacketBatch *batch, uint16_t sent_pkts)
{
	bess::Packet **pkts = batch->pkts();
	uint16_t remaining_pkts = batch->cnt() - sent_pkts;
	if (bytes_in_buffer_ >= MAX_BUFFER_SIZE) {
		LOG(INFO) << "Maximum buffer size reached!\n";
		bess::Packet::Free(pkts + sent_pkts, remaining_pkts);
		return;
	}
	int rc = 0;
	auto entry = buffers_.Find(flow);
	uint64_t remaining_bytes = 0;
	uint16_t i;
	if (entry == nullptr) {
		// allocate new llqueue
		auto llqueue = new bess::utils::LockLessQueue<bess::Packet *>();
		rc = llqueue->Push(pkts + sent_pkts, remaining_pkts);
		buffers_.Insert(flow, llqueue);
	} else {
		rc = entry->second->Push(pkts + sent_pkts, remaining_pkts);
	}

	if (rc == -2) {
		LOG(INFO) << "[BKDRFTQueueOut] Enqueue to buffer failed\n";
	} else {
		for (i = sent_pkts; i < batch->cnt(); i++)
			remaining_bytes += pkts[i]->total_len();
		count_packets_in_buffer_ += remaining_pkts;
		bytes_in_buffer_ += remaining_bytes;
	}
}

void BKDRFTQueueOut::TrySendBuffer()
{
	Port *p = port_;
	const uint16_t qid = 1; // TODO: use RSS for this
	uint16_t max_batch_size = 32; // BQL can be used here
	uint16_t sent_pkts = 0;
	uint32_t sent_bytes = 0;
	auto iter = buffers_.begin();
	// std::list<bess::Packet *>::iterator pkt, batchBegin, batchEnd, lastPkt;
	bess::Packet *pkts[max_batch_size];
	int k = 0;
	int i;
	bool run  = true;
	// TODO: The order of flows trying to send is important
	for (; run && iter != buffers_.end(); iter++) {
		// while there is pkts in for this flow buffer
		k = iter->second->Pop(pkts, max_batch_size);
		LOG(INFO) << "[BKDRFTQueueOut] retrying sending buffer\n";
		sent_pkts = p->SendPackets(qid, pkts, k);

		sent_bytes = 0;
		for (i = 0; i < sent_pkts; i++)
			sent_bytes += pkts[i]->total_len();

		// Drop failed packets
		bess::Packet::Free(pkts + sent_pkts, k - sent_pkts);

		// TODO: update!
		// UpdatePortStats(qid, sent_pkts, 0, &batch);
		if (!(p->GetFlags() & DRIVER_FLAG_SELF_OUT_STATS)) {
			const packet_dir_t dir = PACKET_DIR_OUT;

			p->queue_stats[dir][qid].packets += sent_pkts;
			// TODO: each packet is retried once
			p->queue_stats[dir][qid].dropped += k - sent_pkts;
			p->queue_stats[dir][qid].bytes += sent_bytes;
		}
		count_packets_in_buffer_ -= sent_pkts;
		bytes_in_buffer_ -= sent_bytes;

		if (SendCtrlPkt(p, qid, sent_pkts, sent_bytes) < 1) {
			// ctrl pkt failed
			// run = false;
			break;
		}

		if (sent_pkts < k) {
			// some packets in batch was not sent
			// run = false;
			break;
		}
	}
}

inline uint16_t
BKDRFTQueueOut::SendCtrlPkt(Port *p, queue_t qid, uint16_t sent_pkts,
	uint32_t sent_bytes)
{
	using bess::bkdrft::ctrl_pkt;
	bess::Packet *ctrlpkt;
	ctrl_pkt *buf_ptr;
	uint16_t sent_ctrl_pkts = 0;
	// char *ptr;
	// uint64_t size_ctrl_pkt = sizeof(ctrl_pkt);

	ctrlpkt = current_worker.packet_pool()->Alloc();
	if (ctrlpkt != nullptr) {
		// ctrl_pkt was successfully allocated
		buf_ptr = reinterpret_cast<ctrl_pkt *>
				(ctrlpkt->append(128));
		// ptr = ctrlpkt->buffer<char *>() + SNBUF_HEADROOM;
		// buf_ptr = reinterpret_cast<ctrl_pkt *>(ptr);
		buf_ptr->q = qid;
		buf_ptr->nb_pkts = sent_pkts;
		buf_ptr->bytes = sent_bytes;
		// ctrlpkt->set_total_len(size_ctrl_pkt);
		// ctrlpkt->set_data_len(size_ctrl_pkt);

		// ctrlpkt->CheckSanity();

		// send a ctrl pkt
		sent_ctrl_pkts = p->SendPackets(BKDRFT_CTRL_QUEUE, &ctrlpkt, 1);
		// sent_ctrl_pkts = p->SendPackets(p->num_tx_queues() - 1, &ctrlpkt, 1);

		// update stats
		if (!(p->GetFlags() & DRIVER_FLAG_SELF_OUT_STATS)) {
			const packet_dir_t dir = PACKET_DIR_OUT;

			p->queue_stats[dir][BKDRFT_CTRL_QUEUE].packets += sent_ctrl_pkts;
			p->queue_stats[dir][BKDRFT_CTRL_QUEUE].dropped += 1 - sent_ctrl_pkts;
			p->queue_stats[dir][BKDRFT_CTRL_QUEUE].bytes += ctrlpkt->total_len();
		}

		if (sent_ctrl_pkts == 0) {
			// TODO: what should happen if ctrl_pkt fails
			bess::Packet::Free(ctrlpkt);
			LOG(INFO) << "ctrl packet failed\n";
		} else if (log_) {
			LOG(INFO) << "Send ctrl packet: cqid: " << BKDRFT_CTRL_QUEUE
				<< " dqid: " << (int) qid << "sent packets: "
				<< sent_ctrl_pkts << "\n";
		}
		return sent_ctrl_pkts;
	} else {
		// TODO: What should happend if ctrl_pkt fails
		LOG(INFO) << "[BKDRFTQueueOut][SendCtrlPkt] ctrl pkt alloc failed \n";
		return 0;
	}
}

inline void BKDRFTQueueOut::UpdatePortStats(queue_t qid, uint16_t sent_pkts,
		uint16_t droped, bess::PacketBatch *batch)
{
	Port *p = port_;
	if (!(p->GetFlags() & DRIVER_FLAG_SELF_OUT_STATS)) {
		Port *p = port_;
		uint64_t sent_bytes = 0;
		const packet_dir_t dir = PACKET_DIR_OUT;

		for (int i = 0; i < sent_pkts; i++) {
			sent_bytes += batch->pkts()[i]->total_len();
		}

		p->queue_stats[dir][qid].packets += sent_pkts;
		p->queue_stats[dir][qid].dropped += droped;
		p->queue_stats[dir][qid].bytes += sent_bytes;
	}
}

void BKDRFTQueueOut::ProcessBatch(Context *, bess::PacketBatch *batch) {
	using namespace bess::bkdrft;

	Port *p = port_;
	//   const queue_t qid = _qid;
	const queue_t qid = 1; // TODO: decide which queue to put data on
	int sent_pkts = 0;
	uint32_t sent_bytes = 0;
	uint64_t remaining_bytes = 0;
	uint64_t duration;
	uint16_t i;
	uint64_t pps;
	const int cnt = batch->cnt();
	bess::Packet **pkts = batch->pkts();

	if (unlikely(cnt < 1)) {
		LOG(INFO) << "Empty batch !!!\n";
		return;
	}

	if (p->conf().admin_up) {
		sent_pkts = p->SendPackets(qid, batch->pkts(), cnt);
	}

	// Send CtrlPkt as soon as possible, (other side will not start processing until it has been received
	if (sent_pkts > 0) {
		// if some data pkts was sent
		if (log_) {
			LOG(INFO) << "[BKDRFTQueueOut] should send ctrl pkt\n";
		}
		sent_bytes = 0;
		for (i = 0; i < sent_pkts; i++)
			sent_bytes += pkts[i]->total_len();
		SendCtrlPkt(p, qid, sent_pkts, sent_bytes);
	}

	if (backpressure_) {
		UpdatePortStats(qid, sent_pkts, 0, batch);
		if (sent_pkts < cnt) {
			// some packets was not sent
			// do not drop packets
			// bess::Packet::Free(batch->pkts() + sent_pkts, batch->cnt() - sent_pkts);

			// TODO: Assumming all packets in a batch are for one 
			// flow. Is this assumption correct?
			Flow flow = PacketToFlow(*(pkts[0]));

			// pause the incoming queue of the flow
			// TODO: get line rate some how
			pps = p->rate_.pps[PACKET_DIR_OUT][qid];
			if (pps == 0) {
				duration = MAX_PAUSE_DURATION;
			} else {
			  // FIXME: On a pps rate this part is wrong, I need to provide
				// bps equivalent as well but for now it just doesn't work for
				// some reason.
				
				// for (i = sent_pkts; i < cnt; i++)
				// 	remaining_bytes += pkts[i]->total_len();
				// duration = (remaining_bytes << 3) / bps; // (ns)
				// duration += PAUSE_DURATION_GAMMA;
				// if (duration > MAX_PAUSE_DURATION)
				// 	duration = MAX_PAUSE_DURATION;
				duration = MAX_PAUSE_DURATION;
			}
			// TODO: set this value as class attribute
			BKDRFTSwDpCtrl &dropMan = BKDRFTSwDpCtrl::GetInstance();
			dropMan.PauseFlow(duration, flow);
			LOG(INFO) << "pause duration: " << duration
				<< " remaining byte: " << remaining_bytes
				<< " pps: " << pps << "\n";

			// buffer packets
			BufferBatch(flow, batch, sent_pkts);
		} else {
			// all packets was sent
			if (count_packets_in_buffer_ > 0) {
				// all packets was sent now try others
				LOG(INFO) << "Count packets in buffer: " << count_packets_in_buffer_ << "\n";
				TrySendBuffer();
			}
		}
	} else {
		// no backpressure
		// drop packets
		if (sent_pkts < cnt)
			bess::Packet::Free(pkts + sent_pkts, cnt - sent_pkts);
		UpdatePortStats(qid, sent_pkts, cnt - sent_pkts, batch);
	}
}

ADD_MODULE(BKDRFTQueueOut, "bkdrft_queue_out",
		"sends packets to a port via a specific queue")
