#include "../pb/bkdrft_msg.pb.h"
#include "../port.h"
#include "../utils/bkdrft.h"
#include "../utils/bkdrft_overlay_ctrl.h"
#include "../utils/bkdrft_sw_drop_control.h"
#include "../utils/flow.h"
#include "../utils/format.h"
#include "../utils/random.h"
#include "bkdrft_queue_out.h"

#include <rte_malloc.h>
#include <rte_ethdev.h>
#include <rte_ring.h>
#include <rte_lcore.h>

#define MIN_PAUSE_DURATION (50000L)
#define MAX_PAUSE_DURATION (10000000L)
#define PAUSE_DURATION_GAMMA (2)  // (ns)
#define MAX_BUFFER_SIZE (4194304) // (5242880) //  (1048576) // 1 mega byte
#define LOW_WATER_MARK (5449973)  // ~ MAX_BUFFER_SIZE * 2/3
#define ONE_SEC (1000000000L)     // (ns)
#define PACKET_HEADERS_SIZE (64)

using bess::bkdrft::Flow;
using bess::bkdrft::total_len;

static inline void ecnMark(bess::Packet *pkt) {
  using bess::utils::Ethernet;
  using bess::utils::Ipv4;

  // TODO: checks to see if protocol is ip
  Ethernet *eth = pkt->head_data<Ethernet *>();
  Ipv4 *ip = reinterpret_cast<Ipv4 *>(eth + 1);

  if (ip->protocol == Ipv4::Proto::kTcp)
    ip->type_of_service |= 0x03;
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
  bool ret2;
  int i;

  initialized_ = false;

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
  for (i = 0; i < count_queues_; i++)
    data_queues_[i] = arg.qid(i);

  // Validate the number of requested queues
  if (count_queues_ < 1 || (count_queues_ < 2 && cdq_)) {
    return CommandFailure(EINVAL, "Count queues should be more than 2");
  } else if (count_queues_ > MAX_QUEUES_PER_DIR) {
    return CommandFailure(EINVAL, "Count queues should not be more than %d, "
        "it is currently: %d\n", MAX_QUEUES_PER_DIR, count_queues_);
  }

  // Acquire queues
  ret = port_->AcquireQueues(reinterpret_cast<const module *>(this),
                             PACKET_DIR_OUT, data_queues_, count_queues_);
  if (ret < 0) {
    return CommandFailure(-ret);
  }

  // max packets buffered for each queue (for pktbuffer)
  max_buffer_size_ = port_->tx_queue_size();

  // Check if has doorbell queue
  cdq_ = arg.cdq();

  if (cdq_) {
    doorbell_queue_number_ = data_queues_[0];
  } else {
    doorbell_queue_number_ = -1;
  }

  buffering_ = arg.lossless();
  backpressure_ = arg.backpressure();
  overlay_ = arg.overlay();
  log_ = arg.log();

  // TODO: this modules is not thread safe yet
  if (buffering_ || backpressure_ || cdq_) {
    task_id_t tid = RegisterTask(nullptr);
    if (tid == INVALID_TASK_ID)
      return CommandFailure(ENOMEM, "Context creation failed");
  }

  per_flow_buffering_ = arg.per_flow_buffering();

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

  ecn_threshold_ = 20 * 32;
  if (arg.ecn_threshold())
    ecn_threshold_ = arg.ecn_threshold();

  for (i = 0; i < MAX_QUEUES_PER_DIR; i++) {
    q_info_[i] = {
        .flow = bess::bkdrft::empty_flow,
        .last_visit = 0,
    };
  }

  for (i = 0; i < MAX_QUEUES_PER_DIR; i++)
    failed_ctrl_packets[i] = 0;

  LOG(INFO) << "name: " << name_
            << " pfq: " << per_flow_buffering_
            << " cdq: " << cdq_
            << " buffering: " << buffering_
            << " bp: " << backpressure_
            << " ecn threshold: " << ecn_threshold_
            << " doorbell queue id: " << doorbell_queue_number_ << "\n";

  ret = SetupFlowControlBlockPool();
  if (ret != 0)
    return CommandFailure(ENOMEM, "Failed to setup packet buffers\n");

  for (i = 0; i < COUNT_PRIORITY; i++)
    prio_queue_len_[i] = 0;

  ctrl_batch_used_ = 0;
  first_ctrl_pkt_ts_ = 0;
  ret2 = current_worker.packet_pool()->AllocBulk(ctrl_batch_,
                                              ctrl_batch_size, 256);
  if (!ret2)
    return CommandFailure(ENOMEM, "Failed to allocate ctrl batch\n");

  initialized_ = true;
  return CommandSuccess();

}

int BKDRFTQueueOut::SetupFlowControlBlockPool() {
  fsp_top_ = -1;

  // allocate memory for pool of flow control blocks
  flow_state_pool_ = reinterpret_cast<struct flow_state *>(
                        rte_zmalloc("flow_state_pool",
                          sizeof(struct flow_state) * max_availabe_flows,
                          64));
  if (flow_state_pool_ == nullptr) LOG(FATAL) << "Flow state pool failed\n";

  // allocate memory for an array showing flow of each Flow Control Block
  flow_state_flow_id_ = reinterpret_cast<bess::bkdrft::Flow *> (
                        rte_malloc("flow_id_array",
                          sizeof(bess::bkdrft::Flow) * max_availabe_flows,
                          0));
  if (flow_state_pool_ == nullptr) LOG(FATAL) << "Flow state flow id pool failed\n";

  // initializing flow control block pool elements
  for (uint32_t i = 0; i < max_availabe_flows; i++) {
    flow_state_pool_[i].flow_id = i;
    flow_state_pool_[i].qid = 0;
    flow_state_pool_[i].prio = 0;
    flow_state_pool_[i].in_use = 0;
    if (buffering_) {
      if (per_flow_buffering_) {
        char name[20];
        snprintf(name, 20, "buf_%s_%d", name_.c_str(), i);
        // LOG(INFO) << "buffer name: " <<  name << "\n";

        flow_state_pool_[i].buffer = new_pktbuffer(name, max_buffer_size_);
        if (flow_state_pool_[i].buffer == nullptr) {
          LOG(ERROR) << "new_pktbuffer failed\n";
          for (uint32_t j = 0; j < i; j++) {
            free_pktbuffer(flow_state_pool_[j].buffer);
          }
          goto free_pool;
        }
      }
    }
  }

  if (buffering_ && !per_flow_buffering_) {
    // initialize limited buffers
    char name[20];
    for (uint32_t i = 0; i < MAX_QUEUES_PER_DIR; i++) {
      snprintf(name, 20, "buf_%s_%d", name_.c_str(), i);
      // LOG(INFO) << "buffer name: " <<  name << "\n";

      limited_buffers_[i] = new_pktbuffer(name, max_buffer_size_);
      if (limited_buffers_[i] == nullptr) {
        LOG(INFO) << "new_pktbuffer failed\n";
        for (uint32_t j = 0; j < i; j++) {
          free_pktbuffer(limited_buffers_[j]);
        }
        goto free_pool;
      }
    }
  }
  return 0;

free_pool:
  rte_free(flow_state_pool_);
  rte_free(flow_state_flow_id_);
  return -1;
}

void BKDRFTQueueOut::DeInit() {
  if (port_) {
    port_->ReleaseQueues(reinterpret_cast<const module *>(this), PACKET_DIR_OUT,
                         nullptr, 0);
  }

  // auto iter = BKDRFTQueueOut::buffers_.begin();
  // int size = 0;
  // bess::Packet **pkts;
  // for (; iter != BKDRFTQueueOut::buffers_.end(); iter++) {
  //   size = iter->second->size();
  //   pkts = iter->second->data();
  //   bess::Packet::Free(pkts, size); // free packets
  //   delete (iter->second);          // free vector
  // }

  if (initialized_) {
    if (per_flow_buffering_ && flow_state_pool_ != nullptr) {
      for (uint32_t i = 0; i < max_availabe_flows; i++) {
        // rte_free(flow_state_pool_[i].buffer);
        // LOG(INFO) << "i: " << i << "\n";
        free_pktbuffer(flow_state_pool_[i].buffer);
      }
    } else if (limited_buffers_ != nullptr) {
      for (uint32_t i = 0; i < MAX_QUEUES_PER_DIR ; i++) {
        free_pktbuffer(limited_buffers_[i]);
      }
    }
    rte_free(flow_state_pool_);
    rte_free(flow_state_flow_id_);
  }
  LOG(INFO) << "DeInint: packets in buffer: "
            << BKDRFTQueueOut::count_packets_in_buffer_ << "\n";
}

std::string BKDRFTQueueOut::GetDesc() const {
  // (cdq: %d, bp:%d, lossless: %d, overlay: %d)
  //, cdq_,
  //                             backpressure_, lossless_, overlay_);
  return bess::utils::Format("%s/%s", port_->name().c_str(),
                             port_->port_builder()->class_name().c_str());
}

/*
 * Buffer packets, create a buffer for each flow.
 * Append to the buffer if there is a buffor for current flow.
 * There is a limit for the sum of bytes in each buffer.
 * */
void BKDRFTQueueOut::BufferBatch(__attribute__((unused)) Flow *flow,
        struct flow_state *fstate, bess::PacketBatch *batch,
        uint16_t sent_pkts) {
  // TODO: assuming all packets in the batch are for the given flow.
  bess::Packet **pkts = batch->pkts() + sent_pkts;
  int cnt = batch->cnt();
  uint16_t remaining_pkts = cnt - sent_pkts;
  uint32_t count_enqueue;
  uint64_t remaining_bytes = 0;

  count_enqueue = pktbuffer_enqueue(fstate->buffer, pkts, remaining_pkts);
  uint32_t failed_pkt = remaining_pkts - count_enqueue;
  if (unlikely(failed_pkt > 0)) {
    // Dropped packets failed to enqueue
    bess::Packet::Free(pkts + count_enqueue, failed_pkt);
    // Count the dropped packets
    Port *p = port_;

    // TODO: qid in the following stats is not correct
    // (we are not using per queue stats)
    if (!(p->GetFlags() & DRIVER_FLAG_SELF_OUT_STATS)) {
      const packet_dir_t dir = PACKET_DIR_OUT;
      p->queue_stats[dir][0].packets += 0;
      p->queue_stats[dir][0].dropped += failed_pkt;
      p->queue_stats[dir][0].requested_hist[cnt]++;
      p->queue_stats[dir][0].diff_hist[failed_pkt]++;
    }
  }

  // ECN Mark packets
  if (fstate->buffer->pkts >= ecn_threshold_) {
    for (uint32_t i = 0; i < count_enqueue; i++) {
      ecnMark(pkts[i]);
    }
  }

  count_packets_in_buffer_ += remaining_pkts;
  bytes_in_buffer_ += remaining_bytes;
  filled_buffers_.insert(fstate); // this buffer has some packets

  // LOG(INFO) << "BufferBatch total: " << count_packets_in_buffer_
  //  << " F: " << fstate->buffer->pkts << "\n";
}

/*
 * Try to send each buffer (every flow has a seperate buffer).
 * The packets failed to send are kept in the buffer and the
 * rest of packets in that buffer will not be tried. (Other
 * flows will be tried.)
 * */
void BKDRFTQueueOut::TrySendBufferPFQ(__attribute__((unused))Context *cntx) {
  Port *p = port_;
  queue_t qid;
  uint16_t max_batch_size = peek_size; // TODO:BQL can be used here
  uint16_t sent_pkts;
  uint64_t sent_bytes;
  bool sent_ctrl_pkt;
  int dropped;
  // auto iter = flow_buffer_mapping_.begin();
  // auto iter = filled_buffers_.begin();
  // bess::Packet *pkts[max_batch_size];
  size_t k;
  uint32_t dequeue_size;

  struct flow_state *fstate;
  // Flow *flow;
  // int fstate_index;
  int prio;
  int q_index;
  int flow_id;

  for (prio = 0; prio < COUNT_PRIORITY; prio++) {
    for (q_index = 0; q_index < prio_queue_len_[prio]; q_index++) {
      flow_id = prio_queue_[prio][q_index];
      fstate = &flow_state_pool_[flow_id];
      qid = fstate->qid;
      // flow = &flow_state_flow_id_[flow_id];

      while (true) {
        // LOG(INFO) << "qid: " << (int)fstate->qid << "\n";
        k = fstate->buffer->pkts;
        //LOG(INFO) << "Flow buffer size: " << k << "\n";
        if (unlikely(k <= 0)) {
          // filled_buffers_.erase(iter); // bufer has been emptied
          // if (cntx->current_ns - fstate->last_used > flow_dealloc_limit) {
          //   // the flow control block has not received packet for a while
          //   // put it back in the pool.

          //   // LOG(INFO) << "TrySendBufferPfq: deallocate\n";
          //   DeallocateFlowState(cntx, flow_state_flow_id_[fstate->flow_id]);
          // }
          break;
        }

        if (k > max_batch_size)
          k = max_batch_size;

        sent_pkts = SendPacket(p, fstate, fstate->buffer->peek, k,
                               &sent_bytes, &sent_ctrl_pkt);

        // total_bytes = total_len(pkts, k);
        dropped = 0; // no packet is dropped

        // Remove sent packets from buffer
        dequeue_size = pktbuffer_dequeue(fstate->buffer, nullptr, sent_pkts);
        if (unlikely(dequeue_size != sent_pkts)) {
          LOG(FATAL) << "expected dequeue " << sent_pkts << " actual "
            << dequeue_size << " queue size "
            << fstate->buffer->pkts << "\n";
        }

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
  // bess::Packet *pkts[max_batch_size];
  // number of packets in the currenct buffer
  size_t size;
  uint32_t dequeue_size;

  // for overlay low water
  Flow flow;
  flow_state *fstate = nullptr;

  for (queue_t q = 0; q < count_queues_; q++) {
    // size = limited_buffers_[q].size();
    // size = rte_ring_count(limited_buffers_[q]);
    size = limited_buffers_[q]->pkts;
    if (size == 0)
      continue;

    while (size > 0) {
      if (size > max_batch_size)
        burst = max_batch_size;
      else
        burst = size;

      // TODO: sampling is not a good Idea
      flow = bess::bkdrft::PacketToFlow(*limited_buffers_[q]->peek[0]);
      fstate = GetFlowState(cntx, flow);

      sent_pkts = SendPacket(p, fstate, limited_buffers_[q]->peek,
                             burst, &sent_bytes, &sent_ctrl_pkt);

      dequeue_size = pktbuffer_dequeue(limited_buffers_[q], nullptr, sent_pkts);
      if (dequeue_size != sent_pkts) {
        LOG(FATAL) << "failed to dequeue some packets. requested: " << sent_pkts
                   << " dequeued: " << dequeue_size
                   << " available pkts: " << limited_buffers_[q]->pkts << "\n";
      }
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
      // fstate->packet_in_buffer -= sent_pkts;
      // fstate->byte_in_buffer -= sent_bytes;

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


int BKDRFTQueueOut::TryFlushCtrlBatch()
{
  bool can_flush = (ctrl_batch_used_ > 31) ||
    (current_ns_ - first_ctrl_pkt_ts_) > 1000;
  if (!can_flush) {
    // LOG(INFO) << "Can not flush delta: "<<current_ns_ - first_ctrl_pkt_ts_ <<"\n";
    return 0;
  }

  bool ret;
  uint32_t i;
  uint32_t healthy_pkts;
  uint32_t failed;
  uint32_t sent_ctrl_pkts;
  sent_ctrl_pkts = port_->SendPackets(doorbell_queue_number_, ctrl_batch_,
                                      ctrl_batch_used_);
  ctrl_msg_tp_ += sent_ctrl_pkts;
  failed = ctrl_batch_used_ - sent_ctrl_pkts;
  healthy_pkts = ctrl_batch_size - ctrl_batch_used_;


  // Ctrl packets are not counted in statistics
  // if (!(p->GetFlags() & DRIVER_FLAG_SELF_OUT_STATS)) {
  //   const packet_dir_t dir = PACKET_DIR_OUT;
  //   p->queue_stats[dir][BKDRFT_CTRL_QUEUE].packets += sent_ctrl_pkts;
  //   p->queue_stats[dir][BKDRFT_CTRL_QUEUE].dropped += count -
  //   sent_ctrl_pkts; p->queue_stats[dir][BKDRFT_CTRL_QUEUE].bytes +=
  //   total_len;
  // }

  // move valid discriptors to the fron (TODO: should we use rings?)
  if (unlikely(failed > 0)) {
    // bess::Packet::Free(pkt);
    // failed_ctrl_packets[qid] += dropped;
    for (i = 0; i < failed + healthy_pkts; i++) {
      ctrl_batch_[i] = ctrl_batch_[i + sent_ctrl_pkts];
    }
    // LOG(INFO) << "Failed to send ctrl batch\n";
  }

  // Allocate new healthy packets
  ret= current_worker.packet_pool()->AllocBulk(
                    ctrl_batch_ + failed + healthy_pkts, sent_ctrl_pkts, 256);
  if (unlikely(!ret))
    LOG(FATAL) << "Falied to refill the ctrl_batch_\n";
  // else
  //   LOG(INFO) << "update ctrl batch, new: " << sent_ctrl_pkts
  //     << " kept: " << failed + healthy_pkts << "\n";

  ctrl_batch_used_ = failed;

  return 0;
}

/*
 * Send a BKDRFT Control-Packet for Per-Flow Input Queueing
 * mechanism
 * */
uint16_t BKDRFTQueueOut::SendCtrlPkt(Port *p, queue_t qid, uint32_t prio,
                                     uint16_t sent_pkts, uint32_t sent_bytes)
{
  // int dropped;
  bess::Packet *pkt;
  // uint16_t sent_ctrl_pkts = 0;
  int res;
  // int total_len = 0;

  if (unlikely(ctrl_batch_used_ >= ctrl_batch_size))
    LOG(FATAL) << "ctrl_batch_limit reached\n";

  // pkt = current_worker.packet_pool()->Alloc();
  if (ctrl_batch_used_ == 0)
    first_ctrl_pkt_ts_ = current_ns_;
  pkt = ctrl_batch_[ctrl_batch_used_++];
  if (likely(pkt != nullptr)) {
    res = bess::bkdrft::prepare_ctrl_packet(pkt, qid, prio, sent_pkts,
                                            sent_bytes, &sample_pkt_flow_);
    if (unlikely(res != 0)) {
      LOG(WARNING) << "SendCtrlPkt: failed to prepare pkt\n";
      failed_ctrl_packets[qid] += 1;
      // bess::Packet::Free(pkt);
      ctrl_batch_used_--;
      return 0;
    }

    if (p->getConnectedPortType() == NIC) {
      // mark the control packet to sit on the corresponding doorbel queue
      bess::bkdrft::mark_packet_with_queue_number(pkt, doorbell_queue_number_);
    }

    // int sent_ctrl_pkts = port_->SendPackets(doorbell_queue_number_, &pkt, 1);
    TryFlushCtrlBatch();
    // sent_ctrl_pkts = port_->SendPackets(doorbell_queue_number_, ctrl_batch_,
    //                                     ctrl_batch_used_);
    // if (sent_ctrl_pkts == 0) bess::Packet::Free(pkt);
    // ctrl_msg_tp_ += sent_ctrl_pkts;

    return 1;
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
  // Random rng;
  uint64_t pps;
  uint64_t duration;
  uint64_t ts;
  // uint64_t jitter = rng.GetRange(MIN_PAUSE_DURATION);
  // pps = 3000000;
  Port *p = port_;
  pps = p->rate_[PACKET_DIR_OUT][qid].pps;
  if (pps == 0) {
    // LOG(INFO) << "pps is zero\n";
    duration = MIN_PAUSE_DURATION; // 10 us
  } else {
    duration = ((buffer_size * 1000000000UL) / pps);
    if (duration > MAX_PAUSE_DURATION) {
      // LOG(INFO) << "more than max pause durtaion\n";
      duration = MAX_PAUSE_DURATION;
    } else if (duration < MIN_PAUSE_DURATION) {
      // LOG(INFO) << "less than min pause duration\n";
      duration = MIN_PAUSE_DURATION;
    }
  }

  ts = cntx->current_ns + duration; //+ jitter;

  // LOG(INFO) << "before pauseing flow\n";

  bess::bkdrft::BKDRFTSwDpCtrl &dropMan =
      bess::bkdrft::BKDRFTSwDpCtrl::GetInstance();
  dropMan.PauseFlow(ts, pps + 32, flow);
  // dropMan.PauseFlow(ts, 100000000, flow);
  pause_call_total += 1;

  // if (log_)
  LOG(INFO) << "name: " << name_ << " pause qid: " << (int)qid << " pause duration: "
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
  flow_state *fstate = GetFlowState(cntx, flow); // TODO: use ffs to find free flow in the pool
  queue_t qid = fstate->qid;
  fstate->last_used = cntx->current_ns;

  // Before we give away this batch let's check on our buffer.
  // if (BKDRFTQueueOut::count_packets_in_buffer_ > 0) {
  if (fstate->buffer->pkts > 0) {
    // We have some packet in the buffer, to avoid packet
    // reordering I should send the remining packets in
    // buffer first.

    // First we need to save this Batch descriptors
    // No packet has been sent so we just pass 0
    BufferBatch(&flow, fstate, batch, 0);

    // Now let's send some packet from the buffer!
    // TODO: try send this flow first
    if (per_flow_buffering_) {
      TrySendBufferPFQ(cntx);
    } else {
      TrySendBuffer(cntx);
    }
  } else {
    // There is no packet in the buffer
    sent_pkts = SendPacket(p, fstate, pkts, cnt, nullptr, nullptr);

    if (unlikely(sent_pkts < cnt)) {
      BufferBatch(&flow, fstate, batch, sent_pkts);
    }

    // Nothing has been dropped yet
    UpdatePortStats(qid, sent_pkts, 0, batch);
  }

  // if we are buffering then we can decide based on the buffer size
  // for each flow

  if (overlay_ && fstate->buffer->pkts > buffer_len_high_water
      && cntx->current_ns - fstate->ts_last_overlay > fstate->no_overlay_duration) {
    fstate->ts_last_overlay = cntx->current_ns;
    fstate->overlay_state = OverlayState::TRIGGERED;
    uint64_t duration;
    SendOverlay(flow, fstate, OverlayState::TRIGGERED, &duration);
    fstate->no_overlay_duration = duration;
  }

  if (backpressure_ && fstate->buffer->pkts > bp_buffer_len_high_water) {
    // LOG(INFO) << "pause pkts in buf: " << fstate->buffer->pkts << "\n";
    if (!per_flow_buffering_) {
      // TODO: this code may be better to be placed in some other place
      // TODO: it might be good to have a data structure for keeping track of
      // flows on each queue, without needing to iterate over flow to queue map.
      // pause all flows mapped to this queue
      auto iter = flow_buffer_mapping_.begin();
      for (; iter != flow_buffer_mapping_.end(); iter++) {
        if (iter->second->qid == qid) {
          Pause(cntx, iter->first, qid, iter->second->buffer->pkts);
          // LOG(INFO) << "pause: " << FlowToString(iter->first) << "\n";
        }
      }
    } else {
      // each flow has its own queue
      Pause(cntx, flow, qid, fstate->buffer->pkts);
    }
  }
}

void BKDRFTQueueOut::ProcessBatchLossy(Context *cntx,
                                       bess::PacketBatch *batch) {
  Port *p = port_;
  int sent_pkts = 0;
  const int cnt = batch->cnt();
  bess::Packet **pkts = batch->pkts();
  // uint32_t total_bytes = total_len(pkts, cnt);
  // TODO: the assumption of the sample packet from a batch is wrong
  Flow flow = bess::bkdrft::PacketToFlow(*(pkts[0]));
  struct flow_state *fstate = GetFlowState(cntx, flow);

  // if (cdq_ && qid == 0)
  //   qid = 1; // qid = 0 is reserved for ctrl/command queue

  // send data packets
  sent_pkts = SendPacket(p, fstate, pkts, cnt, nullptr, nullptr);

  // drop fialed packets
  if (unlikely(cnt > sent_pkts)) {
    // LOG(ERROR) << "Sent pkts: " << sent_pkts << " cnt: " << cnt << "\n";
    bess::Packet::Free(pkts + sent_pkts, cnt - sent_pkts);
  }

  // update stats
  UpdatePortStats(fstate->qid, sent_pkts, cnt - sent_pkts, batch);
}

void BKDRFTQueueOut::ProcessBatch(Context *cntx, bess::PacketBatch *batch) {
  if (unlikely(!port_->conf().admin_up)) {
    bess::Packet::Free(batch->pkts(), batch->cnt());
    return;
  }

  current_ns_ = cntx->current_ns;

  // First check if any ctrl packet is in the queue
  // TODO: here...
  if (cdq_) {
    Port *p  = port_;
    if (ctrl_batch_used_ >= ctrl_batch_size - 1) {
      LOG(INFO) << "ctrl batch is full dropping\n";
      bess::Packet::Free(batch->pkts(), batch->cnt());
      if (!(p->GetFlags() & DRIVER_FLAG_SELF_OUT_STATS)) {
        const packet_dir_t dir = PACKET_DIR_OUT;
        p->queue_stats[dir][0].packets += 0;
        p->queue_stats[dir][0].dropped += batch->cnt();
        p->queue_stats[dir][0].requested_hist[batch->cnt()]++;
        p->queue_stats[dir][0].diff_hist[batch->cnt()]++;
      }
      TryFlushCtrlBatch();
      return;
    }
    // TryFailedCtrlPackets();
  }

  if (unlikely(batch->cnt() < 1)) {
    return;
  }

  // static uint64_t last_print = 0;
  // uint64_t before = rte_get_timer_cycles();
  // uint64_t after;

  if (buffering_) {
    ProcessBatchWithBuffer(cntx, batch);
  } else {
    ProcessBatchLossy(cntx, batch);
  }

  // after = rte_get_timer_cycles();
  // if (after - last_print > rte_get_timer_hz()) {
  //   last_print = after;
  //   LOG(INFO) << "process batch: " << after - before << "\n";
  // }
}

flow_state *BKDRFTQueueOut::GetFlowState(Context *cntx, Flow &flow) {
  queue_t qid;
  queue_t mapping_q_candid = 0;
  uint64_t min_flow_ts = UINT64_MAX;

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
    if (q_info_[iter_q].last_visit < min_flow_ts) {
      min_flow_ts = q_info_[iter_q].last_visit;
      mapping_q_candid = iter_q;
    }
  }


  qid = mapping_q_candid;
  if (!cdq_ &&  !per_flow_buffering_) qid = data_queues_[0];

  q_info_[qid].flow = flow;
  q_info_[qid].last_visit = cntx->current_ns;

  // update table
  // struct flow_state *state = new struct flow_state();
  struct flow_state *state = nullptr;
  uint32_t index = (fsp_top_ + 1) % max_availabe_flows;
  for (; index != fsp_top_;) {
    if (flow_state_pool_[index].in_use == 0) {
      state = &flow_state_pool_[index];
      flow_state_flow_id_[index] = flow;
      // fsp_top_ = (index + 1) % max_availabe_flows;
      fsp_top_ = index;
      break;
    } else {
      // check if the flow should be deallocated
      // LOG(INFO) << "Here: " << index << "\n";
      if (flow_state_pool_[index].buffer->pkts == 0 &&
          cntx->current_ns - flow_state_pool_[index].last_used > flow_dealloc_limit) {
        DeallocateFlowState(cntx, flow_state_flow_id_[index]);
        state = &flow_state_pool_[index];
        flow_state_flow_id_[index] = flow;
        // fsp_top_ = (index + 1) % max_availabe_flows;
        fsp_top_ = index;
        break;
      }
    }
   index = (index + 1) % max_availabe_flows;
  }

  if (state == nullptr) {
    LOG(FATAL) << "flow state pool overflow\n";
  }

  state->qid = qid;
  state->overlay_state = OverlayState::SAFE;
  state->ts_last_overlay = 0;
  state->in_use = 1;
  state->last_used = cntx->current_ns;
  if (!per_flow_buffering_) {
    state->buffer = limited_buffers_[qid];
  }
  flow_buffer_mapping_.Insert(flow, state);
  // TODO: removing from prio queue is not that easy (use better data structure)
  // TODO: priority is hard coded
  if (flow.port_dst.value() == 50 || flow.port_dst.value() == 11211
      || flow.port_src.value() == 11211) {
    // high prio
    prio_queue_[0][prio_queue_len_[0]++] = state->flow_id;
    state->prio = 0;
  } else {
    // low prio
    prio_queue_[1][prio_queue_len_[1]++] = state->flow_id;
    state->prio = 1;
  }

  // LOG(INFO) << "name: " << name_ << " Flow Mapped: "
  //           << FlowToString(flow) << " q: " << (int)qid << "\n";

  return state;
}


void BKDRFTQueueOut::DeallocateFlowState(__attribute__((unused)) Context *cntx,
                                         Flow &flow) {
  auto entry = flow_buffer_mapping_.Find(flow);

  if (unlikely(entry == nullptr))
    return;

  LOG(INFO) << "Deallocate Fow State\n";

  struct flow_state *fstate = entry->second;
  uint32_t prio = fstate->prio;

  bool prio_found = false;
  for (int i = 0; i < prio_queue_len_[prio]; i++) {
    if (prio_found) {
      prio_queue_[prio][i - 1] = prio_queue_[prio][i];
    } else if (prio_queue_[prio][i] == prio) {
      prio_found = true;
    }
  }
  if (prio_found)
    prio_queue_len_[prio]--;

  if (per_flow_buffering_) {
    // fstate->buffer->clear();
    // fstate->buffer = nullptr; // put back buffer in the pool
    // rte_ring_reset(fstate->buffer);
    rte_ring_reset(fstate->buffer->ring_queue);
    fstate->buffer->tail = 0;
    fstate->buffer->pkts = 0;
  }
  flow_buffer_mapping_.Remove(flow);

  // TODO: not thread safe
  fstate->in_use = false; // put back buffer in the pool
}

/*
 * Send packets on the given port and queue.
 * Assume packets go through a buffer
 * (not really buffering, just faking).
 * The buffer will be in an empty state when this function returns.
 * (Because we will not keep anything for retransmition or ...)
 * */
int BKDRFTQueueOut::SendPacket(Port *p, struct flow_state *fstate,
                               bess::Packet **pkts,
                               uint32_t cnt, uint64_t *tx_bytes,
                               bool *ctrl_pkt_sent) {
  queue_t qid = fstate->qid;
  uint32_t sent_pkts = 0;
  uint64_t sent_bytes = 0;
  int ret;

  if (ctrl_pkt_sent != nullptr)
    *ctrl_pkt_sent = false;

  if (tx_bytes != nullptr)
    *tx_bytes = 0;

  if (cdq_) {
    // ==== sample_pkt ==========
    // TODO: this is a copy
    sample_pkt_flow_ = flow_state_flow_id_[fstate->flow_id];

    // mark destination queue
    if (p->getConnectedPortType() == NIC) {
      for (uint32_t i = 0; i < cnt; i++) {
        // rte_prefetch0(pkts[i]->head_data()); // makes worse on the test
        ret = bess::bkdrft::mark_packet_with_queue_number(pkts[i], qid);
        if (unlikely(ret < 0)) {
          LOG(ERROR) << "Failed to mark packet with queue number\n";
        }
      }
    }
  }

  sent_pkts = p->SendPackets(qid, pkts, cnt);

  if (sent_pkts > 0 && (cdq_ || tx_bytes != nullptr)) {
    sent_bytes = total_len(pkts, sent_pkts);
    if (tx_bytes != nullptr)
      *tx_bytes = sent_bytes;
  }

  if (sent_pkts > 0 && cdq_) {
    bool res = SendCtrlPkt(p, qid, fstate->prio, sent_pkts, sent_bytes);
    if (ctrl_pkt_sent != nullptr)
      *ctrl_pkt_sent = res;
  }
  return sent_pkts;
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
  uint64_t rate = port_->rate_[PACKET_DIR_OUT][fstate->qid].pps;
  uint64_t buffer_size = 0;
  uint64_t bdp = 0; // bandwidth delay product
  uint64_t dt_lw = 0;
  if (state == OverlayState::TRIGGERED) {
    buffer_size = fstate->buffer->pkts;
    if (buffer_size < buffer_len_low_water) {
      dt_lw = 1000;
    } else if  (rate == 0) {
      dt_lw = 1000000;
    } else {
      // find out when the low water is reached.
      dt_lw = ((buffer_size + bdp) - buffer_len_low_water) * 1e9 / rate;
      if (dt_lw > max_overlay_pause_duration) {
        dt_lw = max_overlay_pause_duration;
      }
    }
  }

  // do not set rate to less than a batch.
  if (rate < 32) {
    rate = 32;
  }

  int ret = OverlayMan.SendOverlayMessage(flow, pkt, rate, dt_lw);
  // int ret = OverlayMan.SendOverlayMessage(flow, pkt, 100000, dt_lw);
  // LOG(INFO) << "SendOverlayMessage return value " << ret << "\n";
  if (ret == 0) {
    overlay_tp_ += 1;

    LOG(INFO) << "Buffer size: " << buffer_size <<  "\n";
    LOG(INFO) << "Sending overlay: name: " << name_
      << "buffer_size: " << buffer_size << " pps: " << rate
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
  current_ns_ = ctx->current_ns;
  if (cdq_) {
    TryFlushCtrlBatch();
    // TryFailedCtrlPackets();
  }

  if (count_packets_in_buffer_ > 0) {
    if (per_flow_buffering_) {
      TrySendBufferPFQ(ctx);
    } else {
      TrySendBuffer(ctx);
    }
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
