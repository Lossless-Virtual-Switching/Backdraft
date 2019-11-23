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
#include "../utils/ether.h"
#include "../utils/ip.h"
#include "../utils/udp.h"

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
  tid = RegisterTask((void *)(uintptr_t)qid_);
  if (tid == INVALID_TASK_ID)
    return CommandFailure(ENOMEM, "Context creation failed");

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

void BKDRFTQueueInc::PerFlowQueuing(bess::PacketBatch *batch) {
/* Let's manage the flows here to see what will happen
   * in near future!
   */
  int err = 0;

  // It can be avoided but we aren't at that level of performance optimization
  uint32_t cnt = batch->cnt(); 

  for (uint32_t i = 0; i < cnt; i++) {
#if FLOW_DEBUG
      if(i == 0)
        LOG(INFO) << "cnt read packets " << cnt << "\n";  
#endif
    bess::Packet *pkt = batch->pkts()[i];
    FlowId id = GetId(pkt);
    auto it = flows_.Find(id);

    // if the Flow doesn't exist create one
    // and add the packet to the new Flow
    if (it == nullptr) {
#if FLOW_DEBUG
      LOG(INFO) << "New Flow " << id.src_ip << "\n";
#endif
      AddNewFlow(pkt, id, &err);
      assert(err == 0);
    } else {
#if FLOW_DEBUG
      LOG(INFO) << "ENQUEUED " << id.src_ip << "\n";
#endif
      Enqueue(it->second, pkt, &err);
      assert(err == 0);
    }
  }
  // End of packet flow accounting!
}

struct task_result BKDRFTQueueInc::RunTask(Context *ctx, bess::PacketBatch *batch,
                                     void *arg) {
  Port *p = port_;
  int err = 0;


  if (!p->conf().admin_up) {
    return {.block = true, .packets = 0, .bits = 0};
  }

  // if (children_overload_ > 0) {
  //   // Alireza
  //   // Not sure if I should send a pause message now!
  //   // p->OverloadSignal();
  //   return {
  //       .block = true,
  //       .packets = 0,
  //       .bits = 0,
  //   };
  // }

  const queue_t qid = (queue_t)(uintptr_t)arg;

  uint64_t received_bytes = 0;

  const int burst = ACCESS_ONCE(burst_);
  const int pkt_overhead = 24;

  batch->set_cnt(p->RecvPackets(qid, batch->pkts(), burst));
  uint32_t cnt = batch->cnt();

  PerFlowQueuing(batch); // If you remove this! Nothing happens :)

  p->queue_stats[PACKET_DIR_INC][qid].requested_hist[burst]++;
  p->queue_stats[PACKET_DIR_INC][qid].actual_hist[cnt]++;
  p->queue_stats[PACKET_DIR_INC][qid].diff_hist[burst - cnt]++;
  if (cnt == 0) {
    return {.block = true, .packets = 0, .bits = 0};
  }

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

  // Before passing the batch for the next module I belive I need to create
  // My batch according to the Flow accounting! I'm not sure though. Need to
  // talk to Brent. So I just want ot put a function here to create my new batch.
  // It sounds stupid tho! Why would be go that way?
  
  //batch = BuildaBatch();

  RunNextModule(ctx, batch);

  // if (cnt < (uint32_t)burst_/2) {
  //   SignalUnderload();
  //   // Just send an unpause message!
  //   // I should create some pause message packets!
  //   // And I don't still know how!!
  //   // p->RecvPackets(0, batch->pkts(), 1)
  // }

  return {.block = false,
          .packets = cnt,
          .bits = (received_bytes + cnt * pkt_overhead) * 8};
}


BKDRFTQueueInc::FlowId BKDRFTQueueInc::GetId(bess::Packet *pkt) {
  using bess::utils::Ethernet;
  using bess::utils::Ipv4;
  using bess::utils::Udp;

  Ethernet *eth = pkt->head_data<Ethernet *>();
  Ipv4 *ip = reinterpret_cast<Ipv4 *>(eth + 1);
  size_t ip_bytes = ip->header_length << 2;
  Udp *udp = reinterpret_cast<Udp *>(reinterpret_cast<uint8_t *>(ip) +
                                     ip_bytes);  // Assumes a l-4 header
  // TODO(joshua): handle packet fragmentation
  FlowId id = {ip->src.value(), ip->dst.value(), udp->src_port.value(),
              udp->dst_port.value(), ip->protocol};
  return id;
}

void BKDRFTQueueInc::AddNewFlow(bess::Packet *pkt, FlowId id, int *err) {
  // creates flow
  Flow *f = new Flow(id);

  // TODO(joshua) do proper error checking
  f->queue = AddQueue(static_cast<int>(kFlowQueueSize), err);

  if (*err != 0) {
    return;
  }

  flows_.Insert(id, f);

  Enqueue(f, pkt, err);
  if (*err != 0) {
    return;
  }

  *err = llring_enqueue(flow_ring_, f);
}

llring *BKDRFTQueueInc::AddQueue(uint32_t slots, int *err) {
  int bytes = llring_bytes_with_slots(slots);
  int ret;

  llring *queue = static_cast<llring *>(aligned_alloc(alignof(llring), bytes));
  if (!queue) {
    *err = -ENOMEM;
    return nullptr;
  }

  ret = llring_init(queue, slots, 1, 1);
  if (ret) {
    std::free(queue);
    *err = -EINVAL;
    return nullptr;
  }
  return queue;
}

void BKDRFTQueueInc::RemoveFlow(Flow *f) {
  // if (f == current_flow_) {
  //   current_flow_ = nullptr;
  // }
  flows_.Remove(f->id);
  delete f;
}

void BKDRFTQueueInc::Enqueue(Flow *f, bess::Packet *newpkt, int *err) {
  // if the queue is full. drop the packet.
  if (llring_count(f->queue) >= max_queue_size_) {
    bess::Packet::Free(newpkt);
    return;
  }

  // creates a new queue if there is not enough space for the new packet
  // in the old queue
  // if (llring_full(f->queue)) {
  //   // uint32_t slots =
  //   //     RoundToPowerTwo(llring_count(f->queue) * kQueueGrowthFactor);
  //   // f->queue = ResizeQueue(f->queue, slots, err);
  //   // if (*err != 0) {
  //   //   bess::Packet::Free(newpkt);
  //   //   return;
  //   // }
  //   // I just convined myself that just drop the packet is 
  //   // case queue in full
  // }

  *err = llring_enqueue(f->queue, reinterpret_cast<void *>(newpkt));
  if (*err == 1) {
    bess::Packet::Free(newpkt);
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
