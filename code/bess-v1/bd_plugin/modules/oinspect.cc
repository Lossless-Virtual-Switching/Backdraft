#include "oinspect.h"

#include "utils/ether.h"
#include "utils/ip.h"

const Commands OInspect::cmds = {
    {"set_overlay_broker", "CommandSetOverlayBrokerArg",
        MODULE_CMD_FUNC(&OInspect::CommandSetOverlayBroker), Command::THREAD_UNSAFE},
};

void OInspect::ProcessBatch(Context *ctx, bess::PacketBatch *batch) {
  using bess::utils::Ethernet;
  using bess::utils::Ipv4;

  bess::PacketBatch alternateBatch;
  // alternateBatch = ctx->task->AllocPacketBatch();

  int cnt = batch->cnt();

  for (int i = 0; i < cnt; i++) {
    bess::Packet *pkt;
    pkt = batch->pkts()[i];
    Ethernet *eth = pkt->head_data<Ethernet *>();
    Ipv4 *ip = reinterpret_cast<Ipv4 *>(eth + 1);

    if (ip->header_length == 6) {
      // probably backdraft
      bess::utils::be32_t * options = reinterpret_cast<bess::utils::be32_t *>(ip + 1);
      if (options->value() == 756) {
        LOG(INFO) << "over\n";
        
        // overload use ip lookup special HEY IP X has issue we should use dst ip
      }
      else if (options->value() == 755) {
        LOG(INFO) << "under\n";
        // underload use ip lookup special HEY IP X has issue we should use dst ip
      }
      
      bess::Packet::Free(pkt);
      continue;
    } 

    alternateBatch.add(pkt);
  }

  batch->clear();
  batch->add(&alternateBatch);

  RunNextModule(ctx, batch);
}

CommandResponse OInspect::CommandSetOverlayBroker(const bkdrft::pb::CommandSetOverlayBrokerArg &arg) {
  std::map<std::string, OBroker*>::iterator it;
  LOG(INFO) << arg.overlay_broker_mod() << std::endl;
  it = OBroker::all_brokers_.find(arg.overlay_broker_mod());
  if (it != OBroker::all_brokers_.end()) {
    overlay_broker_ = it->second;
    return CommandSuccess();
  }
  return CommandFailure(404, "didn't find the module");
}

ADD_MODULE(OInspect, "oinspect", "inspects backdraft overlay messages")
