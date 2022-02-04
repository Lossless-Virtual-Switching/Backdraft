// Copyright (c) 2014-2016, The Regents of the University of California.
// Copyright (c) 2016-2017, Nefeli Networks, Inc.
// All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// * Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
// * Neither the names of the copyright holders nor the names of their
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include "bd_vport.h"

#include <rte_bus_pci.h>
#include <rte_ethdev.h>
#include <rte_flow.h>

#include "../utils/ether.h"
#include "../utils/format.h"

/*!
 * The following are deprecated. Ignore us.
 */
#define SN_TSO_SG 0
#define SN_HW_RXCSUM 0
#define SN_HW_TXCSUM 0

void BDVPort::InitDriver() {
}

CommandResponse BDVPort::Init(const bess::pb::BDVPortArg &arg) {
  int num_txq = num_queues[PACKET_DIR_OUT];
  int num_rxq = num_queues[PACKET_DIR_INC];

  int numa_node = -1;

  std::string port_name;

  port_name = arg.port_name();

  if (arg.rate_limiting()) {
    conf_.rate_limiting = true;
    uint32_t rate = arg.rate() * 1000; // 1000000;

    if (!rate)
      return CommandFailure(ENOENT, "rate not found");

    for (queue_t qid = 0; qid < MAX_QUEUES_PER_DIR; qid++) {
      limiter_.limit[PACKET_DIR_OUT][qid] = rate;
      limiter_.limit[PACKET_DIR_INC][qid] = rate;
    }

    LOG(INFO) << "Rate limiting on " << rate << " (pps) \n";
  }

  // setup a new vport structure and pipes make sure the
  // port_name is unique.
  LOG(INFO) << "Creating BDVPort " << port_name << "\n";
  int poolsize = (num_rx_queues() + num_tx_queues()) * 8 + 64;
  /* int qsize = rx_queue_size(); */
  port_ = _new_vport(port_name.c_str(), num_rxq, num_txq, poolsize,
      64);

  driver_ = "N/A";
  ptype_ = VPORT;

  node_placement_ =
      numa_node == -1 ? UNCONSTRAINED_SOCKET : (1ull << numa_node);

  // Reset hardware stat counters, as they may still contain previous data
  CollectStats(true);

  return CommandSuccess();
}

CommandResponse BDVPort::UpdateConf(__attribute__((unused))const Conf &conf) {
  return CommandSuccess();
}

void BDVPort::DeInit() {
  free_vport(port_);
}

void BDVPort::CollectStats(__attribute__((unused)) bool reset) {
}

int BDVPort::RecvPackets(queue_t qid, bess::Packet **pkts, int cnt) {
  int recv = 0;
  uint32_t allowed_packets = 0;

  // if (ptype_ == VHOST && conf_.rate_limiting && qid)
  if (conf_.rate_limiting) {
    allowed_packets = RateLimit(PACKET_DIR_INC, qid);
    if (allowed_packets == 0) {
      // shouldn't read any packets; this affect the rate so update the rate;
      RecordRate(PACKET_DIR_INC, qid, pkts, 0);
      return 0;
    }

    if (allowed_packets < (uint32_t)cnt) {
      cnt = allowed_packets;
    }

    recv = recv_packets_vport(port_, qid, (void **)pkts, cnt);

    UpdateTokens(PACKET_DIR_INC, qid, recv);
  } else {
    recv = recv_packets_vport(port_, qid, (void **)pkts, cnt);
  }

  RecordRate(PACKET_DIR_INC, qid, pkts, recv);
  return recv;
}

int BDVPort::SendPackets(queue_t qid, bess::Packet **pkts, int cnt) {
  int sent;
  sent = send_packets_vport(port_, qid, (void**)pkts, cnt);
  RecordRate(PACKET_DIR_OUT, qid, pkts, sent);
  return sent;
}

Port::LinkStatus BDVPort::GetLinkStatus() {
  return LinkStatus{.speed = 0U,
                    .full_duplex = true,
                    .autoneg = false,
                    .link_up = 0U}; // TODO: keep track of connected apps
                                       // in vport.
}

ADD_DRIVER(BDVPort, "bdvport_port", "Backdraft VPort")
