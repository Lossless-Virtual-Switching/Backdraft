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

#ifndef BESS_MODULES_QUEUEINC_H_
#define BESS_MODULES_QUEUEINC_H_

#include "../module.h"
#include "../kmod/llring.h"
#include "../pb/module_msg.pb.h"
#include "../port.h"
#include "../utils/cuckoo_map.h"
#include <rte_hash_crc.h>
#include "../utils/ip.h"

#define FLOW_DEBUG 0


using bess::utils::Ipv4Prefix;
using bess::utils::CuckooMap;


class BKDRFTQueueInc final : public Module {
 public:
  static const gate_idx_t kNumIGates = 0;

  static const int kFlowQueueSize = 2048;  // initial queue size for a flow

  static const Commands cmds;

  BKDRFTQueueInc() :  Module(), 
                      port_(), 
                      qid_(), 
                      prefetch_(), 
                      burst_(), 
                      max_queue_size_(kFlowQueueSize) {}

  CommandResponse Init(const bess::pb::BKDRFTQueueIncArg &arg);
  void DeInit() override;

  struct task_result RunTask(Context *ctx, bess::PacketBatch *batch,
                             void *arg) override;

  std::string GetDesc() const override;

  struct FlowId {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint32_t src_port;
    uint32_t dst_port;
    uint8_t protocol;
  };

  // to compare two FlowId for equality in a hash table
  struct EqualTo {
    bool operator()(const FlowId &id1, const FlowId &id2) const {
      bool ips = (id1.src_ip == id2.src_ip) && (id1.dst_ip == id2.dst_ip);
      bool ports =
          (id1.src_port == id2.src_port) && (id1.dst_port == id2.dst_port);
      return (ips && ports) && (id1.protocol == id2.protocol);
    }
  };

  struct Flow {
    FlowId id;                  // allows the flow to remove itself from the map
    struct llring *queue;       // queue to store current packets for flow
    bess::Packet *next_packet;  // buffer to store next packet from the queue.
    Flow() : id(), next_packet(nullptr){};
    Flow(FlowId new_id)
        : id(new_id), next_packet(nullptr){};
    ~Flow() {
      if (queue) {
        bess::Packet *pkt;
        while (llring_sc_dequeue(queue, reinterpret_cast<void **>(&pkt)) == 0) {
          bess::Packet::Free(pkt);
        }

        std::free(queue);
      }

      if (next_packet) {
        bess::Packet::Free(next_packet);
      }
    }
  };


  // hashes a FlowId
  struct Hash {
    // a similar method to boost's hash_combine in order to combine hashes
    inline void combine(std::size_t &hash, const unsigned int &val) const {
      std::hash<unsigned int> hasher;
      hash ^= hasher(val) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    }
    bess::utils::HashResult operator()(const FlowId &id) const {
      std::size_t hash = 0;
      combine(hash, id.src_ip);
      combine(hash, id.dst_ip);
      combine(hash, id.src_port);
      combine(hash, id.dst_port);
      combine(hash, (uint32_t)id.protocol);
      return hash;
    }
  };

  CuckooMap<FlowId, Flow *, Hash, EqualTo> flows_;

  CommandResponse CommandSetBurst(
      const bess::pb::BKDRFTQueueIncCommandSetBurstArg &arg);

  //  Puts the packet into the llring queue within the flow. Takes the flow to
  //  enqueue the packet into, the packet to enqueue into the flow's queue
  //  and integer pointer to be set on error.
  void Enqueue(Flow *f, bess::Packet *pkt, int *err);

  //  Takes a Packet to get a flow id for. Returns the 5 element identifier for
  //  the flow that the packet belongs to
  FlowId GetId(bess::Packet *pkt);

  //  Creates a new flow and adds it to the round robin queue. Takes the first
  //  pkt
  //  to be enqueued in the new flow, the id of the new flow to be created and
  //  integer pointer to set on error.
  void AddNewFlow(bess::Packet *pkt, FlowId id, int *err);

  //  Removes the flow from the hash table and frees all the packets within its
  //  queue. Takes the pointer to the flow to remove
  void RemoveFlow(Flow *f);

  //  allocates llring queue space and adds the queue to the specified flow with
  //  size indicated by slots. Takes the Flow to add the queue, the number
  //  of slots for the queue to have and the integer pointer to set on error.
  //  Returns a llring queue.
  llring *AddQueue(uint32_t slots, int *err);

  //  Per flow queuing!
  void BKDRFTQueueInc::PerFlowQueuing(bess::PacketBatch *batch);

  //  This function determines what packets should be handed to the next
  //  modules
  bess::PacketBatch *batch BKDRFTQueueInc::BuildaBatch();


 private:
  Port *port_;
  queue_t qid_;
  int prefetch_;
  int burst_;
  uint32_t max_queue_size_;
  llring *flow_ring_;   // llring used for dummy round robin.
};

#endif  // BESS_MODULES_QUEUEINC_H_
