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
	// static uint64_t max_ = 0;
  if (entry == nullptr) {
    return (DpTblEntry) {
         .until = 0,
         .rate = 0,
    };
  } else {
		// if (max_ < entry->second.until)
		// 	max_ = entry->second.until;
    return entry->second;
  }
}

} // namespace bkdrft
} // namespace bess
