#ifndef _BKDRFT_H
#define _BKDRFT_H

#define BKDRFT_CTRL_QUEUE (0)

namespace bess {
namespace bkdrft {

struct ctrl_pkt {
	uint32_t bytes;
	uint16_t nb_pkts;
	uint16_t q;
};

static_assert (sizeof(ctrl_pkt) == 8, "ctrl_pkt size is not okay");

}
}

#endif
