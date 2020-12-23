#include <stdio.h>
#include <unistd.h>
#include "bkdrft.h"
#include "bkdrft_const.h"
#include "exp.h"

#include <rte_ether.h>
#include <rte_arp.h>
#include <rte_ip.h>

int str_to_ip(const char *str, uint32_t *addr)
{
  uint8_t a, b, c, d;
  if(sscanf(str, "%hhu.%hhu.%hhu.%hhu", &a, &b, &c, &d) != 4) {
    return -1;
  }

  *addr = MAKE_IP_ADDR(a, b, c, d);
  return 0;
}

void ip_to_str(uint32_t addr, char *str, uint32_t size) {
  uint8_t *bytes = (uint8_t *)&addr; 
  snprintf(str, size, "%hhu.%hhu.%hhu.%hhu", bytes[3], bytes[2], bytes[1], bytes[0]);
}
