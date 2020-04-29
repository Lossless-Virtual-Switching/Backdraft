#ifndef _BKDRFT_H
#define _BKDRFT_H

#define BKDRFT_CTRL_QUEUE (0)

struct ctrl_pkt {
	uint32_t bytes;
	uint16_t nb_pkts;
	uint8_t q;
};

#endif
