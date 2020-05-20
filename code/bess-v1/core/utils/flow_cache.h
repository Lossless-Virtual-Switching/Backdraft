#ifndef _BKDRFT_FLOW_CACHE_
#define _BKDRFT_FLOW_CACHE_

#include "flow.h"
#include "cuckoo_map.h"

namespace bess {
	namespace bkdrft {

		struct CachedData {
			uint16_t qid;
		};

		class FlowCache final
		{
			public:
				// Observe data about a flow and 
				// cache (or update cache) if needed
				void ObserveIncFlow(const uint16_t qid, const Flow &flow);

				// qid parameter is updated if cache hit 
				bool GetFlowIncQueue(const Flow &flow, uint16_t &qid) const;

			private:
				using HashTable = bess::utils::CuckooMap<Flow,
							CachedData, Flow::Hash, Flow::EqualTo>;

				HashTable flowIncBook_;
		};
	}  // bkdrft namesapce
} // bess namespace
#endif // _BKDRFT_FLOW_CACHE_

