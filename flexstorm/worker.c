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

#define _GNU_SOURCE
#include <sched.h>
#include <stdlib.h>
#include <stdio.h>
#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

#ifdef USE_MTCP
# include <mtcp_api.h>
# include <mtcp_epoll.h>

/* int GetRSSCPUCore(in_addr_t sip, in_addr_t dip,  */
/* 		  in_port_t sp, in_port_t dp, int num_queues, */
/* 		  uint8_t endian_check); */

# define MTCP_INPUT_CN		0
/* # define MTCP_OUTPUT_CN		1 */

# define EPOLL_CTL_ADD MTCP_EPOLL_CTL_ADD
# define EPOLL_CTL_DEL MTCP_EPOLL_CTL_DEL
# define EPOLL_CTL_MOD MTCP_EPOLL_CTL_MOD

# define EPOLLIN  MTCP_EPOLLIN
# define EPOLLOUT MTCP_EPOLLOUT
# define EPOLLERR MTCP_EPOLLERR
# define EPOLLHUP MTCP_EPOLLHUP

# define epoll_create1(flags)	mtcp_epoll_create(_mtcp_ctx, 64)
# define epoll_wait(epfd, evs, nevs, timeout) \
  mtcp_epoll_wait(_mtcp_ctx, epfd, evs, nevs, timeout)
#define epoll_ctl(epfd, op, fd, ev) \
  mtcp_epoll_ctl(_mtcp_ctx, epfd, op, fd, ev)

# define epoll_event	mtcp_epoll_event

# define socket(d, t, p)	mtcp_socket(_mtcp_ctx, d, t, p)
# define listen(f, b)		mtcp_listen(_mtcp_ctx, f, b)
# define accept(s, a, al)	mtcp_accept(_mtcp_ctx, s, a, al)
# define connect(s, a, al)	mtcp_connect(_mtcp_ctx, s, a, al)
# define recv(s, b, l, f)	mtcp_recv(_mtcp_ctx, s, b, l, f)
# define send(s, b, l, f)	mtcp_write(_mtcp_ctx, s, (const char *)b, l)
# define bind(s, a, al)		mtcp_bind(_mtcp_ctx, s, a, al)
# define setsockopt(s, l, o, ov, ol)	mtcp_setsockopt(_mtcp_ctx, s, l, o, ov, ol)

/* static __thread mctx_t _mtcp_ctx; */
static mctx_t _mtcp_ctx;
#else
# include <sys/epoll.h>
#endif

#include "storm.h"
#include "hash.h"

/* #define SENDER_WAIT_US	10000 */
/* #define SENDER_WAIT_US	2000 */
#define SENDER_WAIT_US	0
#define MAX_INBUF	8192
#define MAX_EVENTS	16

#define MAX(a, b)	((a) < (b) ? (b) : (a))

struct stream {
  int listener;
#ifdef FLEXNIC
  struct flexnic_queue *inqueue[MAX_WORKERS], *outqueue[MAX_WORKERS];
#endif
  int channel[MAX_WORKERS];
  struct tuple inbuf[MAX_WORKERS][MAX_INBUF];
  int rest[MAX_WORKERS];
  int nchannels;
};

static int workerid;
static struct executor *task2executor[MAX_TASKS];
static volatile uint64_t sender_avglatency = 0;
#ifdef DEBUG_PERF
static volatile uint64_t sender_sleep = 0, sender_inner = 0, sender_sending = 0, sender_start = 0, sender_advance = 0, sender_post = 0;
#endif
static volatile size_t sender_sent = 0;
static struct stream stream;

#include "worker.h"

#ifdef NORMAL_QUEUE

static struct tuple *queue_peek(struct queue *q, bool trywait)
{
  int r;

  if(trywait) {
    int val;
    r = sem_getvalue(&q->tsem, &val);
    assert(r == 0);
    if(val <= 0) {
#ifdef DEBUG_PERF
      q->empty++;
#endif
      return NULL;
    }
  } else {
#ifdef DEBUG_PERF
    r = sem_trywait(&q->tsem);
    if(r != 0) {
      assert(errno == EAGAIN);
      q->empty++;
#endif
    r = sem_wait(&q->tsem);
    assert(r == 0);
#ifdef DEBUG_PERF
    }
#endif
  }

  return &q->elems[q->tail];
}

static void queue_advance(struct queue *q, bool peeked)
{
  if(peeked) {
    int r = sem_wait(&q->tsem);
    assert(r == 0);
  }
  int r = sem_post(&q->hsem);
  assert(r == 0);
  q->tail = (q->tail + 1) % MAX_ELEMS;
}

void queue_post(struct queue *q, struct tuple *t)
{
  int r;
#ifdef DEBUG_PERF
  r = sem_trywait(&q->hsem);
  if(r != 0) {
    assert(errno == EAGAIN);
    q->full++;
#endif
  r = sem_wait(&q->hsem);
  assert(r == 0);
#ifdef DEBUG_PERF
  }
#endif

  assert(t->task != 0);

  memcpy(&q->elems[q->head], t, sizeof(struct tuple));

  r = sem_post(&q->tsem);
  assert(r == 0);

  q->head = (q->head + 1) % MAX_ELEMS;
}

static void queue_init(struct queue *q)
{
  memset(q, 0, sizeof(struct queue));
  int r = sem_init(&q->tsem, 0, 0);
  assert(r == 0);
  r = sem_init(&q->hsem, 0, MAX_ELEMS);
  assert(r == 0);
}

static int task2sock(int taskid, struct worker **outworker)
{
  int outsock = 0;

  for(int i = 0; i < MAX_WORKERS && workers[i].hostname != NULL; i++) {
    for(int j = 0; j < MAX_EXECUTORS && workers[i].executors[j].execute != NULL; j++) {
      if(taskid == workers[i].executors[j].taskid) {
	outsock = workers[i].sock;
	if(outworker != NULL) {
	  *outworker = &workers[i];
	}
	break;
      }
    }
  }
  assert(outsock != 0);

  return outsock;
}

#endif

#ifdef FLEXNIC

static struct tuple *flexnic_queue_peek(struct flexnic_queue *q)
{
  // Spin for new element
  while(q->elems[q->tail].task == 0) {
#ifdef DEBUG_PERF
    q->empty++;
#endif
  }
#ifdef FLEXNIC_DEBUG
  struct tuple *t = &q->elems[q->tail];
  printf("worker %d: incoming tuple %d -> %d\n", workerid, t->fromtask, t->task);
#endif
  return &q->elems[q->tail];
}

static struct tuple *flexnic_queue_poll(struct flexnic_queue *q)
{
  // Spin for new element
  if(q->elems[q->tail].task == 0) {
#ifdef DEBUG_PERF
    q->empty++;
#endif
    return NULL;
  }
#ifdef FLEXNIC_DEBUG
  struct tuple *t = &q->elems[q->tail];
  printf("worker %d: incoming tuple %d -> %d\n", workerid, t->fromtask, t->task);
#endif
  return &q->elems[q->tail];
}

static void flexnic_queue_advance(struct flexnic_queue *q)
{
  q->elems[q->tail].task = 0;
  q->tail = (q->tail + 1) % MAX_ELEMS;
}

void flexnic_queue_post(struct flexnic_queue *q, struct tuple *t)
{
/* #ifdef DEBUG_PERF */
/*   if(q->elems[q->head].task != 0) { */
/*     q->full++; */
/*   } */
/* #endif */
  // Spin until free entries
  while(q->elems[q->head].task != 0) {
#ifdef DEBUG_PERF
    q->full++;
#endif
  }

#ifdef FLEXNIC_DEBUG
  printf("worker %d: sending tuple %d -> %d\n", workerid, t->fromtask, t->task);
#endif

  memcpy(&q->elems[q->head], t, sizeof(struct tuple));
  q->head = (q->head + 1) % MAX_ELEMS;
}

static struct flexnic_queue *flexnic_queue_init(const char *filename)
{
    int fd;
    void *p;
    struct stat st;

    if ((fd = shm_open(filename, O_RDWR, 0600)) == -1) {
        perror("shm_open failed");
        abort();
    }

    if (fstat(fd, &st) != 0) {
        perror("fstat failed");
        abort();
    }
    assert(st.st_size == sizeof(struct flexnic_queue));

    if ((p = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) ==
            (void *) -1)
    {
        perror("mmap failed");
        abort();
    }

    close(fd);
    return p;
}
#endif

/* static size_t target[MAX_TASKS]; */

void tuple_send(struct tuple *t, struct executor *self)
{
  // Set source and destination
  t->fromtask = self->taskid;
  t->task = self->grouper(t, self);
  t->starttime = rdtsc();

  // Drop?
  if(t->task == 0) {
    return;
  }

  /* __sync_fetch_and_add(&target[t->task], 1); */

  // Put to outqueue
#if !defined(FLEXNIC) || defined(NORMAL_QUEUE)
  queue_post(&self->outqueue, t);
#else
  flexnic_queue_post(self->outqueue, t);
#endif

  self->emitted++;
}

static void *executor_thread(void *arg)
{
  struct executor *self = arg;

#ifdef THREAD_AFFINITY
#if defined(BIGFISH) || defined(BIGFISH_FLEXNIC)
  // Pin to core
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(self->taskid, &set);
  int r = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &set);
  assert(r == 0);
#endif
#endif

#ifdef USE_MTCP
  static int corenum = 3;
  int r = mtcp_core_affinitize(__sync_fetch_and_add(&corenum, 1));
  assert(r == 0);
#endif

  // Run dispatch loop
  for(;;) {
    if(!self->spout) {
#if !defined(FLEXNIC) || defined(NORMAL_QUEUE)
      struct tuple *t = queue_peek(&self->inqueue, false);
#else
      struct tuple *t = flexnic_queue_peek(self->inqueue);
#endif
      assert(t != NULL);
      /* printf("Tuple for %d received\n", t->task); */
      uint64_t starttime = rdtsc();
      self->execute(t, self);
      /* printf("Tuple %d done\n", t->task); */
      uint64_t now = rdtsc();
#if !defined(FLEXNIC) || defined(NORMAL_QUEUE)
      queue_advance(&self->inqueue, false);
      self->avglatency += starttime - t->starttime;
#else
      flexnic_queue_advance(self->inqueue);
#endif
      self->execute_time += now - starttime;
      self->numexecutes++;
    } else {
      uint64_t starttime = rdtsc();
      self->execute(NULL, self);
      uint64_t now = rdtsc();
      self->execute_time += now - starttime;
      self->numexecutes++;
    }
  }

  return NULL;
}

#if !defined(FLEXNIC) || defined(NORMAL_QUEUE)
static void *sender_thread(void *arg)
{
  struct executor *executor = arg;

#ifdef USE_MTCP
  int r = mtcp_core_affinitize(2);
  assert(r == 0);
#endif

/* #ifdef USE_MTCP */
/*   int r; */

/*   /\* get mtcp context ready *\/ */
/*   if ((r = mtcp_core_affinitize(MTCP_OUTPUT_CN)) != 0) { */
/*     fprintf(stderr, "[%d] mtcp_core_affinitize failed: %d\n", MTCP_OUTPUT_CN, r); */
/*     abort(); */
/*   } */
/* #endif */

  sleep(10);

#ifndef FLEXNIC
  // Open all outgoing streams
  for(int i = 0; i < MAX_WORKERS && workers[i].hostname != NULL; i++) {
#ifdef USE_MTCP
    if(workerid == i) {
      workers[i].sock = -10;
      continue;
    }
#endif

    struct sockaddr_in saddr = {
      .sin_family = AF_INET
    };

    int r = inet_pton(AF_INET, workers[i].hostname, &saddr.sin_addr);
    assert(r == 1);

    saddr.sin_port = htons(workers[i].port);

/* #ifdef USE_MTCP */
/*     if ((workers[i]._mtcp_ctx = mtcp_create_context(MTCP_OUTPUT_CN)) == NULL) { */
/*       fprintf(stderr, "[%d] mtcp_create_context failed\n", MTCP_OUTPUT_CN); */
/*       abort(); */
/*     } */

/*     if ((r = mtcp_init_rss(workers[i]._mtcp_ctx, INADDR_ANY, 1, */
/* 			   saddr.sin_addr.s_addr, saddr.sin_port)) != 0) */
/*       { */
/* 	fprintf(stderr, "[%d] mtcp_init_rss failed: %d\n", MTCP_OUTPUT_CN, r); */
/* 	abort(); */
/*       } */
/*     workers[i].sock = mtcp_socket(workers[i]._mtcp_ctx, AF_INET, SOCK_STREAM, IPPROTO_TCP); */
/* #else */
    workers[i].sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
/* #endif */
    if(workers[i].sock == -1) {
      perror("socket");
    }
    assert(workers[i].sock != -1);

/* #ifdef USE_MTCP */

/* #define SPORT	1025 */

/*     struct in_addr sip; */
/*     r = inet_pton(AF_INET, workers[workerid].hostname, &sip); */
/*     assert(r == 1); */
/*     uint16_t myport = 0; */

/*     // TODO: Try all port combinations until found a good one */
/*     for(int sport = SPORT; sport < SPORT + 1000; sport++) { */
/*       int dcpu = GetRSSCPUCore(sip.s_addr, saddr.sin_addr.s_addr, */
/* 			       htons(sport), saddr.sin_port, */
/* 			       2, 0); */
/*       int scpu = GetRSSCPUCore(saddr.sin_addr.s_addr, sip.s_addr, */
/* 			       saddr.sin_port, htons(sport), */
/* 			       2, 0); */

/*       fprintf(stderr, "%d: %s:%u -> %s:%u = core %u\n", workerid, */
/* 	      workers[workerid].hostname, sport, workers[i].hostname, */
/* 	      ntohs(saddr.sin_port), dcpu); */
/*       fprintf(stderr, "%d: %s:%u <- %s:%u = core %u\n", workerid, */
/* 	      workers[workerid].hostname, sport, workers[i].hostname, */
/* 	      ntohs(saddr.sin_port), scpu); */

/*       if(dcpu == 0) { */
/* 	myport = sport; */
/* 	break; */
/*       } */
/*     } */

/*     struct sockaddr_in l_saddr = { */
/*       .sin_family = AF_INET, */
/*       .sin_port = htons(myport), */
/*       .sin_addr = sip, */
/*     }; */
/*     r = mtcp_bind(workers[i]._mtcp_ctx, workers[i].sock, (void *)&l_saddr, sizeof(struct sockaddr_in)); */
/*     assert(r == 0); */
/* #endif */

    printf("%d: connect to '%s' on FD %d...\n", workerid, workers[i].hostname,
	   workers[i].sock);

/* #ifdef USE_MTCP */
/*     r = mtcp_connect(workers[i]._mtcp_ctx, workers[i].sock, (void *)&saddr, sizeof(struct sockaddr_in)); */
/* #else */
    r = connect(workers[i].sock, (void *)&saddr, sizeof(struct sockaddr_in));
/* #endif */
    if(r != 0) {
      printf("%d: connecting to '%s': %s\n", workerid,
	     workers[i].hostname, strerror(errno));
    }
    assert(r == 0);

    printf("done\n");
  }
#endif

  sleep(2);

  sender_start = rdtsc();

  for(;;) {
    bool sending;
#ifdef DEBUG_PERF
    uint64_t begin = rdtsc();
#endif

    // Sleep
#if SENDER_WAIT_US > 0
    usleep(SENDER_WAIT_US);
#endif

#ifdef DEBUG_PERF
    uint64_t after_sleep = rdtsc();
    sender_sleep += after_sleep - begin;
#endif

    // Send everything in a burst
    do {
#ifdef DEBUG_PERF
      uint64_t start_loop = rdtsc();
#endif

      sending = false;
      // Send tuples from outqueues
      for(int i = 0; i < MAX_EXECUTORS && executor[i].execute != NULL; i++) {
	struct queue *q = &executor[i].outqueue;
	struct tuple *t = queue_peek(q, true);

	// Do we have a tuple?
	if(t != NULL) {
	  /* printf("%d: Sending to %d from %d, entry %d, head %d, executor %d\n", */
	  /* 	 workerid, t->task, t->fromtask, q->tail, q->head, i); */
	  uint64_t now = rdtsc();
	  sender_avglatency += now - t->starttime;
	  sender_sent++;

#ifndef FLEXNIC
	  int r = 0;
	  uint8_t *p = (void *)t;
	  struct worker *w = NULL;
	  int fd = task2sock(t->task, &w);
	  size_t tosend = sizeof(struct tuple);

#	ifdef USE_MTCP
	  if(fd == -10) {
	    // It's on the local host
	    struct executor *to_exec = task2executor[t->task];
	    assert(to_exec != NULL);
	    r = pthread_mutex_lock(&to_exec->post_mutex);
	    assert(r == 0);
	    queue_post(&to_exec->inqueue, t);
	    r = pthread_mutex_unlock(&to_exec->post_mutex);
	    assert(r == 0);

	    r = sizeof(struct tuple);
	  } else {
#	endif
	    /* printf("%d: prior to send\n", workerid); */
	    do {
	      /* #ifdef USE_MTCP */
	      /* 	    int ret = mtcp_write(w->_mtcp_ctx, fd, (const char *)p, tosend); */
	      /* #else */
	      int ret;

	    uint64_t now2 = rdtsc();

#	ifdef USE_MTCP
	      do {
#	endif
		ret = send(fd, p, tosend, 0);
#	ifdef USE_MTCP
		if(ret == -1 && errno == EAGAIN) {
		  pthread_yield();
		}
	      } while(ret == -1 && errno == EAGAIN);
#	endif

	    uint64_t after_post = rdtsc();
	    sender_post += after_post - now2;

	      /* #endif */
	      if(ret == -1) {
		perror("send");
	      }
	      assert(ret != -1);
	      r += ret;
	      tosend -= ret;
	      p += ret;
	    } while(r < sizeof(struct tuple));
	    /* printf("%d: sending done\n", workerid); */
#	ifdef USE_MTCP
	  }
#	endif
#else
	  flexnic_queue_post(stream.outqueue[task2sock(t->task, NULL) - 1], t);
	  int r = sizeof(struct tuple);
#endif

#ifdef DEBUG_PERF
	  uint64_t after_send = rdtsc();
	  sender_inner += after_send - now;
#endif

	  queue_advance(q, true);
	  if(r == -1 || r != sizeof(struct tuple)) {
	    printf("r == %d, sending to %d, from %d\n", r, t->task, workerid);
	    perror("worker");
	  }
	  assert(r != -1 && r == sizeof(struct tuple));
	  sending = true;

#ifdef DEBUG_PERF
	  uint64_t after_advance = rdtsc();
	  sender_advance += after_advance - after_send;
#endif
	}
      }

#ifdef DEBUG_PERF
      uint64_t end_loop = rdtsc();
      sender_sending += end_loop - start_loop;
#endif
    } while(sending);
  }

  return NULL;
}

#	ifndef FLEXNIC
static int stream_listen(struct stream *s, uint16_t port)
{
  s->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  assert(s->listener != -1);

  int optval = 1;
  int r = setsockopt(s->listener, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int));
  assert(r == 0);

#ifdef USE_MTCP
  r = mtcp_setsock_nonblock(_mtcp_ctx, s->listener);
  assert(r == 0);
#endif

  struct sockaddr_in saddr = {
    .sin_family = AF_INET,
    .sin_port = htons(port),
    .sin_addr = { INADDR_ANY },
  };

  r = bind(s->listener, (void *)&saddr, sizeof(saddr));
  assert(r == 0);

  r = listen(s->listener, 4);
  assert(r == 0);

  return 0;
}
#	endif
#endif

int main(int argc, char *argv[])
{
  assert(argc > 1);
  workerid = atoi(argv[1]);
  struct executor *executor = workers[workerid].executors;
  int r;

  if(executor[0].execute == NULL) {
    printf("Not a valid worker ID: %d\n", workerid);
    return EXIT_SUCCESS;
  }

#ifdef USE_MTCP
  if ((r = mtcp_init("mystorm_mtcp.conf")) != 0) {
    fprintf(stderr, "mtcp_init failed: %d\n", r);
    return EXIT_FAILURE;
  }

  /* get mtcp context ready */
  /* if ((r = mtcp_core_affinitize(MTCP_INPUT_CN)) != 0) { */
  /*   fprintf(stderr, "[%d] mtcp_core_affinitize failed: %d\n", MTCP_INPUT_CN, r); */
  /*   abort(); */
  /* } */

#ifdef USE_MTCP
  r = mtcp_core_affinitize(1);
  assert(r == 0);
#endif

  if ((_mtcp_ctx = mtcp_create_context(MTCP_INPUT_CN)) == NULL) {
    fprintf(stderr, "[%d] mtcp_create_context failed\n", MTCP_INPUT_CN);
    abort();
  }
#endif

  // Initialize quick lookup tables
  for(int i = 0; i < MAX_EXECUTORS && executor[i].execute != NULL; i++) {
    assert(task2executor[executor[i].taskid] == NULL);
    task2executor[executor[i].taskid] = &executor[i];
  }

/* #ifdef USE_MTCP */

/* #define SIP	"10.0.0.1" */
/* #define DIP	"10.0.0.4" */
/* #define SPORT	1025 */
/* #define DPORT	7001 */

/*   for(int s = 0; s < 3; s++) { */
/*     for(int d = 0; d < 3; d++) { */
/*       for(int sport = SPORT; sport < SPORT + 10; sport++) { */
/* 	for(int dport = DPORT; dport < DPORT + 10; dport++) { */
/* 	  struct in_addr sip, dip; */
/* 	  r = inet_pton(AF_INET, workers[s].hostname, &sip); */
/* 	  assert(r == 1); */
/* 	  r = inet_pton(AF_INET, workers[d].hostname, &dip); */
/* 	  assert(r == 1); */
/* 	  fprintf(stderr, "%d: %s:%u -> %s:%u = core %u\n", workerid, */
/* 		  workers[s].hostname, sport, workers[d].hostname, dport, */
/* 		  GetRSSCPUCore(sip.s_addr, dip.s_addr, */
/* 				htons(sport), htons(dport), */
/* 				2, 0)); */
/* 	} */
/*       } */
/*     } */
/*   } */

/* #endif */

#ifndef FLEXNIC
  // Listen on specified port
  r = stream_listen(&stream, workers[workerid].port);
  assert(r == 0);
#endif

#if !defined(FLEXNIC) || defined(NORMAL_QUEUE)
  // Spawn sender thread
  pthread_t sender_tid;
  r = pthread_create(&sender_tid, NULL, sender_thread, executor);
  assert(r == 0);
#endif

  // Init all executors
  for(int i = 0; i < MAX_EXECUTORS && executor[i].execute != NULL; i++) {
#ifdef USE_MTCP
    r = pthread_mutex_init(&executor[i].post_mutex, NULL);
    assert(r == 0);
#endif

    if(executor[i].init != NULL) {
      executor[i].init(&executor[i]);
    }
  }

#ifdef FLEXNIC
  for(int i = 0; i < MAX_WORKERS && workers[i].hostname != NULL; i++) {
    workers[i].sock = i + 1;
  }
#endif

  // Init & spawn all executors
  for(int i = 0; i < MAX_EXECUTORS && executor[i].execute != NULL; i++) {
#ifdef NORMAL_QUEUE
    // Initialize
    queue_init(&executor[i].inqueue);
    queue_init(&executor[i].outqueue);
#endif
#ifdef FLEXNIC
    char filename[128];
    r = snprintf(filename, 128, "worker_%d_inq_%d", workerid, i);
    assert(r >= 0);
#	ifndef NORMAL_QUEUE
    executor[i].inqueue = flexnic_queue_init(filename);
#	else
    stream.inqueue[i] = flexnic_queue_init(filename);
#	endif
    r = snprintf(filename, 128, "worker_%d_outq_%d", workerid, i);
    assert(r >= 0);
#	ifndef NORMAL_QUEUE
    executor[i].outqueue = flexnic_queue_init(filename);
#	else
    stream.outqueue[i] = flexnic_queue_init(filename);
    stream.channel[i] = i;
    stream.nchannels++;
#	endif
#endif

    // Spawn
    r = pthread_create(&executor[i].tid, NULL, executor_thread, &executor[i]);
    assert(r == 0);
  }

#ifndef FLEXNIC
  // Accept all incoming connections
  for(int i = 0; i <= MAX_WORKERS && workers[i].hostname != NULL; i++) {
#ifdef USE_MTCP
    if(i == workerid) {
      continue;
    }
#endif
#	if 1
    struct sockaddr_in sa;
    socklen_t len = sizeof(struct sockaddr_in);
    char addr[256];
    printf("%d: accept\n", workerid);
#ifdef USE_MTCP
    do {
#endif
      stream.channel[stream.nchannels] = accept(stream.listener, (void *)&sa, &len);
#ifdef USE_MTCP
    } while(stream.channel[stream.nchannels] == -1 && errno == EAGAIN);
#endif
    if(stream.channel[stream.nchannels] == -1) {
      perror("blah");
    }
    assert(stream.channel[stream.nchannels] != -1);
    inet_ntop(AF_INET, &sa.sin_addr, addr, 256);
    printf("%d: New connection on FD %d from %s\n", workerid,
	   stream.channel[stream.nchannels], addr);
    stream.nchannels++;
#	else
    stream.channel[stream.nchannels] = accept(stream.listener, NULL, NULL);
    assert(stream.channel[stream.nchannels] != -1);
    stream.nchannels++;
    assert(stream.nchannels <= MAX_WORKERS);
#	endif
  }
#endif

  struct timeval next_timeout;

  r = gettimeofday(&next_timeout, NULL);
  assert(r == 0);

  next_timeout.tv_sec += 1;

  int epfd = epoll_create1(0);
  assert(epfd != -1);

  struct epoll_event event = { .events = EPOLLIN }, events[MAX_EVENTS];

  for(int i = 0; i < stream.nchannels; i++) {
#ifdef USE_MTCP
    event.data.sockid = i;
#else
    event.data.fd = i;
#endif
    r = epoll_ctl(epfd, EPOLL_CTL_ADD, stream.channel[i], &event);
  }

#if defined(DEBUG_PERF)
  uint64_t start = rdtsc(), time_posting = 0, time_selecting = 0, time_recv = 0;
  (void)start;
#endif

  // Run dispatch loop
  for(;;) {
#if defined(DEBUG_PERF)
    uint64_t begin_select = rdtsc();
#endif

    struct timeval now, tout;
    r = gettimeofday(&now, NULL);
    assert(r == 0);
    timersub(&next_timeout, &now, &tout);
    if(tout.tv_sec < 0 || tout.tv_usec < 0) {
      timerclear(&tout);
    }
    int timeout = (tout.tv_sec * 1000) + (tout.tv_usec / 1000);

#if defined(FLEXNIC) && defined(NORMAL_QUEUE)
    // Fake select on flexnic queues
    for(int i = 0; i < nfds; i++) {
      if(FD_ISSET(i, &readfds)) {
	if(flexnic_queue_poll(stream.inqueue[i]) == NULL) {
	  FD_CLR(i, &readfds);
	}
      }
    }
#else
    int nevents = epoll_wait(epfd, events, MAX_EVENTS, timeout);
    if(nevents == -1 && errno != EINTR) {
      perror("epoll_wait");
    }
    assert(nevents != -1 || errno == EINTR);
#endif

    // Print stats upon timeout and reset
    if(timeout == 0) {
      next_timeout.tv_sec++;

#ifdef DEBUG_PERF
      for(int i = 0; i < MAX_EXECUTORS && executor[i].execute != NULL; i++) {
	struct executor *e = &executor[i];

	printf("%d: in %zu / %zu out %zu / %zu, emitted %zu, execute time %zu, numexecutes %zu, avg. latency %zu\n",
	       e->taskid,
#	ifdef NORMAL_QUEUE
	       e->inqueue.empty, e->inqueue.full,
	       e->outqueue.empty, e->outqueue.full,
#	else
	       e->inqueue->empty, e->inqueue->full,
	       e->outqueue->empty, e->outqueue->full,
#	endif
	       e->emitted,
	       e->execute_time / (e->numexecutes > 0 ? e->numexecutes : 1),
	       e->numexecutes,
	       e->avglatency / (e->numexecutes > 0 ? e->numexecutes : 1));

#	ifndef NORMAL_QUEUE
        e->inqueue->empty = e->inqueue->full = 0;
        e->outqueue->empty = e->outqueue->full = 0;
#	endif
      }

    /* printf("%d: ", workerid); */
    /* for(int j = 0; j < MAX_TASKS; j++) { */
    /*   if(target[j] > 0) { */
    /* 	printf("%d: %zu ", j, target[j]); */
    /*   } */
    /* } */
    /* printf("\n"); */

#	ifdef NORMAL_QUEUE
      printf("%d total time(ms) = %.2f, time selecting = %.2f, time working = %.2f, time queue posting = %.2f\n",
	     workerid,
	     (float)(rdtsc() - start) / PROC_FREQ,
	     (float)time_selecting / PROC_FREQ,
	     (float)time_posting / PROC_FREQ,
	     (float)time_recv / PROC_FREQ);
      printf("%d total time(ms) = %.2f, time sleeping = %.2f, time sending = %.2f, time sendmsg = %.2f, time post = %.2f, time queue advance = %.2f\n",
	     workerid,
	     (float)(rdtsc() - sender_start) / PROC_FREQ,
	     (float)sender_sleep / PROC_FREQ,
	     (float)sender_sending / PROC_FREQ,
	     (float)sender_inner / PROC_FREQ,
	     (float)sender_post / PROC_FREQ,
	     (float)sender_advance / PROC_FREQ);
#	endif
#endif

      if(sender_sent > 0) {
	printf("%d sender avglatency %" PRIu64 "\n", workerid,
	       sender_avglatency / sender_sent);
      }
    }

#ifdef DEBUG_PERF
    uint64_t begin_post = rdtsc();
    time_selecting += begin_post - begin_select;
#endif

    // Post tuples to inqueues
    for(int e = 0; e < nevents; e++) {
#ifdef USE_MTCP
      int i = events[e].data.sockid;
#else
      int i = events[e].data.fd;
#endif
      /* printf("%d: reading stream %d, FD %d\n", workerid, i, stream.channel[i]); */
      if(i != -1) {
#ifndef FLEXNIC
	/* printf("%d: calling recv\n", workerid); */
	char *bufp = ((char *)stream.inbuf[i]) + stream.rest[i];
	ssize_t ret;
#ifdef USE_MTCP
	do {
#endif
	  ret = recv(stream.channel[i], bufp,
		     MAX_INBUF * sizeof(struct tuple) - stream.rest[i], 0);
#ifdef USE_MTCP
	  if(ret == -1 && errno == EAGAIN) {
	    pthread_yield();
	  }
	} while(ret == -1 && errno == EAGAIN);
#endif
	if(ret == -1) {
	  perror("recv");
	}
	assert(ret != -1);
	/* printf("%d: recv'ed %zu bytes\n", workerid, ret); */
#else
	ssize_t ret = 0;
	for(int j = 0;
	    flexnic_queue_poll(stream.inqueue[i]) != NULL && j < MAX_INBUF;
	    j++, ret += sizeof(struct tuple)) {
	  struct tuple *t = flexnic_queue_peek(stream.inqueue[i]);
	  memcpy(&stream.inbuf[i][j], t, sizeof(struct tuple));
	  flexnic_queue_advance(stream.inqueue[i]);
	}
#endif

	for(int k = 0; k < (ret + stream.rest[i]) / sizeof(struct tuple); k++) {
	  struct tuple *t = &stream.inbuf[i][k];

	  t->starttime = rdtsc();

	  // Find executor that runs target task
	  struct executor *to_exec = task2executor[t->task];
	  assert(to_exec != NULL);

#ifdef DEBUG_PERF
	  uint64_t begin = rdtsc();
#endif
#ifdef NORMAL_QUEUE

#	ifdef USE_MTCP
	    r = pthread_mutex_lock(&to_exec->post_mutex);
	    assert(r == 0);
#	endif

	    queue_post(&to_exec->inqueue, t);

#	ifdef USE_MTCP
	    r = pthread_mutex_unlock(&to_exec->post_mutex);
	    assert(r == 0);
#	endif
#endif
#ifdef DEBUG_PERF
	  uint64_t now = rdtsc();
	  time_recv += now - begin;
#endif
	}

	// Save the rest
	int newrest = (ret + stream.rest[i]) % sizeof(struct tuple);
	if(newrest > 0) {
	  memmove(stream.inbuf[i], &stream.inbuf[i][(ret + stream.rest[i]) / sizeof(struct tuple)], newrest);
	}
	stream.rest[i] = newrest;
      }

      assert(i != -1);
    }

#ifdef DEBUG_PERF
    time_posting += rdtsc() - begin_post;
#endif
  }

  return 0;
}
