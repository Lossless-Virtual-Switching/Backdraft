#ifndef _LLRING_POOL_H
#define _LLRING_POOL_H
#include "llring.h"
#include "list.h"

struct llr_seg {
  struct llring ring;
  struct list_head list;
};

struct llring_pool {
  // continues memory for llring pool
  struct llr_seg *memory;
  struct llr_seg **ptr_stack; // stack of pointers to llrings
  uint32_t num_llrings; // capacity of the pool
  uint32_t stack_pointer;
  uint32_t available_llr; // number of llrings availabe in the pool
  uint32_t llr_size; // number of slots for each llring
};

// void init_llr_pool(uint32_t num_llr, uint32_t count_slots,
//                    struct llring_pool *pool);
struct llring_pool *new_llr_pool(uint32_t num_llr, uint16_t count_slots);
void free_llr_pool(struct llring_pool *pool);
struct llr_seg *pull_llr(struct llring_pool *pool);
int push_llr(struct llring_pool *pool, struct llr_seg *llr);

#endif
