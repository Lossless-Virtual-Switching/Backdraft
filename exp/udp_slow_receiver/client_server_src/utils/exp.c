#include <stdio.h>
#include "../include/exp.h"

int str_to_ip(const char *str, uint32_t *addr)
{
	uint8_t a, b, c, d;
	if(sscanf(str, "%hhu.%hhu.%hhu.%hhu", &a, &b, &c, &d) != 4) {
		return -1;
	}

	*addr = MAKE_IP_ADDR(a, b, c, d);
	return 0;
}
