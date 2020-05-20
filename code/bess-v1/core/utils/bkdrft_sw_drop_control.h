#ifndef _BKDRFT_SW_DROP_CONTROL_
#define _BKDRFT_SW_DROP_CONTROL_

#include <rte_hash_crc.h>

#include "cuckoo_map.h"
#include "flow.h"
#include "flow_cache.h"

namespace bess {
	namespace bkdrft {

		struct DpTblEntry {
			uint64_t until;  // pause until this timestamp
		};

		static_assert(sizeof(DpTblEntry) == 8, "Size of DpTblEntry is wrong");

		class BKDRFTSwDpCtrl final
		{
			// This class is a singleton
			private:
				BKDRFTSwDpCtrl() {}

			public:
				// Access to the class instance with this method	
				static BKDRFTSwDpCtrl& GetInstance()
				{
					static BKDRFTSwDpCtrl s_instance_;
					return s_instance_;
				}

				BKDRFTSwDpCtrl(BKDRFTSwDpCtrl const&) = delete;

				void operator=(BKDRFTSwDpCtrl const&) = delete;

			private:
				using HashTable = bess::utils::CuckooMap<Flow,
					DpTblEntry, Flow::Hash, Flow::EqualTo>;
				HashTable flowBook_;
				FlowCache flowCache_;

			public:
				void PauseFlow(uint16_t duration, const Flow &flow);

				// uint64_t GetIncQueueStatus(uint16_t qid);

				uint64_t GetFlowStatus(Flow &flow);

				// void ObserveIncFlow(uint16_t qid, const Flow &flow);
		};

	}  // bkdrft namespace
} // bess namesapce
#endif  // _BKDRFT_SW_DROP_CONTROL_
