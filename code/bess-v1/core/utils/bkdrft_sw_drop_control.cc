#include "bkdrft_sw_drop_control.h"
// #include "time.h"

namespace bess {
namespace bkdrft {

// This functions pauses the incoming queue
// of the given flow.
// TODO: what if some flows share the same queue
// TODO: what if the flow change its queue
void BKDRFTSwDpCtrl::PauseFlow(uint64_t ts, uint64_t rate, const Flow &flow) {
  // LOG(INFO) << "Pause flow\n";

  auto entry = flowBook_.Find(flow);

  if (entry == nullptr) {
    flowBook_.Insert(flow, {
                     .until = ts,
                     .rate = rate,
                 });
  } else {
    entry->second.until = ts;
    entry->second.rate = rate;
  }
}

struct DpTblEntry BKDRFTSwDpCtrl::GetFlowStatus(Flow &flow) {
  auto entry = flowBook_.Find(flow);
  // LOG(INFO) << "get flow status"<< flow.addr_dst.value() << "\n";
  if (entry == nullptr) {
    return (DpTblEntry) {
         .until = 0,
         .rate = 0,
    };
  } else {
    return entry->second;
  }
}

// This function determines until when the queue should
// be paused
// uint64_t BKDRFTSwDpCtrl::GetIncQueueStatus(uint16_t qid)
// {
// 	auto entry = incQBook_.Find(qid);
// 	if (entry == nullptr)
// 		return 0;
// 	uint64_t t =  entry->second.until;
// 	uint64_t now = rdtsc();
// 	now  = tsc_to_ns(now);  // nano seconds
// 	if ( t < now) {
// 		// TODO: check if updating struct updates the map
// 		entry->second.until = 0;
// 		return 0;
// 	}
// 	return t;
// }

// void BKDRFTSwDpCtrl::ObserveIncFlow(uint16_t qid, const Flow &flow)
// {
// 	flowCache_.ObserveIncFlow(qid, flow);
// }

} // namespace bkdrft
} // namespace bess
