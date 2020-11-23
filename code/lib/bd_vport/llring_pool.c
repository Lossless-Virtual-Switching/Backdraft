#include <rte_malloc.h>
#include "llring_pool.h"

#define SINGLE_PRODUCER 1
#define SINGLE_CONSUMER 1
#define ROUND_TO_64(x) ((x + 32) & (~0x3f))

struct llring_pool *new_llr_pool(uint32_t num_llr, uint16_t count_slots)
{
  uint64_t bytes_per_llr_seg;
  uint64_t llring_region_memory;
  uint64_t size_pool_struct;
  uint64_t memory_bytes;
  void *ptr;
  struct llring_pool *pool;
  struct llring *llr;
  struct llr_seg *seg;
  uint32_t i;
  uint16_t water_mark = (count_slots >> 3) * 7;

  // Allocate enough memory for the pool, llrings and stack
  bytes_per_llr_seg = llring_bytes_with_slots(count_slots) +
    sizeof(struct list_head);
  bytes_per_llr_seg = ROUND_TO_64(bytes_per_llr_seg);
  llring_region_memory =  bytes_per_llr_seg * num_llr;
  size_pool_struct = ROUND_TO_64(sizeof(struct llring_pool));
  memory_bytes =  size_pool_struct +                       // pool struct
    llring_region_memory +                                 // llring
    ROUND_TO_64(sizeof(struct llr_seg *)) * num_llr;       // stack

  // Pool
  ptr = rte_malloc(NULL, memory_bytes, 64);
  pool = ptr;
  pool->num_llrings = num_llr;
  pool->stack_pointer = 0;
  pool->available_llr = num_llr;
  pool->llr_size = count_slots;

  // LLRings
  ptr += size_pool_struct;
  pool->memory = ptr;

  // Stack
  ptr += llring_region_memory;
  pool->ptr_stack = ptr;

  // Initialize llrings and stack
  ptr = (void *)pool->memory;
  for (i = 0; i < num_llr; i++) {
    seg = ptr;
    llr = &seg->ring;
    llring_init(llr, count_slots, SINGLE_PRODUCER, SINGLE_CONSUMER);
    llring_set_water_mark(llr, water_mark);
    pool->ptr_stack[i] = seg;
    ptr += bytes_per_llr_seg;
  }

  return pool;
}

void free_llr_pool(struct llring_pool *pool)
{
  rte_free(pool);
}

struct llr_seg *pull_llr(struct llring_pool *pool)
{
  if (pool->available_llr == 0)
    return NULL;
  struct llr_seg *seg = pool->ptr_stack[pool->stack_pointer];
  pool->stack_pointer++;
  pool->available_llr--;
  return seg;
}

int push_llr(struct llring_pool *pool, struct llr_seg *seg)
{
  // TODO: verify the llr pointer is valid
  if (seg == NULL)
    return -1;
  if (pool->available_llr >= pool->num_llrings)
    return -2; // there is probably a bug in user application

  pool->stack_pointer--;
  pool->ptr_stack[pool->stack_pointer] = seg;
  pool->available_llr++;
  return 0;
}
