#include "bkdrft_queue_inc.h"
#include <rte_cycles.h>
#include <rte_malloc.h>

#include "../port.h"
#include "../utils/bkdrft.h"
#include "../utils/bkdrft_overlay_ctrl.h"
#include "../utils/bkdrft_sw_drop_control.h"
#include "../utils/endian.h"
#include "../utils/format.h"
#include "../utils/time.h"
#include "../utils/ether.h"

#define max_burst (32)

using bess::utils::be16_t;
using bess::utils::be32_t;
using bess::utils::Ethernet;
using bess::utils::Vlan;
using bess::utils::Ipv4;
using namespace bess::bkdrft;


bool is_arp(bess::Packet *pkt) {
  Ethernet *eth = reinterpret_cast<Ethernet *>(pkt->head_data());
  uint16_t ether_type = eth->ether_type.value();
  if (ether_type == Ethernet::Type::kArp)
    return true;
  else if (ether_type == Ethernet::Type::kVlan) {
    Vlan *vlan = reinterpret_cast<Vlan *>(eth + 1);
    return vlan->ether_type.value() == Ethernet::Type::kArp;
  }
  return false;
}

const Commands BKDRFTQueueInc::cmds = {
    {"set_burst", "BKDRFTQueueIncCommandSetBurstArg",
     MODULE_CMD_FUNC(&BKDRFTQueueInc::CommandSetBurst), Command::THREAD_SAFE},
    {"get_overlay_stats", "BKDRFTQueueIncCommandGetOverlayStatsResponse",
     MODULE_CMD_FUNC(&BKDRFTQueueInc::CommandGetOverlayStats),
                                                        Command::THREAD_SAFE}
};

CommandResponse BKDRFTQueueInc::Init(const bess::pb::BKDRFTQueueIncArg &arg) {
  const char *port_name;
  task_id_t tid;
  CommandResponse err;
  burst_ = bess::PacketBatch::kMaxBurst;
  if (!arg.port().length()) {
    return CommandFailure(EINVAL, "Field 'port' must be specified");
  }
  port_name = arg.port().c_str();

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

  // check if module backpressure is active
  backpressure_ = arg.backpressure();
  overlay_ = arg.overlay();
  cdq_ = arg.cdq();

  // Check if port supports rate limiting, it is needed for overlay
  if ((overlay_ || backpressure_) && (!port_->isRateLimitingEnabled()))
    return CommandFailure(EINVAL, "Port does not support rate limmiting, it is "
                                  "needed for overlay to work.");

  if (overlay_ && !cdq_)
    return CommandFailure(EINVAL, "Overlay is implemented in cdq mode, "
                                   "cdq is not active");

  // Get requested queues
  count_managed_queues = arg.qid_size();
  managed_queues = new queue_t[count_managed_queues];
  for (int i = 0; i < count_managed_queues; i++)
    managed_queues[i] = arg.qid(i);

  if (count_managed_queues < 1 || (count_managed_queues < 2 && cdq_)) {
    return CommandFailure(EINVAL, "Number of queues are insufficient, "
        "at least one queue is needed. If cdq is enabled there should be a "
        "minimum of 2 queues.");
  }

  if (count_managed_queues > 1 && !cdq_) {
    return CommandFailure(EINVAL, "BKDRFTQueueInc needs to have cdq enabled "
        "in order to manage multiple queues");
  }

  if (count_managed_queues > MAX_QUEUES_PER_DIR) {
    return CommandFailure(EINVAL, "Maximum supported number of queues are %d. "
        "%d queues have been requested", MAX_QUEUES_PER_DIR, count_managed_queues);
  }

  // acquire queue
  int ret = port_->AcquireQueues(reinterpret_cast<const module *>(this),
                               PACKET_DIR_INC, managed_queues,
                               count_managed_queues);
  if (ret < 0) {
    return CommandFailure(-ret);
  }

  if (cdq_) {
    doorbell_queue = managed_queues[0];
  } else {
    doorbell_queue = -1;
  }

  // register task
  tid = RegisterTask(nullptr);
  // tid = RegisterTask((void *)(uintptr_t)qid_);
  if (tid == INVALID_TASK_ID)
    return CommandFailure(ENOMEM, "Context creation failed");

  // initialize q_status
  for (int i = 0; i < MAX_QUEUES_PER_DIR; i++) {
    q_status_[i] = (queue_pause_status){
      until : 0,
      failed_ctrl : 0,
      remaining_dpkt : 0,
    };
  }

  int number_of_8bytes = MAX_QUEUES_PER_DIR / 64;
  if (count_managed_queues  % 64 != 0) number_of_8bytes++;
  overview_mask_ = reinterpret_cast<uint64_t *>(rte_zmalloc(nullptr, number_of_8bytes * 8, 0));
  count_overview_seg_ = number_of_8bytes;

  return CommandSuccess();
}

void BKDRFTQueueInc::DeInit() {
  if (port_) {
      port_->ReleaseQueues(reinterpret_cast<const module *>(this),
                           PACKET_DIR_INC, managed_queues,
                           count_managed_queues);
  }
}

std::string BKDRFTQueueInc::GetDesc() const {
  return bess::utils::Format("%s:%s/%s", port_->name().c_str(), "*",
                             port_->port_builder()->class_name().c_str());
}

inline bool BKDRFTQueueInc::IsQueuePausedInCache(Context *ctx, queue_t qid) {
  uint64_t now = ctx->current_ns;
  if (overlay_ && !Flow::EqualTo()(q_status_flows_[qid], empty_flow)) {
    auto &overlay_ctrl = BKDRFTOverlayCtrl::GetInstance();
    auto entry = overlay_ctrl.getOverlayEntry(q_status_flows_[qid]);
    if (entry != nullptr && entry->pause_until_ts > now)
      return true;
  }

  if (backpressure_ && !Flow::EqualTo()(q_status_flows_[qid], empty_flow)) {
    auto &pause_ctrl = BKDRFTSwDpCtrl::GetInstance();
    DpTblEntry entry = pause_ctrl.GetFlowStatus(q_status_flows_[qid]);
    if (entry.until > now)
      return true;
  }
  return false;
}

uint32_t BKDRFTQueueInc::ReadBatch(queue_t qid, bess::PacketBatch *batch,
                                   uint32_t burst) {
  Port *p = port_;
  batch->set_cnt(p->RecvPackets(qid, batch->pkts(), burst));
  const uint32_t cnt = batch->cnt();
  p->queue_stats[PACKET_DIR_INC][qid].requested_hist[burst]++;
  p->queue_stats[PACKET_DIR_INC][qid].actual_hist[cnt]++;
  p->queue_stats[PACKET_DIR_INC][qid].diff_hist[burst - cnt]++;

  uint64_t received_bytes = 0;
  for (uint32_t i = 0; i < cnt; i++) {
    received_bytes += batch->pkts()[i]->total_len();
  }

  if (!(p->GetFlags() & DRIVER_FLAG_SELF_INC_STATS)) {
    p->queue_stats[PACKET_DIR_INC][qid].packets += cnt;
    p->queue_stats[PACKET_DIR_INC][qid].bytes += received_bytes;
  }
  return cnt;
}

inline bool BKDRFTQueueInc::CheckQueuedCtrlMessages(__attribute__((unused))
                                                    Context *ctx,
                                                    queue_t *qid,
                                                    uint32_t *burst)
{

  // First come First server
  // if (doorbell_service_queue_.empty()) return false;
  // auto item = doorbell_service_queue_.front();
  // *qid = item.first;
  // *burst = item.second;
  // doorbell_service_queue_.pop();
  // // LOG(INFO) << "Selected queue: " << (int)*qid << " count: " << *burst << "\n";
  // return true;

  // Lowest Queue-id First
  // static int j = 0;
  // static int selected_bit = -1;
  int j = 0;
  int selected_bit = -1;
  int selected_queue = 0;
  uint64_t temp_mask;
  int begin = j;
  do {
    if (overview_mask_[j] > 0) {
      temp_mask = overview_mask_[j];
      // temp_mask &= UINT64_MAX << (selected_bit + 1);
      while (true) {
        selected_bit = __builtin_ffsl(temp_mask) - 1;
        if (selected_bit < 0) break;
        selected_queue = selected_bit + (j * 64);
        if (unlikely(IsQueuePausedInCache(ctx, selected_queue))) {
          temp_mask ^= 1L << selected_bit; // turn off selected_queue
          continue;
        }
        *qid = selected_queue;
        *burst = q_status_[selected_queue].remaining_dpkt;
        // LOG(INFO) << "selected_queue: " << selected_queue
        //           << " mask: " << overview_mask_[j] << "\n";
        return true; // found a queue
      }
    }
    // else {
    //   selected_bit = -1;
    // }

    j++;
    j %= count_overview_seg_;
  } while (j != begin);
  return false;
}

inline bool BKDRFTQueueInc::isManagedQueue(queue_t qid) {
  for (int i = 0; i < count_managed_queues; i++) {
    if (qid == managed_queues[i])
      return true;
  }
  return false;
}

uint32_t BKDRFTQueueInc::CDQ(Context *ctx,
    bess::PacketBatch *batch, queue_t &_qid) {
  // First check if there are data queues needed to be read from previous
  // control messages
  // If there are no data queues left from previous control messages then
  // read a batch from the command/ctrl queue.
  // If the command/ctrl queue is empty then nothing to do.
  // Else if the command/ctrl queue has some packets update the list of data
  // queues to be read and process one of them.

  bess::PacketBatch ctrl_batch;
  int res;
  uint32_t cnt;
  queue_t qid = 0;
  uint32_t burst = 0;
  bool found_qid = false;
  // bool found_qid = CheckQueuedCtrlMessages(ctx, &qid, &burst);

  if (!found_qid) {
    cnt = ReadBatch(doorbell_queue, &ctrl_batch, 32); // 32 is kMaxBurst
    if (cnt == 0) {
      // no ctrl msg. we are done!
      // return 0;
      goto check_queue_table;
    }

    // received some ctrl/commad message
    {
      queue_t dqid = 0;
      uint32_t nb_pkts = 0;
      char message_type;

      // a pointer to parsed protobuf object
      // static void *pb = rte_malloc(nullptr, sizeof(bess::pb::Ctrl), 0);
      //  TODO: for Overlay messages we should do something else!
      void *pb = &ctrl_pkt_pb_tmp_;

      bess::Packet *pkt;
      for (uint32_t i = 0; i < cnt; i++) {
        pkt = ctrl_batch.pkts()[i];
        res = parse_bkdrft_msg(pkt, &message_type, &pb);
        if (unlikely(res != 0)) {
          // LOG(WARNING) << "Failed to parse bkdrft msg\n";
          bess::Packet::Free(pkt); // free unknown packet
          continue;
        }

        if (message_type == BKDRFT_CTRL_MSG_TYPE) {
          bess::pb::Ctrl *ctrl_msg = reinterpret_cast<bess::pb::Ctrl *>(pb);
          dqid = static_cast<queue_t>(ctrl_msg->qid());
          nb_pkts = ctrl_msg->nb_pkts();
          // TODO: not checking isManagedQueue for performance testing
          // if (isManagedQueue(dqid))
          q_status_[dqid].remaining_dpkt += nb_pkts;
          // doorbell_service_queue_.push(std::pair<uint16_t, uint32_t>(dqid, nb_pkts));
          if (likely(q_status_[dqid].remaining_dpkt > 0)) {
            int index = dqid / 64;
            int bit = dqid % 64;
            overview_mask_[index] |= 1L << bit;
            // LOG(INFO) << "add q: " << (int)dqid << " index " << index << " bit "
            //           << bit << " mask " << overview_mask_[index] << "\n";
          }
          //
          // LOG(INFO) << "Received ctrl message: qid: " << (int)dqid
          //   << " count: " << nb_pkts << "\n";

          // not freeing ctrl_msg because we want it to be reused
          // delete ctrl_msg;
        } else if (message_type == BKDRFT_OVERLAY_MSG_TYPE) {
          bess::pb::Overlay *overlay_msg =
            reinterpret_cast<bess::pb::Overlay *>(pb);

          if (overlay_) {
            // uint64_t pps = overlay_msg->packet_per_sec();
            // LOG(INFO) << "Received overlay message: pps: " << pps << "\n";

            // update port rate limit for queue
            auto &overlay_ctrl = BKDRFTOverlayCtrl::GetInstance();
            overlay_ctrl.ApplyOverlayMessage(*overlay_msg, ctx->current_ns);
          }
          delete overlay_msg;
        } else {
          LOG(ERROR) << "Wrong message type!\n";
        }

        bess::Packet::Free(pkt); // free ctrl pkt
      }
    }
check_queue_table:
    found_qid = CheckQueuedCtrlMessages(ctx, &qid, &burst);
  }

  if (likely(found_qid)) {
    /* important note:
     * if burst is less than a specific number (I guess 4) the packets are note
     * read from the port.
     */
    uint32_t cnt = ReadBatch(qid, batch, max_burst); // burst
    // if (port_->getConnectedPortType() == NIC && cnt > 0) {
    //   LOG(INFO) << "FOUND qid: " << (int)qid << " burst: " << burst << "\n";
    //   LOG(INFO) << "Read packets: " << cnt << "\n";
    // }

    /* important note:
     * ! if we do not read all the packets in the queue (I mean if we
     * simply forget about the packets we tried to read but failed).
     * very quickly (but not always) the data queue gets field with packets
     * and sender can not send any packet and hence no ctrl message is send
     * either. as a result a dead lock (live lock) happens. But what if the
     * sender misinformed us or the packets were dropped in the network (e.g. in
     * the switches).
     */
    q_status_[qid].remaining_dpkt -= cnt;
    if (q_status_[qid].remaining_dpkt <= 0) {
      int index = qid / 64;
      int bit = qid % 64;
      overview_mask_[index] &= ~(1L << bit);
      // LOG(INFO) << "del q: " << (int)qid << " index " << index << " bit " <<
      // bit << " mask " << overview_mask_[index] << "\n";
    }

    // Go through all packets for finding ARP?!
    // We can group packets here, like having queues (buffers) here
    // Now that we are doing this we can do some optimization
    // We could even merge with queue out for ultimate performance (maybe?)
    // int ret;
    // for (uint32_t i = 0; i < cnt; i++) {
    //   bess::Packet *pkt = batch->pkts()[0];
    //   Ethernet *eth = pkt->head_data<Ethernet *>();
    //   Ipv4 *ip_hdr = get_ip_header(eth);
    //   if (ip_hdr != nullptr && ip_hdr->protocol == BKDRFT_ARP_IP_PROTO) {
    //     eth->ether_type = be16_t(Ethernet::Type::kArp);
    //     ret = remove_ip_header(pkt);
    //     if (ret < 0) {
    //       // TODO: what to do?
    //     }
    //   }
    // }

    // TODO: this code snipt should be here?
    if (cnt && overlay_) {
      // TODO: sampling is not the correct way (but what about flow caching?)
      auto &OverlayMan = bess::bkdrft::BKDRFTOverlayCtrl::GetInstance();
      bess::Packet *pkt = batch->pkts()[0];
      q_status_flows_[qid] = bess::bkdrft::PacketToFlow(*pkt);
      uint64_t pause_ts = OverlayMan.FillBook(port_, qid, q_status_flows_[qid]);
      q_status_[qid].until = pause_ts;
    }

    _qid = qid;
    return cnt;
  }
  return 0;
}

struct task_result
BKDRFTQueueInc::RunTask(Context *ctx, bess::PacketBatch *batch, void *) {
  Port *p = port_;

  if (!p->conf().admin_up) {
    return {.block = true, .packets = 0, .bits = 0};
  }

  queue_t qid = managed_queues[0];
  uint32_t cnt;
  uint64_t received_bytes = 0;

  const int burst = ACCESS_ONCE(burst_);
  const int pkt_overhead = 24;

  // static uint64_t last_print= 0;
  // uint64_t before = rte_get_timer_cycles();
  // uint64_t after;

  if (cdq_) {
    cnt = CDQ(ctx, batch, qid);
  } else {
    if (IsQueuePausedInCache(ctx, qid)) {
      return {.block = true, .packets = 0, .bits = 0};
    }
    cnt = ReadBatch(qid, batch, burst);
  }

  // after = rte_get_timer_cycles();
  // if (after - last_print >= rte_get_timer_hz()) {
  //   LOG(INFO) << "read packet cycles: " << after - before << "\n";
  //   last_print = after;
  // }

  if (cnt > 0) {
    // (commented out) only pause vhost the nic should not be paused (?)
    if (backpressure_) {
      // ! assume all the batch belongs to the same flow
      q_status_flows_[qid] = PacketToFlow(*(batch->pkts()[0]));

      // check if the data queue is paused
      // TODO: check BESS shared object class!
      BKDRFTSwDpCtrl &dropMan = bess::bkdrft::BKDRFTSwDpCtrl::GetInstance();
      DpTblEntry entry = dropMan.GetFlowStatus(q_status_flows_[qid]);
      if (entry.until != 0 && entry.until != q_status_[qid].until) {
        // LOG(INFO) << "Pause: " << FlowToString(q_status_flows_[qid]) << "\n";
        q_status_[qid].until = entry.until;

        uint32_t rate = entry.rate;
        if (rate < 32)
          rate = 32;

        p->limiter_.limit[PACKET_DIR_OUT][qid] = rate;
        p->limiter_.limit[PACKET_DIR_INC][qid] = rate;
      }
    }

    for (uint32_t i = 0; i < cnt; i++) {
      received_bytes += batch->pkts()[i]->total_len();
      if (prefetch_) {
        rte_prefetch0(batch->pkts()[i]->head_data());
      }
    }

    RunNextModule(ctx, batch);

    return {.block = false,
            .packets = cnt,
            .bits = (received_bytes + cnt * pkt_overhead) * 8};
  } else {
    // There is no  data pkts
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

CommandResponse BKDRFTQueueInc::CommandGetOverlayStats(
                                 const bess::pb::EmptyArg &)
{
  bess::pb::BKDRFTQueueIncCommandGetOverlayStatsResponse resp;
  for (size_t i = 0; i < port_->num_rx_queues(); i++) {
    resp.add_pkts(port_->queue_stats[PACKET_DIR_INC][i].overlay_packets);
    resp.add_duration(port_->queue_stats[PACKET_DIR_INC][i].overlay_duration);
  }
  // LOG(INFO) << "name: " << name_
  //           << " overlay throughput: " << "?"
  //           << "\n";
  return CommandSuccess(resp);
}

ADD_MODULE(BKDRFTQueueInc, "bkdrft_queue_inc",
           "receives packets from a port via a specific queue")
