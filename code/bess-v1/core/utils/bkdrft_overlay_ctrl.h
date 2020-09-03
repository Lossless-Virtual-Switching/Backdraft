#ifndef _BKDRFT_OVERLAY_CTRL_
#define _BKDRFT_OVERLAY_CTRL_

#include <rte_hash_crc.h>

#include "cuckoo_map.h"
#include "flow.h"
// #include "flow_cache.h"
#include "../port.h"
#include "../pb/bkdrft_msg.pb.h"

namespace bess {
namespace bkdrft {

#define MAX_TEMPLATE_SIZE 1536
#define DEFAULT_WINDOW_SIZE 32

struct OverlayEntry {
  Port * port; 
  queue_t qid;
  bool block;
  uint64_t pause_until_ts;
  uint32_t window;
};

class BKDRFTOverlayCtrl final {
  // This class is a singleton
 private:
  BKDRFTOverlayCtrl() {}

 public:
  // Access to the class instance with this method
  static BKDRFTOverlayCtrl &GetInstance() {
    static BKDRFTOverlayCtrl s_instance_;
    return s_instance_;
  }

  BKDRFTOverlayCtrl(BKDRFTOverlayCtrl const &) = delete;
  void operator=(BKDRFTOverlayCtrl const &) = delete;

 private:
  using HashTable =
      bess::utils::CuckooMap<Flow, OverlayEntry, Flow::Hash, Flow::EqualTo>;
  HashTable flowBook_;


 public:
  int SendOverlayMessage(const Flow &f, Packet * pkt, uint64_t pps,
		  uint64_t pause_duration);
  uint64_t FillBook(Port * port, queue_t qid, const Flow &flow);
  void ApplyOverlayMessage(bess::pb::Overlay &overlay_msg, uint64_t current_ns);

  static void FillOverlayPacket(const Flow &f, Packet *pkt, uint64_t pps,
		  uint64_t puase_duration);
  static void ReadOverlayPacket(Packet *pkt); // not sure 
  static int ExtractFlow(bess::pb::Overlay overlay_msg, Flow &flow);

  const OverlayEntry *getOverlayEntry(const Flow &f);
};

}  // namespace bkdrft
}  // namespace bess
#endif  // _BKDRFT_OVERLAY_CTRL_
