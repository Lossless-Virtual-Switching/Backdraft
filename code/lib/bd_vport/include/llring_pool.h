#ifndef _LLRING_POOL_H
#define _LLRING_POOL_H
#include <rte_stack.h>
#include "llring.h"
#include "list_types.h"

struct llr_seg {
  struct list_head list;
  struct llring ring;
};

struct llring_pool {
  // continues memory for llring pool
  struct llr_seg *memory;
  struct rte_stack *ptr_stack;
  uint32_t num_llrings; // capacity of the pool
  uint32_t llr_size; // number of slots for each llring
};

// void init_llr_pool(uint32_t num_llr, uint32_t count_slots,
//                    struct llring_pool *pool);
struct llring_pool *new_llr_pool(const char *name, int socket_id,
                                 uint32_t num_llr, uint16_t count_slots);
void free_llr_pool(struct llring_pool *pool);
struct llr_seg *pull_llr(struct llring_pool *pool);
int push_llr(struct llring_pool *pool, const struct llr_seg *llr);

#endif
