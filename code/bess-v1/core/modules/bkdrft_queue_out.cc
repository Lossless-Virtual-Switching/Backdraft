#include "../pb/bkdrft_msg.pb.h"
#include "../port.h"
#include "../utils/bkdrft.h"
#include "../utils/bkdrft_overlay_ctrl.h"
#include "../utils/bkdrft_sw_drop_control.h"
#include "../utils/flow.h"
#include "../utils/format.h"
#include "bkdrft_queue_out.h"
#include <rte_ethdev.h>

#define MIN_PAUSE_DURATION (1000)
#define MAX_PAUSE_DURATION (5000000) // (31250L) // (93750)// (125000)// (62500L)
// (ns)// 93750// 46875
#define PAUSE_DURATION_GAMMA (2)  // (ns)
#define MAX_BUFFER_SIZE (4194304) // (5242880) //  (1048576) // 1 mega byte
#define LOW_WATER_MARK (5449973)  // ~ MAX_BUFFER_SIZE * 2/3
#define ONE_SEC (1000000000L)     // (ns)
#define PACKET_HEADERS_SIZE (64)

using bess::bkdrft::Flow;
using bess::bkdrft::total_len;

#ifdef HOLB
#pragma message "Compiling with HOLB flag"
// =================================================
// This buffer is useful when per flow queueing is active
// we buffer each packet in its seperate queue
bess::utils::CuckooMap<Flow, std::vector<bess::Packet *> *, Flow::Hash,
                       Flow::EqualTo>
    BKDRFTQueueOut::buffers_;

// The limmited_buffers_ are used for buffering packets when
// per flow queueing is not active.
std::vector<bess::Packet *> BKDRFTQueueOut::limited_buffers_[MAX_QUEUES];

uint64_t BKDRFTQueueOut::count_packets_in_buffer_ = 0;
uint64_t BKDRFTQueueOut::bytes_in_buffer_ = 0;
// =================================================
#endif

static inline void ecnMark(bess::Packet *pkt) {
  using bess::utils::Ethernet;
  using bess::utils::Ipv4;

  // TODO: checks to see if protocol is ip
  Ethernet *eth = pkt->head_data<Ethernet *>();
  Ipv4 *ip = reinterpret_cast<Ipv4 *>(eth + 1);

  if (ip->protocol == Ipv4::Proto::kTcp)
    ip->type_of_service = 0x03;
}

/*
 * Define BKDRFTQueueOut Commands
 * */
const Commands BKDRFTQueueOut::cmds = {
  {"get_pause_calls", "EmptyArg",
    MODULE_CMD_FUNC(&BKDRFTQueueOut::CommandPauseCalls), Command::THREAD_SAFE},
  {"get_ctrl_msg_tp", "EmptyArg",
    MODULE_CMD_FUNC(&BKDRFTQueueOut::CommandGetCtrlMsgTp), Command::THREAD_SAFE},
  {"get_overlay_tp", "EmptyArg",
    MODULE_CMD_FUNC(&BKDRFTQueueOut::CommandGetOverlayTp), Command::THREAD_SAFE},
  {"set_overlay_threshold", "BKDRFTQueueOutCommandSetOverlayThresholdArg",
    MODULE_CMD_FUNC(&BKDRFTQueueOut::CommandSetOverlayThreshold),
    Command::THREAD_UNSAFE},
  {"set_backpressure_threshold",
    "BKDRFTQueueOutCommandSetBackpressureThresholdArg",
    MODULE_CMD_FUNC(&BKDRFTQueueOut::CommandSetBackpressureThreshold),
    Command::THREAD_UNSAFE}
};

CommandResponse BKDRFTQueueOut::Init(const bess::pb::BKDRFTQueueOutArg &arg) {
  const char *port_name;
  int ret;
  int i;

  if (!arg.port().length()) {
    return CommandFailure(EINVAL, "Field 'port' must be specified");
  }

  // Get port
  port_name = arg.port().c_str();

  const auto &it = PortBuilder::all_ports().find(port_name);
  if (it == PortBuilder::all_ports().end()) {
    return CommandFailure(ENODEV, "Port %s not found", port_name);
  }
  port_ = it->second;

  node_constraints_ = port_->GetNodePlacementConstraint();

  // Get liset of requested queues
  count_queues_ = arg.qid_size();
  data_queues_ = new queue_t[count_queues_];
  for (int i = 0; i < count_queues_; i++)
    data_queues_[i] = arg.qid(i);

  // Validate the number of requested queues
  // count_queues_ = arg.count_queues();
  if (count_queues_ < 1 || (count_queues_ < 2 && cdq_)) {
    return CommandFailure(EINVAL, "Count queues should be more than 2");
  } else if (count_queues_ > MAX_QUEUES) {
    return CommandFailure(EINVAL, "Count queues should not be more than %d, "
        "it is currently: %d\n", MAX_QUEUES, count_queues_);
  }

  // Acquire queues
  ret = port_->AcquireQueues(reinterpret_cast<const module *>(this),
                             PACKET_DIR_OUT, data_queues_, count_queues_);
  if (ret < 0) {
    return CommandFailure(-ret);
  }

  max_buffer_size_ = port_->tx_queue_size();

  // Check if has doorbell queue
  cdq_ = arg.cdq();

  if (cdq_) {
    doorbell_queue_number_ = data_queues_[0];
  } else {
    doorbell_queue_number_ = -1;
  }

  lossless_ = arg.lossless();
  backpressure_ = arg.backpressure();
  overlay_ = arg.overlay();
  log_ = arg.log();

  // TODO: this modules is not thread safe yet
  if (lossless_) {
    task_id_t tid = RegisterTask(nullptr);
    if (tid == INVALID_TASK_ID)
      return CommandFailure(ENOMEM, "Context creation failed");
  }

  per_flow_buffering_ = arg.per_flow_buffering();
  multiqueue_ = arg.multiqueue();

  ecn_threshold_ = 20 * 32;
  if (arg.ecn_threshold())
    ecn_threshold_ = arg.ecn_threshold();

  count_packets_in_buffer_ = 0;
  bytes_in_buffer_ = 0;
  pause_call_total = 0;
  ctrl_msg_tp_ = 0;
  overlay_tp_ = 0;
  stats_begin_ts_ = 0;

  name_ = arg.mname();
  if (name_.length() < 1) {
    name_ = "unnamed";
  }
  LOG(INFO) << "name: " << name_ << "\n";

  for (i = 0; i < MAX_QUEUES; i++) {
    q_info_[i] = {
        .flow = bess::bkdrft::empty_flow,
        .last_visit = 0,
    };
  }

  for (i = 0; i < MAX_QUEUES; i++)
    failed_ctrl_packets[i] = 0;

  LOG(INFO) << "BKDRFTQueueOut: name: " << name_
            << " pfq: " << per_flow_buffering_ << " cdq: " << cdq_
            << " lossless: " << lossless_ << " bp: " << backpressure_ << "\n";
  LOG(INFO) << "name: " << name_ << " doorbell queue id: "
            << doorbell_queue_number_ << "\n";

#ifdef HOLB
  LOG(INFO) << "HOLB is enabled\n";
#endif

  return CommandSuccess();
}

void BKDRFTQueueOut::DeInit() {
  if (port_) {
    port_->ReleaseQueues(reinterpret_cast<const module *>(this), PACKET_DIR_OUT,
                         nullptr, 0);
  }

  auto iter = BKDRFTQueueOut::buffers_.begin();
  int size = 0;
  bess::Packet **pkts;
  for (; iter != BKDRFTQueueOut::buffers_.end(); iter++) {
    size = iter->second->size();
    pkts = iter->second->data();
    bess::Packet::Free(pkts, size); // free packets
    delete (iter->second);          // free vector
  }
  LOG(INFO) << "DeInint: packets in buffer: "
            << BKDRFTQueueOut::count_packets_in_buffer_ << "\n";
}

std::string BKDRFTQueueOut::GetDesc() const {
  // (cdq: %d, bp:%d, lossless: %d, overlay: %d)
  //, cdq_,
  //                             backpressure_, lossless_, overlay_);
#ifdef HOLB
  return bess::utils::Format("%s/%s(HOLB)", port_->name().c_str(),
                             port_->port_builder()->class_name().c_str());
#else
  return bess::utils::Format("%s/%s (%ld)", port_->name().c_str(),
                             port_->port_builder()->class_name().c_str(),
                             count_packets_in_buffer_);
#endif
}

/*
 * Buffer packets, create a buffer for each flow.
 * Append to the buffer if there is a buffor for current flow.
 * There is a limit for the sum of bytes in each buffer.
 * */
void BKDRFTQueueOut::BufferBatch(__attribute__((unused)) Flow &flow,
        flow_state *fstate, bess::PacketBatch *batch, uint16_t sent_pkts) {
  // TODO: assuming all packets in the batch are for the given flow.
  bess::Packet **pkts = batch->pkts();
  int cnt = batch->cnt();
  uint16_t remaining_pkts = cnt - sent_pkts;

  // do not buffer more than a certain threshold
  // LOG(INFO) << "bytes in buffre " << bytes_in_buffer_ << "\n";
  // if (unlikely(fstate->byte_in_buffer >= MAX_BUFFER_SIZE)) {
  if (unlikely(fstate->byte_in_buffer >= max_buffer_size_)) {
    if (log_)
      LOG(INFO) << "Maximum buffer size reached!\n";

    // Count the dropped packets
    Port *p = port_;

    // TODO: qid in the following stats is not correct
    if (!(p->GetFlags() & DRIVER_FLAG_SELF_OUT_STATS)) {
      const packet_dir_t dir = PACKET_DIR_OUT;

      p->queue_stats[dir][0].packets += 0;
      p->queue_stats[dir][0].dropped += remaining_pkts;
      p->queue_stats[dir][0].requested_hist[cnt]++;
      p->queue_stats[dir][0].diff_hist[remaining_pkts]++;
    }

    bess::Packet::Free(pkts + sent_pkts, remaining_pkts);
    return;
  }

  uint64_t remaining_bytes = 0;

  // in this mode each flow can have its own buffer
  auto buf = fstate->buffer;
  size_t buf_size = buf->size();
  uint64_t pkt_len;
  for (int i = sent_pkts; i < cnt; i++) {
    pkt_len = pkts[i]->total_len();
    remaining_bytes += pkt_len;
    fstate->byte_in_buffer += pkt_len;
    fstate->packet_in_buffer += 1;
        
    buf->push_back(pkts[i]);
    buf_size++;
    if (buf_size > ecn_threshold_)
      ecnMark(pkts[i]);
  }

  count_packets_in_buffer_ += remaining_pkts;
  bytes_in_buffer_ += remaining_bytes;
}

/*
 * Try to send each buffer (every flow has a seperate buffer).
 * The packets failed to send are kept in the buffer and the
 * rest of packets in that buffer will not be tried. (Other
 * flows will be tried.)
 * */
void BKDRFTQueueOut::TrySendBufferPFQ(__attribute__((unused)) Context *cntx) {
  Port *p = port_;
  queue_t qid;
  uint16_t max_batch_size = 32; // BQL can be used here
  uint16_t sent_pkts;
  uint64_t sent_bytes;
  bool sent_ctrl_pkt;
  int dropped;
  auto iter = flow_buffer_mapping_.begin();
  bess::Packet *pkts[max_batch_size];
  size_t k;

  flow_state *fstate;

  // TODO: The order of flows trying to send is important, but it is not
  // taken to the account here.
  for (; iter != flow_buffer_mapping_.end(); iter++) {
    fstate = iter->second;
    qid = fstate->qid;

    while (true) {
      k = fstate->buffer->size();
      if (k <= 0)
        break;

      if (k > max_batch_size)
        k = max_batch_size;

      for (size_t i = 0; i < k; i++)
        pkts[i] = fstate->buffer->at(i);

      sent_pkts = SendPacket(p, qid, pkts, k, &sent_bytes, &sent_ctrl_pkt);
      // total_bytes = total_len(pkts, k);
      dropped = 0; // no packet is dropped

      // Remove sent packets from buffer
      fstate->buffer->erase(fstate->buffer->begin(),
                          fstate->buffer->begin() + sent_pkts);

      if (!(p->GetFlags() & DRIVER_FLAG_SELF_OUT_STATS)) {
        const packet_dir_t dir = PACKET_DIR_OUT;

        p->queue_stats[dir][qid].packets += sent_pkts;
        p->queue_stats[dir][qid].dropped += dropped;
        p->queue_stats[dir][qid].bytes += sent_bytes;
        p->queue_stats[dir][qid].requested_hist[k]++;  // sent_pkts
        p->queue_stats[dir][qid].actual_hist[sent_pkts]++;
        p->queue_stats[dir][qid].diff_hist[dropped]++;
      }

      count_packets_in_buffer_ -= sent_pkts;
      bytes_in_buffer_ -= sent_bytes;
      fstate->packet_in_buffer -= sent_pkts;
      fstate->byte_in_buffer -= sent_bytes;

      if (sent_pkts < k || (cdq_ && !sent_ctrl_pkt)) {
        // some of the packets failed or
        // control packet failed
        break;
      }
    }

    // if (overlay_ && fstate != nullptr
    //     && fstate->byte_in_buffer < buffer_len_low_water
    //     && fstate->overlay_state == OverlayState::TRIGGERED) {
    //   fstate->overlay_state = OverlayState::SAFE;
    //   SendOverlay(iter->first, qid, OverlayState::SAFE);
    // }
  }
}

void BKDRFTQueueOut::TrySendBuffer(Context *cntx) {
  // try send buffer when there are limited number of them
  Port *p = port_;
  const uint16_t max_batch_size = 32; // BQL can be used here
  uint16_t sent_pkts;
  uint64_t sent_bytes;
  bool sent_ctrl_pkt;
  // number of packets to send
  uint16_t burst;
  bess::Packet *pkts[max_batch_size];
  // number of packets in the currenct buffer
  size_t size;

  // for overlay low water
  Flow flow;
  flow_state *fstate = nullptr;

  for (queue_t q = 0; q < count_queues_; q++) {
    size = limited_buffers_[q].size();
    if (size == 0)
      continue;

    while (size > 0) {
      if (size > max_batch_size)
        burst = max_batch_size;
      else
        burst = size;

#ifdef HOLB
      // = CHeck here!
      Flow flow = bess::bkdrft::PacketToFlow(*limited_buffers_[q].at(0));
      auto found = flow_buffer_mapping_.Find(flow);
      if (found == nullptr) {
        break;
      }
#endif

      for (uint16_t i = 0; i < burst; i++) {
        pkts[i] = limited_buffers_[q].at(i);

#ifdef HOLB
        // = CHeck here!
        Flow flow = bess::bkdrft::PacketToFlow(*pkts[i]);
        auto found = flow_buffer_mapping_.Find(flow);
        if (found == nullptr) {
          burst = i;
          pkts[i] = nullptr;
          break;
        }
#endif
      }

      // TODO: it is wrong to assume all the batch has the same flow
      // TODO: this might not change during each iteration (may change in for loop but may not in the while)
      flow = bess::bkdrft::PacketToFlow(*pkts[0]);
      fstate = GetFlowState(cntx, flow);

      // TODO: the mode which is not perflow_queueing but uses multiple queue
      // like RSS is not implemented sent_pkts = SendPacket( p, q, pkts, burst,
      // &sent_bytes, &sent_ctrl_pkt);
      sent_pkts = SendPacket(p, q, pkts, burst, &sent_bytes, &sent_ctrl_pkt);
      // LOG(INFO) << "q: " << (int)q << "sent: " << sent_pkts << "\n";

      // remove sent packets from buffer
      limited_buffers_[q].erase(
          limited_buffers_[q].begin(),
          limited_buffers_[q].begin() + sent_pkts);
      size -= sent_pkts;

      // update stats counters
      if (!(p->GetFlags() & DRIVER_FLAG_SELF_OUT_STATS)) {
        const packet_dir_t dir = PACKET_DIR_OUT;

        p->queue_stats[dir][q].packets += sent_pkts;
        // p->queue_stats[dir][q].dropped += 0;
        p->queue_stats[dir][q].bytes += sent_bytes;
        p->queue_stats[dir][q].requested_hist[burst]++;  // sent_pkts
        p->queue_stats[dir][q].actual_hist[sent_pkts]++;
        p->queue_stats[dir][q].diff_hist[0]++;
      }

      count_packets_in_buffer_ -= sent_pkts;
      bytes_in_buffer_ -= sent_bytes;
      fstate->packet_in_buffer -= sent_pkts;
      fstate->byte_in_buffer -= sent_bytes;

      if (sent_pkts < burst || (cdq_ && !sent_ctrl_pkt)) {
        // failed to send all the burst or
        // failed to send ctrl packet for the current flow
        break;
      }
    }

    // if (overlay_ && fstate != nullptr
    //     && fstate->byte_in_buffer < buffer_len_low_water
    //     && fstate->overlay_state == OverlayState::TRIGGERED) {
    //   fstate->overlay_state = OverlayState::SAFE;
    //   SendOverlay(flow, q, OverlayState::SAFE);
    // }
  }
}

/*
 * Send a BKDRFT Control-Packet for Per-Flow Input Queueing
 * mechanism
 * */
inline uint16_t BKDRFTQueueOut::SendCtrlPkt(Port *p, queue_t qid,
                                            uint16_t sent_pkts,
                                            uint32_t sent_bytes) {
  int dropped;
  bess::Packet *pkt;
  uint16_t sent_ctrl_pkts = 0;
  // int total_len = 0;

  pkt = current_worker.packet_pool()->Alloc();
  if (likely(pkt != nullptr)) {
    int res = bess::bkdrft::prepare_ctrl_packet(pkt, qid, sent_pkts, sent_bytes,
                                                &sample_pkt_flow_);
    if (res != 0) {
      LOG(WARNING) << "SendCtrlPkt: failed to prepare pkt\n";
      failed_ctrl_packets[qid] += 1;
      bess::Packet::Free(pkt);
      return 0;
    }

    // mark the control packet to sit on the corresponding doorbel queue
    bess::bkdrft::mark_packet_with_queue_number(pkt, doorbell_queue_number_);

    sent_ctrl_pkts = p->SendPackets(doorbell_queue_number_, &pkt, 1);
    dropped = 1 - sent_ctrl_pkts;

    // Ctrl packets are not counted in statistics
    // if (!(p->GetFlags() & DRIVER_FLAG_SELF_OUT_STATS)) {
    //   const packet_dir_t dir = PACKET_DIR_OUT;
    //   p->queue_stats[dir][BKDRFT_CTRL_QUEUE].packets += sent_ctrl_pkts;
    //   p->queue_stats[dir][BKDRFT_CTRL_QUEUE].dropped += count -
    //   sent_ctrl_pkts; p->queue_stats[dir][BKDRFT_CTRL_QUEUE].bytes +=
    //   total_len;
    // }

    if (dropped > 0) {
      bess::Packet::Free(pkt);
      failed_ctrl_packets[qid] += dropped;
    }
    ctrl_msg_tp_ += sent_ctrl_pkts;
    return sent_ctrl_pkts;
  } else {
    if (log_)
      LOG(INFO) << "[BKDRFTQueueOut][SendCtrlPkt] ctrl pkt alloc failed \n";
    failed_ctrl_packets[qid] += 1;
    return 0;
  }
}

inline void BKDRFTQueueOut::UpdatePortStats(queue_t qid, uint16_t sent_pkts,
                                            uint16_t dropped,
                                            bess::PacketBatch *batch) {
  Port *p = port_;
  if (!(p->GetFlags() & DRIVER_FLAG_SELF_OUT_STATS)) {
    bess::Packet **pkts = batch->pkts();
    const packet_dir_t dir = PACKET_DIR_OUT;
    uint64_t sent_bytes = total_len(pkts, sent_pkts);

    p->queue_stats[dir][qid].packets += sent_pkts;
    p->queue_stats[dir][qid].bytes += sent_bytes;
    p->queue_stats[dir][qid].dropped += dropped;
    p->queue_stats[dir][qid].requested_hist[sent_pkts + dropped]++;
    p->queue_stats[dir][qid].actual_hist[sent_pkts]++;
    p->queue_stats[dir][qid].diff_hist[dropped]++;
  }
}

/* Try to send ctrl packets for data packets already sent */
bool BKDRFTQueueOut::TryFailedCtrlPackets() {
  int cnt = 0;
  int k = 0;
  int batch_size = 32;
  int sent = 0;
  bool alloc_res = false;
  bess::Packet *pkts[32];
  Port *p = port_;

  for (int i = 0; i < MAX_QUEUES; i++) {
    cnt = failed_ctrl_packets[i];
    if (cnt == 0) {
      continue;
    }

    // k = min(cnt, batch_size)
    k = cnt;
    if (k > batch_size) {
      k = batch_size;
    }

    alloc_res = current_worker.packet_pool()->AllocBulk(pkts, k);
    if (likely(alloc_res)) {
      for (int j = 0; j < k; j++) {
        bess::Packet *pkt = pkts[j];
        // TODO: sotre how many ctrl pkt for each flow was failed
        // we do not have enough information specially sample_pkt_ is not
        // correct Maybe have a similar system as buffering the dropped packets
        int res =
            bess::bkdrft::prepare_ctrl_packet(pkt, i, 0, 0, &sample_pkt_flow_);
        if (res != 0) {
          LOG(WARNING) << "TryFailedCtrlPackets: failed to prepare pkt\n";
          bess::Packet::Free(pkts + j, k - j);
          k = j;
          break;
        }
      }

      sent = p->SendPackets(BKDRFT_CTRL_QUEUE, pkts, k);
      ctrl_msg_tp_ += sent;
      failed_ctrl_packets[i] -= sent;
      if (sent < k) {
        bess::Packet::Free(pkts + sent, k - sent);
        return false;
      }
    }
  }
  return true;
}

/*
 * Initiate a pause mechanisem for current flow.
 * The Pause-Call per Second statistics is the number of
 * times this function is called.
 * */
inline void BKDRFTQueueOut::Pause(Context *cntx, const Flow &flow,
                                  const queue_t qid,
                                  const uint64_t buffer_size) {
  // pause the incoming queue of the flow
  // TODO: get incomming line rate for bandwith-delay estimation
  uint64_t pps;
  uint64_t duration;
  uint64_t ts;
  uint64_t effect_time = 200000; // TODO: find this variable, rtt + ...
  uint64_t estimated_buffer_len;
  Port *p = port_;
  pps = p->rate_.pps[PACKET_DIR_OUT][qid];
  if (pps == 0) {
    // LOG(INFO) << "pps is zero\n";
    duration = 100000; // 100 us
  } else {
    estimated_buffer_len = buffer_size + (pps * effect_time / 1000000);
    duration = ((estimated_buffer_len * 1000000000UL) / pps);
    if (duration > MAX_PAUSE_DURATION) {
      // LOG(INFO) << "more than max pause durtaion\n";
      // duration = MAX_PAUSE_DURATION;
    } else if (duration < MIN_PAUSE_DURATION) {
      // LOG(INFO) << "less than min pause duration\n";
      // duration = MIN_PAUSE_DURATION;
    }
  }

  ts = cntx->current_ns + duration;

  bess::bkdrft::BKDRFTSwDpCtrl &dropMan =
      bess::bkdrft::BKDRFTSwDpCtrl::GetInstance();
  dropMan.PauseFlow(ts, flow);
  pause_call_total += 1;

  if (log_)
    LOG(INFO) << "pause qid: " << (int)qid << " pause duration: "
              << duration << " until: " << ts
              << " pps: " << pps << "\n  flow: " << FlowToString(flow) << "\n";
}


void BKDRFTQueueOut::ProcessBatchWithBuffer(Context *cntx,
                                          bess::PacketBatch *batch) {
  using namespace bess::bkdrft;
  Port *p = port_;
  uint16_t sent_pkts = 0;
  const int cnt = batch->cnt();
  bess::Packet **pkts = batch->pkts();
  Flow flow = PacketToFlow(*(pkts[0]));
  flow_state *fstate = GetFlowState(cntx, flow);
  queue_t qid = fstate->qid;

  // find the number of packets in the buffer assigned to this flow

  // Before we give away this batch let's check on our buffer.
  // if (BKDRFTQueueOut::count_packets_in_buffer_ > 0) {
  if (fstate->packet_in_buffer > 0) {
    // We have some packet in the buffer, to avoid packet
    // reordering I should send the remining packets in
    // buffer first.

    // First we need to save this Batch descriptors
    // No packet has been sent so we just pass 0
    BufferBatch(flow, fstate, batch, 0);

    // Now let's send some packet from the buffer!
    // TODO: try send this flow first
    if (per_flow_buffering_) {
      TrySendBufferPFQ(cntx);
    } else {
      TrySendBuffer(cntx);
    }
  } else {
    // There is no packet in the buffer
    sent_pkts = SendPacket(p, qid, pkts, cnt, nullptr, nullptr);

    if (unlikely(sent_pkts < cnt)) {
      BufferBatch(flow, fstate, batch, sent_pkts);
    }

    // Nothing has been dropped yet
    UpdatePortStats(qid, sent_pkts, 0, batch);
  }

  // if we are buffering then we can decide based on the buffer size
  // for each flow

  if (overlay_ && fstate->packet_in_buffer > buffer_len_high_water
      && cntx->current_ns - fstate->ts_last_overlay > fstate->no_overlay_duration) {
    fstate->ts_last_overlay = cntx->current_ns;
    fstate->overlay_state = OverlayState::TRIGGERED;
    uint64_t duration;
    SendOverlay(flow, fstate, OverlayState::TRIGGERED, &duration);
    fstate->no_overlay_duration = duration;
  }

  if (backpressure_ && fstate->packet_in_buffer > bp_buffer_len_high_water) {
    if (!per_flow_buffering_) {
      // TODO: this code may be better to be placed in some other place
      // TODO: it might be good to have a data structure for keeping track of
      // flows on each queue, without needing to iterate over flow to queue map.
      // pause all flows mapped to this queue
      auto iter = flow_buffer_mapping_.begin();
      for (; iter != flow_buffer_mapping_.end(); iter++) {
        if (iter->second->qid == qid)
          Pause(cntx, iter->first, qid, iter->second->packet_in_buffer);
      }
    } else {
      // each flow has its own queue
      Pause(cntx, flow, qid, fstate->packet_in_buffer);
    }
  }
}

void BKDRFTQueueOut::ProcessBatchLossy(Context *cntx,
                                       bess::PacketBatch *batch) {
  Port *p = port_;
  int sent_pkts = 0;
  const int cnt = batch->cnt();
  bess::Packet **pkts = batch->pkts();
  uint32_t total_bytes = total_len(pkts, cnt);
  // TODO: the assumption of the sample packet from a batch is wrong
  Flow flow = bess::bkdrft::PacketToFlow(*(pkts[0]));

  queue_t qid;
  qid = GetFlowState(cntx, flow)->qid;

  if (cdq_ && qid == 0)
    qid = 1; // qid = 0 is reserved for ctrl/command queue

  // send data packets
  sent_pkts = SendPacket(p, qid, pkts, cnt, nullptr, nullptr);

  // drop fialed packets
  bess::Packet::Free(pkts + sent_pkts, cnt - sent_pkts);

  // update stats
  UpdatePortStats(qid, sent_pkts, cnt - sent_pkts, batch);

  MeasureForPolicy(cntx, qid, flow, sent_pkts, 0, cnt, total_bytes);
}

void BKDRFTQueueOut::ProcessBatch(Context *cntx, bess::PacketBatch *batch) {
  if (unlikely(!port_->conf().admin_up)) {
    bess::Packet::Free(batch->pkts(), batch->cnt());
    return;
  }

  // First check if any ctrl packet is in the queue
  // TODO: here...
  if (cdq_)
    TryFailedCtrlPackets();

  if (unlikely(batch->cnt() < 1)) {
    return;
  }

  if (lossless_) {
    ProcessBatchWithBuffer(cntx, batch);
  } else {
    ProcessBatchLossy(cntx, batch);
  }
}

flow_state *BKDRFTQueueOut::GetFlowState(Context *cntx, Flow &flow) {
  queue_t qid;
  queue_t mapping_q_candid = 0;
  uint64_t min_flow_ts = UINT64_MAX;
  bool found_qid = false;

  // first check if the flow was mapped before
  auto entry = flow_buffer_mapping_.Find(flow);
  if (entry != nullptr) {
    qid = entry->second->qid;
    q_info_[qid].last_visit = cntx->current_ns;
    return entry->second;
  }

  // check if the flow is mapped to a queue
  // if it is not find the queue with the LRU policy
  uint16_t i;
  if (cdq_) {
    // in cdq mode queue doorbell is reserved for command packets
    i = 1;
  } else {
    i = 0;
  }

  // find a queue which has not been used recently
  uint16_t iter_q;
  for (; i < count_queues_; i++) {
    iter_q = data_queues_[i];
    auto q_flow = q_info_[iter_q].flow;
    if (Flow::EqualTo()(q_flow, flow)) {
      qid = iter_q;
      found_qid = true;
      q_info_[qid].last_visit = cntx->current_ns;
      break;
    } else if (q_info_[iter_q].last_visit < min_flow_ts) {
      min_flow_ts = q_info_[iter_q].last_visit;
      mapping_q_candid = iter_q;
    }
  }

  if (unlikely(!found_qid)) {
    qid = mapping_q_candid;
    q_info_[qid].flow = flow;
    q_info_[qid].last_visit = cntx->current_ns;
  }

  // update table
  struct flow_state *state = new struct flow_state();
  state->qid = qid;
  state->overlay_state = OverlayState::SAFE;
  state->ts_last_overlay = 0;
  state->packet_in_buffer = 0;
  state->byte_in_buffer = 0;
  if (per_flow_buffering_) {
    state->buffer = new std::vector<bess::Packet *>();
  } else {
    state->buffer = &limited_buffers_[qid];
  }
  flow_buffer_mapping_.Insert(flow, state);

  return state;
}

/*
 * Send packets on the given port and queue.
 * Assume packets go through a buffer
 * (not really buffering, just faking).
 * If the size of buffer is greater than ecn_threshold_ then mark the
 * packets.
 * The buffer will be in an empty state when this function returns.
 * (Because we will not keep anything for retransmition or ...)
 * */
int BKDRFTQueueOut::SendPacket(Port *p, queue_t qid, bess::Packet **pkts,
                               uint32_t cnt, uint64_t *tx_bytes,
                               bool *ctrl_pkt_sent) {
  uint32_t sent_pkts = 0;
  uint64_t sent_bytes = 0;
  int ret;

  if (ctrl_pkt_sent != nullptr)
    *ctrl_pkt_sent = false;

  if (tx_bytes != nullptr)
    *tx_bytes = 0;

  if (cdq_) {
    // ==== sample_pkt ==========
    sample_pkt_flow_ = bess::bkdrft::PacketToFlow(*pkts[0]);

    // mark destination queue
    for (uint32_t i = 0; i < cnt; i++) {
      // bess::bkdrft::mark_packet_with_queue_number(pkts[i], qid);
      ret = bess::bkdrft::mark_packet_with_queue_number(pkts[i], qid);
      if (ret < 0) {
        if (p->getConnectedPortType() == NIC)
          LOG(ERROR) << "Failed to mark packet with queue number\n";
      }
    }
  }

  sent_pkts = p->SendPackets(qid, pkts, cnt);

  if (sent_pkts && (cdq_ || tx_bytes != nullptr)) {
    sent_bytes = total_len(pkts, sent_pkts);
    if (tx_bytes != nullptr)
      *tx_bytes = sent_bytes;
  }

  if (sent_pkts > 0 && cdq_) {
    bool res = SendCtrlPkt(p, qid, sent_pkts, sent_bytes);
    if (ctrl_pkt_sent != nullptr)
      *ctrl_pkt_sent = res;
  }
  return sent_pkts;
}

void BKDRFTQueueOut::MeasureForPolicy(Context *cntx, queue_t qid,
                                      const Flow &flow, uint16_t sent_pkts,
                                      __attribute__((unused)) uint32_t sent_bytes, uint16_t tx_burst,
                                      __attribute__((unused)) uint32_t total_bytes) {
  double drop_est = 0;
  auto entry = flow_drop_est.Find(flow);
  if (entry != nullptr) {
    drop_est = entry->second;
  }
  const double g = 0.75; uint16_t dropped = tx_burst - sent_pkts;
  // __attribute__((unused)) uint32_t remaining_bytes = total_bytes - sent_bytes;

  // update drop estimate
  drop_est = (g * dropped) + ((1 - g) * drop_est);
  flow_drop_est.Insert(flow, drop_est);

  if (drop_est > drop_high_water) {
    // LOG(INFO) << "drop est: " << drop_est << "\n";
    if (backpressure_) {
      // TODO: removed buffer_size and placed 0
      // uint64_t buffer_size = BufferSize(flow);
      Pause(cntx, flow, qid, 100);
    }

  }
}

/*
 * flow: indicates the upstream sender which should receive an overlay message
 * qid: this flow is mapped to which queue (used for getting pps estimate)
 * */
int BKDRFTQueueOut::SendOverlay(const Flow &flow, const flow_state *fstate,
                                OverlayState state, uint64_t *duration) {
  // LOG(INFO) << "in SendOverlay function\n";
  bess::Packet *pkt = current_worker.packet_pool()->Alloc();
  if (pkt == nullptr) {
    LOG(INFO) << "Failed to allocate overlay\n";
    return -1;
  }

  auto &OverlayMan = bess::bkdrft::BKDRFTOverlayCtrl::GetInstance();
  uint64_t rate = port_->rate_.pps[PACKET_DIR_OUT][fstate->qid];
  // uint64_t rate = port_->rate_.bps[PACKET_DIR_OUT][fstate->qid];
  uint64_t pps = port_->rate_.pps[PACKET_DIR_OUT][fstate->qid];
  uint64_t buffer_size = 0;
  uint64_t bdp = 0; // bandwidth delay product
  uint64_t dt_lw = 0;
  if (state == OverlayState::TRIGGERED) {
    buffer_size = fstate->packet_in_buffer;
    if (buffer_size < buffer_len_low_water) {
      dt_lw = 1000;
    } else if  (rate == 0) {
      dt_lw = 1000000;
    } else {
      // find out when the low water is reached.
      auto entry = OverlayMan.getOverlayEntry(flow);
      if (entry != nullptr) {
        // uint64_t rtt = 10000000; // us
        // bdp = entry->port->rate_.bps[PACKET_DIR_INC][entry->qid] * rtt / 1e9;
      }
      dt_lw = ((buffer_size + bdp) - buffer_len_low_water) * 1e9 / rate;
      if (dt_lw > max_overlay_pause_duration) {
        dt_lw = max_overlay_pause_duration;
      }
    }
  }

  // do not set rate to less than a batch.
  if (pps < 32) {
    pps = 32;
  }

  int ret = OverlayMan.SendOverlayMessage(flow, pkt, pps, dt_lw);
  // int ret = OverlayMan.SendOverlayMessage(flow, pkt, 100000, dt_lw);
  // LOG(INFO) << "SendOverlayMessage return value " << ret << "\n";
  if (ret == 0) {
    overlay_tp_ += 1;

    LOG(INFO) << "Buffer size: " << buffer_size <<  "\n";
    LOG(INFO) << "Sending overlay: name: " << name_
      << "buffer_size: " << buffer_size << " pps: " << pps
      << " pause duration: " << dt_lw << "\n";
  } else {
    bess::Packet::Free(pkt);
  }

  // TODO: if successfuly sent update the duration else it should be zero
  if (duration != nullptr) {
    *duration = dt_lw;
  }
  return 0;
}

struct task_result BKDRFTQueueOut::RunTask(Context *ctx,
                                           __attribute__((unused))
                                           bess::PacketBatch *batch,
                                           __attribute__((unused)) void *arg) {
  if (cdq_) {
    TryFailedCtrlPackets();
  }

  if (per_flow_buffering_) {
    TrySendBufferPFQ(ctx);
  } else {
    TrySendBuffer(ctx);
  }

  // update stats
  if (unlikely(stats_begin_ts_ == 0)) {
    stats_begin_ts_ = ctx->current_ns;
  } else if (ctx->current_ns - stats_begin_ts_ > ONE_SEC) {
    if (backpressure_) {
      pcps.push_back(pause_call_total);
      pause_call_total = 0;
    }

    if (cdq_) {
      ctrl_msg_tp_last_ = ctrl_msg_tp_;
      ctrl_msg_tp_ = 0;
    }

    if (overlay_) {
      overlay_per_sec.push_back(overlay_tp_);
      overlay_tp_ = 0;
    }

    stats_begin_ts_ = 0;
  }

  return {.block = false, .packets = 0, .bits = 0};
}

CommandResponse BKDRFTQueueOut::CommandPauseCalls(const bess::pb::EmptyArg &)
{
  bess::pb::BKDRFTQueueOutCommandPauseCallsResponse resp;
  for (size_t i = 0; i < pcps.size(); i++) {
    LOG(INFO) << "pcps: " << pcps.at(i) << " name: " << name_ << "\n";
    resp.add_pcps(pcps.at(i));
  }
  LOG(INFO) << "===================="
            << "\n";
  return CommandSuccess(resp);
}

CommandResponse BKDRFTQueueOut::CommandGetCtrlMsgTp(const bess::pb::EmptyArg &)
{
  bess::pb::BKDRFTQueueOutCommandGetCtrlMsgTpResponse resp;
  resp.set_throughput(ctrl_msg_tp_last_);
  LOG(INFO) << "name: " << name_
            << " control message throughput: " << ctrl_msg_tp_last_
            << "\n";
  return CommandSuccess(resp);
}

CommandResponse BKDRFTQueueOut::CommandGetOverlayTp(const bess::pb::EmptyArg &)
{
  bess::pb::BKDRFTQueueOutCommandGetOverlayTpResponse resp;
  for (size_t i = 0; i < overlay_per_sec.size(); i++) {
    resp.add_throughput(overlay_per_sec[i]);
  }
  // LOG(INFO) << "name: " << name_
  //           << " overlay throughput: " << "?"
  //           << "\n";
  return CommandSuccess(resp);
}

CommandResponse BKDRFTQueueOut::CommandSetOverlayThreshold(
  const bess::pb::BKDRFTQueueOutCommandSetOverlayThresholdArg &args)
{
  // TODO: make this command thread safe
  buffer_len_low_water = args.low_water();
  buffer_len_high_water = args.high_water();
  LOG(INFO) << "Update Overlay Threshold: name: " << name_
            << " lw: " << buffer_len_low_water
            << " hw: " << buffer_len_high_water << "\n";
  return CommandSuccess();
}

CommandResponse BKDRFTQueueOut::CommandSetBackpressureThreshold(
  const bess::pb::BKDRFTQueueOutCommandSetBackpressureThresholdArg &arg) {
  bp_buffer_len_high_water = arg.threshold();
  LOG(INFO) << "Update Backpressure Threshold: Name: " << name_
            << " threshold: " << bp_buffer_len_high_water << "\n";
  return CommandSuccess();
}

ADD_MODULE(BKDRFTQueueOut, "bkdrft_queue_out",
           "sends packets to a port via a specific queue")
