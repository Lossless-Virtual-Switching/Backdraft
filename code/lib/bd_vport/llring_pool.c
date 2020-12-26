#include <assert.h>
#include <rte_malloc.h>
#include "llring_pool.h"

#define SINGLE_PRODUCER 1
#define SINGLE_CONSUMER 1
#define ROUND_TO_64(x) ((x + 64) & (~0x3f))

struct llring_pool *new_llr_pool(const char *name, int socket_id,
                                 uint32_t num_llr, uint16_t count_slots)
{
  // Precondition: count_slots should be power of 2
  assert((count_slots & (count_slots-1)) == 0);
  // Precondition: name can not be null
  assert(name != NULL);

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
  int rc;

  // Allocate enough memory for the pool, llrings and stack
  bytes_per_llr_seg = llring_bytes_with_slots(count_slots) +
    sizeof(struct list_head);
  bytes_per_llr_seg = ROUND_TO_64(bytes_per_llr_seg);
  llring_region_memory =  bytes_per_llr_seg * num_llr;
  size_pool_struct = ROUND_TO_64(sizeof(struct llring_pool));
  memory_bytes =  size_pool_struct +                       // pool struct
    llring_region_memory;                                  // llring

  // Pool
  ptr = rte_malloc(NULL, memory_bytes, 64);
  pool = ptr;
  pool->num_llrings = num_llr;
  pool->llr_size = count_slots;

  // LLRings
  ptr += size_pool_struct;
  pool->memory = ptr;

  // Stack
  pool->ptr_stack = rte_stack_create(name, num_llr, socket_id, RTE_STACK_F_LF);

  // Initialize llrings and stack
  ptr = (void *)pool->memory;
  for (i = 0; i < num_llr; i++) {
    seg = ptr;
    llr = &seg->ring;
    rc = llring_init(llr, count_slots, SINGLE_PRODUCER, SINGLE_CONSUMER);

    if (unlikely(rc != 0)) {
      fprintf(stderr, "initializing llring failed\n");
      free_llr_pool(pool);
      return NULL;
    }

    llring_set_water_mark(llr, water_mark);

    // TESTING
    assert(llring_free_count(llr) > 0);

    rc = rte_stack_push(pool->ptr_stack, (void **)&seg, 1);
    assert(rc == 1);
    ptr += bytes_per_llr_seg;
  }

  return pool;
}

void free_llr_pool(struct llring_pool *pool)
{
  rte_stack_free(pool->ptr_stack);
  rte_free(pool);
}

struct llr_seg *pull_llr(struct llring_pool *pool)
{
  struct llr_seg *seg;
  int rc;
  rc = rte_stack_pop(pool->ptr_stack, (void **)&seg, 1);
  if (rc == 0)
    return NULL;
  return seg;
}

int push_llr(struct llring_pool *pool, const struct llr_seg *seg)
{
  int rc;
  // TODO: verify the llr pointer is valid
  if (seg == NULL)
    return -1;
  rc = rte_stack_push(pool->ptr_stack, (void **)&seg, 1);
  if (rc == 0)
    return 2;
  return 0;
}
