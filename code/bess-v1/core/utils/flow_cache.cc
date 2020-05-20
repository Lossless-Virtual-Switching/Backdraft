#include "flow_cache.h"

namespace bess {
	namespace bkdrft {

		void FlowCache::ObserveIncFlow(const uint16_t qid,
				const Flow &flow)
		{
			// TODO: make sure ObserveIncFlow is not called
			// without cautious (call only if need to update the 
			// cache.)
			CachedData data = {
				.qid = qid,
			};
			flowIncBook_.Insert(flow, data);
		}

		bool FlowCache::GetFlowIncQueue(const Flow &flow, uint16_t &qid) const
		{
			auto entry = flowIncBook_.Find(flow);
			if (entry == nullptr)
				return false;
			qid = entry->second.qid;
			return true;
		}
	} // bkdrft namespace
} // bess namespace
