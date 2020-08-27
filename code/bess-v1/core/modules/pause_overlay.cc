#include "pause_overlay.h"

#include "../utils/format.h"
#include "../utils/flow.h"

CommandResponse PauseOverlay::Init(__attribute__((unused)) const bess::pb::PauseOverlayArg &arg) {
  return CommandSuccess();
}

void PauseOverlay::DeInit() {
}

std::string PauseOverlay::GetDesc() const {
  return bess::utils::Format("BKDRFT Pause Overlay");
}

void PauseOverlay::ProcessBatch(Context *ctx, bess::PacketBatch *batch) {
  bess::Packet *pkt;
  pkt = current_worker.packet_pool()->Alloc();
  if (pkt != nullptr) {
    bess::bkdrft::BKDRFTOverlayCtrl &OverlayMan =
      bess::bkdrft::BKDRFTOverlayCtrl::GetInstance();
    bess::bkdrft::Flow f = bess::bkdrft::PacketToFlow(*batch->pkts()[0]);
    // TODO: check if high water mark has been passed
    // TODO: keep a moving average (or exponentialy moving average) of queue length
    // pps is set to 0 just to silent the compile error, this module is just for testing purposes. pps was added later.
    OverlayMan.SendOverlayMessage(f, pkt, 0, 0);
  }
  RunNextModule(ctx, batch); 
}

ADD_MODULE(PauseOverlay, "pause_overlay",
           "Generates pause overlay message for a flow, if needed!")
