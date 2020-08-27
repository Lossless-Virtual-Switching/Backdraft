# ifndef _UDP_CONFIG_H
# define _UDP_CONFIG_H
#include "include/exp.h"

const int mode_client = 0;
const int mode_server = 1;

const int system_bess = 0;
const int system_bkdrft = 1;

extern struct Context[2] = {
	// server context
	{
		.mode = mode_server,
		.system_mode = system_bess,

                .default_qid = 0,

		.delay = 0,
	},
};

# endif // _UDP_CONFIG_H
