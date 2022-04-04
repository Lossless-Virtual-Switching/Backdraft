#include "dpfq.h"

#include "port.h"
#include "utils/format.h"

#include "utils/endian.h"
#include "utils/ether.h"
#include "utils/ip.h"

// const Commands DPFQ::cmds = {};
//

int DPFQ::Resize(int slots) {
  // TODO: add parameter for count queue
  for (int q = 0; q < 2; q++) {
    struct llring *old_queue = queues_[q];
    struct llring *new_queue;
    int bytes = llring_bytes_with_slots(slots);

    new_queue =
      reinterpret_cast<llring *>(std::aligned_alloc(alignof(llring), bytes));
    if (!new_queue) {
      return -ENOMEM;
    }

    // multiple producer - single consumer
    int ret = llring_init(new_queue, slots, 0, 1);
    if (ret) {
      std::free(new_queue);
      return -EINVAL;
    }

    /* migrate packets from the old queue */
    if (old_queue) {
      bess::Packet *pkt;

      while (llring_sc_dequeue(old_queue, (void **)&pkt) == 0) {
        ret = llring_sp_enqueue(new_queue, pkt);
        if (ret == -LLRING_ERR_NOBUF) {
          bess::Packet::Free(pkt);
        }
      }

      std::free(old_queue);
    }

    queues_[q] = new_queue;
    size_ = slots;
  }
  return 0;
}

CommandResponse DPFQ::Init(__attribute__((unused))
    const bkdrft::pb::DPFQArg &arg) {
  task_id_t tid;
  CommandResponse err;
  tid = RegisterTask(nullptr);
  if (tid == INVALID_TASK_ID)
    return CommandFailure(ENOMEM, "Context creation failed");
  burst_ = bess::PacketBatch::kMaxBurst;

  int ret = Resize(1024);
  if (ret) {
    return CommandFailure(-ret);
  }

  return CommandSuccess();
}

void DPFQ::DeInit() {
  bess::Packet *pkt;

  // TODO: add parameter for count queue
  for (int q = 0; q < 2; q++) {
    if (queues_[q]) {
      while (llring_sc_dequeue(queues_[q], (void **)&pkt) == 0) {
        bess::Packet::Free(pkt);
      }
      std::free(queues_[q]);
    }
  }
}

std::string DPFQ::GetDesc() const {
  return bess::utils::Format("DPFQ");
}

struct task_result DPFQ::RunTask(Context *ctx, bess::PacketBatch *batch,
    __attribute__((unused)) void *arg) {
  const int burst = ACCESS_ONCE(burst_);
  const int pkt_overhead = 24;

  uint64_t total_bytes = 0;
  int dequeued = 0;

  // TODO: add parameter for count queue
  for (int q = 0; q < 2; q++) {
    struct llring * queue_ = queues_[q];
    uint32_t cnt = llring_sc_dequeue_burst(queue_,
        (void **)(batch->pkts() + dequeued), (burst - dequeued));
    if (cnt == 0) {
      continue;
    }

    stats_.dequeued += cnt;
    dequeued += cnt;
    for (uint32_t i = 0; i < cnt; i++) {
      total_bytes += batch->pkts()[i]->total_len();
    }

    if (dequeued >= burst) {
      break;
    }
  }

  batch->set_cnt(dequeued);
  RunNextModule(ctx, batch);
  return {.block = false,
    .packets = (unsigned) dequeued,
    .bits = (total_bytes + dequeued * pkt_overhead) * 8};
}

void DPFQ::ProcessBatch(__attribute__((unused)) Context *ctx, bess::PacketBatch *batch) {
  // TODO: if flow is not pause pass it to next module
  // pkt array ***
  // int size_q ***
  for (int i = 0; i < batch->cnt(); i++) {
    bess::Packet *pkt = batch->pkts()[i];
    int qid = HashPacket(pkt);
    if (qid < 0) {
      bess::Packet::Free(pkt);
      continue;
    }

    // if qid is paused ***
      int queued = llring_mp_enqueue_burst(queues_[qid], (void **)&pkt, 1);
    // else ***
    //   arr [size++] = pkt ***

    // TODO: per queue stat collection
    stats_.enqueued += queued;

    if (queued < 1) {
      stats_.dropped += 1;
      bess::Packet::Free(pkt);
    }
  }
  // clear batch ***
  // if ***
  // add to batch ***
}

int DPFQ::HashPacket(bess::Packet *pkt)
{
  using bess::utils::Ethernet;
  using bess::utils::Ipv4;
  Ethernet *eth = pkt->head_data<Ethernet *>();
  uint32_t ether_type = eth->ether_type.value();
  Ipv4 *ip;
  uint32_t ip_dst;

  if (ether_type == Ethernet::Type::kIpv4) {
    ip =reinterpret_cast<Ipv4 *>(eth + 1);
    ip_dst = ip->dst.value();
    // TODO: use number of available queues
    return ip_dst % 2;
  }
  return -1;
}

void DPFQ::SignalOverloadBP(__attribute__((unused)) bess::Packet *pkt) {
  // TODO: what to do when receiving overload signal
}

void DPFQ::SignalUnderloadBP(__attribute__((unused)) bess::Packet *pkt) {
  // TODO: what to do when receiving underload signal
}

ADD_MODULE(DPFQ, "dpfq", "dynamic per flow queuing")
