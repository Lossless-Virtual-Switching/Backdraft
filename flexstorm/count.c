/*
 * Copyright 2019 University of Washington, Max Planck Institute for
 * Software Systems, and The University of Texas at Austin
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>

#include "storm.h"
#include "hash.h"
#include "collections/hash_table.h"

#define DEBUG

#define NUM_WINDOW_CHUNKS			5
#define DEFAULT_SLIDING_WINDOW_IN_SECONDS	(NUM_WINDOW_CHUNKS * 60)
#define DEFAULT_EMIT_FREQUENCY_IN_SECONDS	(DEFAULT_SLIDING_WINDOW_IN_SECONDS / NUM_WINDOW_CHUNKS)

#define BUCKETS		5500000
/* #define BUCKETS		NUM_BUCKETS */

struct bucket {
  char	str[MAX_STR];
  long	slots[NUM_WINDOW_CHUNKS];
};

struct count_state {
  collections_hash_table *counts;
};

static __thread struct executor *myself = NULL;

static void tuple_free(void *t)
{
  assert(!"NYI");
}

#if 0
static int count_reset(uint64_t key, void *data, void *arg)
{
  uintptr_t slot = (uintptr_t)arg;
  struct bucket *b = data;

  b->slots[slot] = 0;

  return 1;
}

static int count_emit(uint64_t key, void *data, void *arg)
{
  struct bucket *b = data;
  long sum = 0;
  struct tuple t;

  for(int i = 0; i < NUM_WINDOW_CHUNKS; i++) {
    sum += b->slots[i];
  }

  memset(&t, 0, sizeof(struct tuple));
  strcpy(t.v[0].str, b->str);
  t.v[0].integer = sum;
  tuple_send(&t, myself);

  return 1;
}
#endif

void count_execute(const struct tuple *t, struct executor *self)
{
  assert(self != NULL);
  struct count_state *st = self->state;
  static __thread uintptr_t headslot = 0, tailslot = 1;
  static __thread time_t lasttime = 0;
#ifdef DEBUG
  static __thread time_t debug_lasttime = 0;
  static __thread size_t numexecutes = 0;
  static __thread uint64_t execute_time = 0;
 /* before_time = 0, hash_time = 0, lookup_time = 0, insert_time = 0; */

  uint64_t starttime = rdtsc();

  numexecutes++;
#endif

  assert(t != NULL);

  if(myself == NULL) {
    myself = self;
  } else {
    assert(myself == self);
  }

  /* printf("Counter %d got '%s', number %zu.\n", t->task, t->v[0].str, numexecutes); */

  /* uint64_t before_hash = rdtsc(); */

  uint64_t key = hash(t->v[0].str, strlen(t->v[0].str), 0);

  /* uint64_t before_lookup = rdtsc(); */

  struct bucket *b = collections_hash_find(st->counts, key);

  /* uint64_t before_insert = rdtsc(); */

  if(b == NULL) {
    b = malloc(sizeof(struct bucket));
    assert(b != NULL);
    memset(b, 0, sizeof(struct bucket));
    collections_hash_insert(st->counts, key, b);
    strcpy(b->str, t->v[0].str);
  }
  b->slots[headslot]++;

  /* usleep(10000); */

#ifdef DEBUG
  uint64_t now = rdtsc();
  /* before_time += before_hash - starttime; */
  /* hash_time += before_lookup - before_hash; */
  /* lookup_time += before_insert - before_lookup; */
  /* insert_time += now - before_insert; */
  execute_time += now - starttime;
#endif

  struct timeval tv;
  int r = gettimeofday(&tv, NULL);
  assert(r == 0);

  if(tv.tv_sec >= lasttime + DEFAULT_EMIT_FREQUENCY_IN_SECONDS) {
    lasttime = tv.tv_sec;

#if 0
    // Emit all counts
    r = collections_hash_visit(st->counts, count_emit, NULL);
    assert(r != 0);

    // Wipe current window
    r = collections_hash_visit(st->counts, count_reset, (void *)tailslot);
    assert(r != 0);
#endif

    // Advance window
    headslot = tailslot;
    tailslot = (tailslot + 1) % NUM_WINDOW_CHUNKS;
  }

#ifdef DEBUG
  if(tv.tv_sec >= debug_lasttime + 1) {
    debug_lasttime = tv.tv_sec;
    printf("Worker %d executed %zu latency %" PRIu64 ", tuple latency %zu\n",
	   self->taskid, numexecutes, execute_time / numexecutes,
	   self->avglatency / (self->numexecutes > 0 ? self->numexecutes : 1));
    /* printf("Worker %d executed %zu latency %" PRIu64 ", %" PRIu64 ", %" PRIu64 ", %" PRIu64 ", %" PRIu64 "\n", self->taskid, numexecutes, execute_time / numexecutes, */
    /* 	   before_time / numexecutes, */
    /* 	   hash_time / numexecutes, */
    /* 	   lookup_time / numexecutes, */
    /* 	   insert_time / numexecutes); */
    /* numexecutes = 0; */
    /* execute_time = 0; */
  }
#endif
}

void count_init(struct executor *self)
{
  assert(self != NULL);
  self->state = malloc(sizeof(struct count_state));
  assert(self->state != NULL);
  struct count_state *st = self->state;
  printf("%d: Creating hash\n", self->taskid);
  collections_hash_create_with_buckets(&st->counts, BUCKETS, tuple_free);
  assert(st->counts != NULL);
  printf("%d: hash created\n", self->taskid);
}
