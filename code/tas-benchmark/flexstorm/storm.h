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

#ifndef STORM_H
#define STORM_H

#define _GNU_SOURCE
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>

// Set FLEXNIC requirement here!
#if defined(BIGFISH_FLEXNIC) || defined(BIGFISH_FLEXNIC_DPDK) || defined(BIGFISH_FLEXNIC_DPDK2)
#	define FLEXNIC
//#	define NORMAL_QUEUE
#else
#	define NORMAL_QUEUE
#endif

//#define THREAD_AFFINITY
#define DEBUG_PERF

#define MAX_VECTOR	5	// Max. tuple vector
#define MAX_STR		64
#define MAX_WORKERS	5
#define MAX_EXECUTORS	5
#define MAX_TASKS	100
#define MAX_ELEMS	(4 * 1024)	// Queue elements
//#define MAX_ELEMS	64	// Queue elements

struct tuple;
struct executor;

typedef void (*WorkerExecute)(const struct tuple *t, struct executor *self);
typedef void (*WorkerInit)(struct executor *self);
typedef int (*GrouperFunc)(const struct tuple *t, struct executor *self);

struct tuple {
#ifndef FLEXNIC
  int		task, fromtask;
#else
  volatile int		task, fromtask;
#endif
  uint64_t 	starttime;
  struct {
    char	str[MAX_STR];
    int		integer;
  } v[MAX_VECTOR];
};

struct queue {
  sem_t		hsem, tsem;
  struct tuple 	elems[MAX_ELEMS];
  int		head, tail;
#ifdef DEBUG_PERF
  size_t	full, empty;
#endif
};

struct flexnic_queue {
  __attribute__ ((aligned (64))) volatile int head;
  __attribute__ ((aligned (64))) volatile int tail;
  __attribute__ ((aligned (64))) struct tuple	elems[MAX_ELEMS];
#ifdef DEBUG_PERF
  __attribute__ ((aligned (64))) volatile size_t full;
  __attribute__ ((aligned (64))) volatile size_t empty;
#endif
};

struct executor {
  pthread_t	tid;
  int		taskid, outtasks[MAX_TASKS];
#if !defined(FLEXNIC) || (defined(NORMAL_QUEUE) && !defined(FLEXNIC_EMULATION))
  struct queue	inqueue, outqueue;
#else
  struct flexnic_queue *inqueue, *outqueue;
#endif
  WorkerExecute	execute;
  WorkerInit	init;
  void		*state;
  GrouperFunc	grouper;
  bool		spout;
  size_t	outqueue_empty, outqueue_full, inqueue_empty, inqueue_full;
#ifndef FLEXNIC_EMULATION
  size_t	execute_time, numexecutes, emitted, recved, avglatency;
#else
  size_t	tuples, lasttuples, full;
  size_t	wait_inq, wait_outq, memcpy_time, batchdone_time, batch_size, batches;
  int		rx_id, workerid;
#endif
#ifdef USE_MTCP
  pthread_mutex_t post_mutex;
#endif
} __attribute__ ((aligned (64)));

void queue_post(struct queue *q, struct tuple *t);
void tuple_send(struct tuple *t, struct executor *self);

static inline uint64_t rdtsc(void)
{
    uint32_t eax, edx;
    __asm volatile ("rdtsc" : "=a" (eax), "=d" (edx));
    return ((uint64_t)edx << 32) | eax;
}

#endif
