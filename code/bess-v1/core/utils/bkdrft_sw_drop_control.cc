#include "bkdrft_sw_drop_control.h"
// #include "time.h"

namespace bess {
	namespace bkdrft {

		// This functions pauses the incoming queue
		// of the given flow.
		// TODO: what if some flows share the same queue
		// TODO: what if the flow change its queue
		void BKDRFTSwDpCtrl::PauseFlow(uint64_t ts,
				const Flow &flow)
		{
			// LOG(INFO) << "Pause flow\n";

			// uint64_t now;
			auto entry = flowBook_.Find(flow);

			// now = rdtsc();
			// now  = tsc_to_ns(now);  // nano seconds
			
			if (entry == nullptr) {
				flowBook_.Insert(flow, {
					.until = ts,
					});
			} else {
				entry->second.until = ts;
			}

			// uint16_t qid;
			// if (flowCache_.GetFlowIncQueue(flow, qid)) {
			// 	auto entry = incQBook_.Find(qid);
			// 	uint64_t now = rdtsc();
			// 	now  = tsc_to_ns(now);  // nano seconds
			// 	if (entry == nullptr) {
			// 		incQBook_.Insert(qid, {
			// 				.until = now + duration,
			// 				});
			// 	} else {
			// 		// TODO: check if updating struct updates the map
			// 		entry->second.until = now + duration;
			// 	}
			// } else {
			// 	// cache miss what to do now?!
			// 	// should not happen
			// }

		}

		uint64_t BKDRFTSwDpCtrl::GetFlowStatus(Flow &flow)
		{
			uint64_t now = 0;
			uint64_t until;
			auto entry = flowBook_.Find(flow);
			// LOG(INFO) << "get flow status"<< flow.addr_dst.value() << "\n";
			if (entry == nullptr) {
				return 0;
			} else {
				// now = rdtsc();
				// now  = tsc_to_ns(now);  // nano seconds
				until = entry->second.until;
				// LOG(INFO) << "until (" << until << "), now (" << now << ");\n";
				if (until < now) {
					entry->second.until = 0;
					return 0;
				}
				return until;
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

	} // bkdrft namespace
} // bess namespace
