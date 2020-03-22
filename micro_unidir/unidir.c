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

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "unidir.h"
#include "../common/microbench.h"
#include "../common/socket_shim.h"

#define BATCH_SIZE 32
#define MAX_PENDING 32
//#define PRINT_STATS

struct connection {
  int cfd;
  size_t allocated;
  struct connection *next;
  struct connection *next_tx;
  short open;
  short id;
};

struct context {
  int cn;
  int ep;
  int lfd;
  ssctx_t sc;
  struct connection *conns;
  struct connection *conns_unused;

  uint64_t bytes;
  uint64_t sum;
  uint64_t *conn_cnts;

  uint32_t conns_open;
  uint32_t conns_closed;
  volatile uint32_t kop;

#ifdef PRINT_STATS
  uint64_t batches;
  uint64_t n_ops;
  uint64_t n_ops_short;
#endif
};

static struct params params;
static int start_tx = 0;

static inline uint64_t touch(const void *buf, size_t len)
{
  uint64_t x = 0;
  size_t off = 0, avail;

  while (off < len) {
    avail = len - off;
    if (avail >= 32) {
      x += *(const uint64_t *) ((const uint8_t *) buf + off);
      x += *(const uint64_t *) ((const uint8_t *) buf + off + 8);
      x += *(const uint64_t *) ((const uint8_t *) buf + off + 16);
      x += *(const uint64_t *) ((const uint8_t *) buf + off + 24);
      off += 32;
    } else if (avail >= 16) {
      x += *(const uint64_t *) ((const uint8_t *) buf + off);
      x += *(const uint64_t *) ((const uint8_t *) buf + off + 8);
      off += 16;
    } else if (avail >= 8) {
      x += *(const uint64_t *) ((const uint8_t *) buf + off);
      off += 8;
    } else if (avail >= 4) {
      x += *(const uint32_t *) ((const uint8_t *) buf + off);
      off += 4;
    } else if (avail >= 2) {
      x += *(const uint16_t *) ((const uint8_t *) buf + off);
      off += 2;
    } else {
      x += *(const uint8_t *) buf + off;
      off++;
    }
  }
  return x;
}

/** open non-blocking listening socket */
static void open_listening(struct context *c)
{
  int fd, ret;
  struct sockaddr_in si;
  ss_epev_t ev;

  if ((fd = ss_socket(c->sc, AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    fprintf(stderr, "[%d] socket failed\n", c->cn);
    abort();
  }

  /* set reuse port (for linux to open multiple listening sockets) */
  if (ss_set_reuseport(c->sc, fd) != 0) {
    fprintf(stderr, "[%d] set reuseport failed\n", c->cn);
    abort();
  }

  /* set non blocking */
  if ((ret = ss_set_nonblock(c->sc, fd)) < 0) {
    fprintf(stderr, "[%d] setsock_nonblock failed: %d\n", c->cn, ret);
    abort();
  }

  memset(&si, 0, sizeof(si));
  si.sin_family = AF_INET;
  si.sin_port = htons(params.port);

  /* bind socket */
  if ((ret = ss_bind(c->sc, fd, (struct sockaddr *) &si, sizeof(si)))
      < 0)
  {
    fprintf(stderr, "[%d] bind failed: %d\n", c->cn, ret);
    abort();
  }

  /* listen on socket */
  if ((ret = ss_listen(c->sc, fd, params.conns)) < 0) {
    fprintf(stderr, "[%d] listen failed: %d\n", c->cn, ret);
    abort();
  }

  /* add listening socket to epoll */
  ev.events = SS_EPOLLIN | SS_EPOLLERR | SS_EPOLLHUP;
  ev.data.ptr = NULL;
  if (ss_epoll_ctl(c->sc, c->ep, SS_EPOLL_CTL_ADD, fd, &ev) < 0) {
    fprintf(stderr, "[%d] mtcp_epoll_ctl\n", c->cn);
    abort();
  }

  c->lfd = fd;
}

static void prepare_context(struct context *c)
{
  int i, cn = c->cn;
  struct connection *co;
#ifdef USE_MTCP
  int ret;
#endif

#ifdef USE_MTCP
  /* get mtcp context ready */
  if ((ret = mtcp_core_affinitize(cn)) != 0) {
    fprintf(stderr, "[%d] mtcp_core_affinitize failed: %d\n", cn, ret);
    abort();
  }

  if ((c->sc = mtcp_create_context(cn)) == NULL) {
    fprintf(stderr, "[%d] mtcp_create_context failed\n", cn);
    abort();
  }
#endif

  /* allocate connection structs */
  for (i = 0; i < params.conns; i++) {
    if ((co = calloc(1, sizeof(*co))) == NULL) {
      fprintf(stderr, "[%d] alloc of connection structs failed\n", cn);
      abort();
    }
    co->open = 0;
    co->id = i;
    co->next = c->conns_unused;
    c->conns_unused = co;
  }

  /* prepare epoll */
  if ((c->ep = ss_epoll_create(c->sc, params.conns + 1)) < 0) {
    fprintf(stderr, "[%d] epoll_create failed\n", cn);
    abort();
  }

  /* prepare listening socket */
  if (params.ip == 0) {
    open_listening(c);
  }
}

static inline void conn_connected(struct context *c, struct connection *co)
{
  ss_epev_t ev;

  co->next = c->conns;
  c->conns = co;
  c->conns_open++;

  co->open = 1;

  if (!params.tx && params.ip != 0) {
    ev.data.ptr = co;
    ev.events = SS_EPOLLIN | SS_EPOLLERR | SS_EPOLLHUP;
    if (ss_epoll_ctl(c->sc, c->ep, SS_EPOLL_CTL_MOD, co->cfd, &ev) < 0) {
     fprintf(stderr, "[%d] epoll_ctl CA\n", c->cn);
      abort();
    }
  }
}

static inline void conn_connect(struct context *c)
{
  int fd, cn, ret;
  ssctx_t sc;
  ss_epev_t ev;
  struct connection *co;
  struct sockaddr_in addr;

  co = c->conns_unused;
  c->conns_unused = co->next;
  c->conns_closed--;

  cn = c->cn;
  sc = c->sc;

  /* prepare address */
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = params.ip;
  addr.sin_port = htons(params.port);

  /* create socket */
  if ((fd = ss_socket(sc, AF_INET, SOCK_STREAM, IPPROTO_TCP))
      < 0)
  {
    perror("creating socket failed");
    fprintf(stderr, "[%d] socket failed\n", cn);
    abort();
  }
  co->cfd = fd;

  /* make socket non-blocking */
  if ((ret = ss_set_nonblock(sc, fd)) != 0) {
    fprintf(stderr, "[%d] set_nonblock failed: %d\n", cn, ret);
    abort();
  }

  /* disable nagling */
  if (ss_set_nonagle(sc, fd) != 0) {
    fprintf(stderr, "[%d] setsockopt TCP_NODELAY failed\n", cn);
    abort();
  }

  /* add to epoll */
  ev.data.ptr = co;
  ev.events = SS_EPOLLIN | SS_EPOLLOUT | SS_EPOLLHUP | SS_EPOLLERR;
  if (ss_epoll_ctl(sc, c->ep, SS_EPOLL_CTL_ADD, fd, &ev) < 0) {
    fprintf(stderr, "[%d] adding to epoll failed\n", cn);
  }

  /* initiate non-blocking connect*/
  ret = ss_connect(sc, fd, (struct sockaddr *) &addr, sizeof(addr));
  if (ret == 0) {
    conn_connected(c, co);
  } else if (ret < 0 && errno == EINPROGRESS) {
    /* still going on */
  } else {
    /* opening connection failed */
    fprintf(stderr, "[%d] connect failed: %d\n", cn, ret);
    abort();
  }
}

static inline void accept_connections(struct context *co)
{
  int cfd;
  struct connection *c;
  ss_epev_t ev;

  while (co->conns_unused != NULL) {
    if ((cfd = ss_accept(co->sc, co->lfd, NULL, NULL)) < 0) {
      if (errno == EAGAIN) {
        break;
      }

      perror("accpt failed");
      fprintf(stderr, "[%d] accept failed: %d\n", co->cn, cfd);
      abort();
    }

    if (ss_set_nonblock(co->sc, cfd) < 0) {
      fprintf(stderr, "[%d] set nonblock failed\n", co->cn);
      abort();
    }

    if (ss_set_nonagle(co->sc, cfd) < 0) {
      fprintf(stderr, "[%d] set nonagle failed\n", co->cn);
      abort();
    }

    c = co->conns_unused;
    co->conns_unused = c->next;
    co->conns_closed--;

    /* add socket to epoll */
    ev.data.ptr = c;
    ev.events = SS_EPOLLIN | SS_EPOLLERR | SS_EPOLLHUP |
      (params.tx ? SS_EPOLLOUT : 0);
    if (ss_epoll_ctl(co->sc, co->ep, SS_EPOLL_CTL_ADD, cfd, &ev) < 0) {
     fprintf(stderr, "[%d] epoll_ctl CA\n", co->cn);
      abort();
    }

    c->cfd = cfd;

    conn_connected(co, c);
  }

  /* if we can't accept more connections, remove lfd from epoll */
  if (co->conns_unused == NULL) {
    if (ss_epoll_ctl(co->sc, co->ep, SS_EPOLL_CTL_DEL, co->lfd, &ev) < 0) {
      fprintf(stderr, "[%d] epoll_ctl lfd del failed\n", co->cn);
      abort();
    }
  }
}

static void listener_event(struct context *ctx, uint32_t event)
{
  if ((event & (SS_EPOLLERR | SS_EPOLLHUP)) != 0) {
    fprintf(stderr, "listener event failed\n");
    abort();
  }

  accept_connections(ctx);
}

static void *loop_receive(void *data)
{
  struct context *ctx = data;
  struct connection *c;
  ss_epev_t evs[BATCH_SIZE];
  ss_epev_t *ev;
  ssctx_t sc;
  int ret, i, ep, status;
  socklen_t slen;
  ssize_t bytes;
  void *buf;
  uint64_t bytes_rx_bump;
#ifdef PRINT_STATS
  uint32_t n_ops, n_short;
#endif

  if ((buf = malloc(params.bytes)) == NULL) {
    fprintf(stderr, "loop: malloc buf failed\n");
    abort();
  }

  prepare_context(ctx);

  sc = ctx->sc;
  ep = ctx->ep;

  while (1) {
    /* open connections if necessary:
     *  - not in listening mode
     *  - haven't reached the connection limit
     *  - maximum pending connect limit not reached
     */
    while (params.ip != 0 && ctx->conns_open < params.conns &&
        ctx->conns_closed > 0 &&
        (params.conns - ctx->conns_open - ctx->conns_closed) < MAX_PENDING)
    {
      conn_connect(ctx);
    }


    if ((ret = ss_epoll_wait(sc, ep, evs, BATCH_SIZE, -1)) < 0) {
      fprintf(stderr, "[%d] epoll_wait failed\n", ctx->cn);
      abort();
    }

    bytes_rx_bump = 0;
#ifdef PRINT_STATS
    n_ops = n_short = 0;
#endif
    for (i = 0; i < ret; i++) {
      ev = &evs[i];
      c = ev->data.ptr;

      /* catch events on listening socket */
      if (c == NULL) {
        listener_event(ctx, ev->events);
        continue;
      }

      if ((ev->events & (SS_EPOLLERR | SS_EPOLLHUP)) != 0) {
        fprintf(stderr, "error on connection\n");
        abort();
      }

      if ((ev->events & SS_EPOLLOUT) != 0) {
        slen = sizeof(status);
        if (ss_getsockopt(sc, c->cfd, SOL_SOCKET, SO_ERROR, &status, &slen) < 0) {
            perror("getsockopt failed");
            abort();
        }
        if (status != 0) {
          fprintf(stderr, "connect failed asynchronously\n");
          abort();
        }

        conn_connected(ctx, c);
      }

      if ((ev->events & SS_EPOLLIN) != 0) {
        bytes = ss_read(sc, c->cfd, buf, params.bytes);
        if (bytes > 0) {
          ctx->conn_cnts[c->id] += bytes;
          ctx->sum += touch(buf, bytes);
          bytes_rx_bump += bytes;
#ifdef PRINT_STATS
          n_ops++;
          if (bytes < params.bytes) {
            n_short++;
          }
#endif
        } else if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
          /*fprintf(stderr, "weird got would block\n");*/
        } else {
          abort();
        }
      }
    }

    if (params.op_delay > 0) {
      ctx->kop = kill_cycles(params.op_delay * bytes_rx_bump / params.bytes,
          ctx->kop);
    }

    __sync_fetch_and_add(&ctx->bytes, bytes_rx_bump);
#ifdef PRINT_STATS
    __sync_fetch_and_add(&ctx->batches, 1);
    __sync_fetch_and_add(&ctx->n_ops, n_ops);
    __sync_fetch_and_add(&ctx->n_ops_short, n_short);
#endif
  }
  return NULL;
}

static void *loop_send(void *data)
{
  struct context *ctx = data;
  struct connection *c;
  ss_epev_t evs[BATCH_SIZE];
  ss_epev_t *ev;
  ssctx_t sc;
  int ret, i, ep, status;
  socklen_t slen;
  ssize_t bytes;
  void *buf;
  uint64_t bytes_rx_bump;
  uint64_t bytes_tx_bump;
#ifdef PRINT_STATS
  uint32_t n_ops, n_short;
#endif

  if ((buf = malloc(params.bytes)) == NULL) {
    fprintf(stderr, "loop: malloc buf failed\n");
    abort();
  }

  prepare_context(ctx);

  sc = ctx->sc;
  ep = ctx->ep;

  while (1) {
    /* open connections if necessary:
     *  - not in listening mode
     *  - haven't reached the connection limit
     *  - maximum pending connect limit not reached
     */
    while (params.ip != 0 && ctx->conns_open < params.conns &&
        ctx->conns_closed > 0 &&
        (params.conns - ctx->conns_open - ctx->conns_closed) < MAX_PENDING)
    {
      conn_connect(ctx);
    }


    if ((ret = ss_epoll_wait(sc, ep, evs, BATCH_SIZE, -1)) < 0) {
      fprintf(stderr, "[%d] epoll_wait failed\n", ctx->cn);
      abort();
    }

    bytes_rx_bump = 0;
    bytes_tx_bump = 0;
#ifdef PRINT_STATS
    n_ops = n_short = 0;
#endif
    for (i = 0; i < ret; i++) {
      ev = &evs[i];
      c = ev->data.ptr;

      /* catch events on listening socket */
      if (c == NULL) {
        listener_event(ctx, ev->events);
        continue;
      }

      if ((ev->events & (SS_EPOLLERR | SS_EPOLLHUP)) != 0) {
        fprintf(stderr, "error on connection\n");
        abort();
      }

      if ((ev->events & SS_EPOLLOUT) != 0) {
        if (c->open == 0) {
          slen = sizeof(status);
          if (ss_getsockopt(sc, c->cfd, SOL_SOCKET, SO_ERROR, &status, &slen) < 0) {
              perror("getsockopt failed");
              abort();
          }
          if (status != 0) {
            fprintf(stderr, "connect failed asynchronously\n");
            abort();
          }

          conn_connected(ctx, c);
        }

        if (start_tx) {
          memset(buf, 1, params.bytes);
          bytes = ss_write(sc, c->cfd, buf, params.bytes);
          if (bytes > 0) {
            ctx->conn_cnts[c->id] += bytes;
            bytes_tx_bump += bytes;
#ifdef PRINT_STATS
            n_ops++;
            if (bytes < params.bytes)
              n_short++;
#endif
          } else if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            /*fprintf(stderr, "weird got would block\n");*/
          } else {
            perror("write failed");
            abort();
          }
        }
      }

      if ((ev->events & SS_EPOLLIN) != 0) {
        bytes = ss_read(sc, c->cfd, buf, params.bytes);
        if (bytes > 0) {
          ctx->sum += touch(buf, bytes);
          bytes_rx_bump += bytes;
        } else if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
          /*fprintf(stderr, "weird got would block\n");*/
        } else {
          perror("read failed");
          abort();
        }
      }
    }

    if (params.op_delay > 0) {
      ctx->kop = kill_cycles(bytes_tx_bump * params.op_delay / params.bytes,
          ctx->kop);
    }

    __sync_fetch_and_add(&ctx->bytes, bytes_tx_bump);
#ifdef PRINT_STATS
    __sync_fetch_and_add(&ctx->batches, 1);
    __sync_fetch_and_add(&ctx->n_ops, n_ops);
    __sync_fetch_and_add(&ctx->n_ops_short, n_short);
#endif
  }
  return NULL;
}

int main(int argc, char *argv[])
{
  struct context *ctx;
  struct context **contexts;
  uint64_t bytes, c_open, c_closed, *conn_cnts, conn_min, conn_max, conn_diff;
#ifdef PRINT_STATS
  uint64_t stat_batches, stat_ops, stat_short;
#endif
  double jfi, jfi_sosq;
  pthread_t thread;
  void *(*loop)(void *);
  uint32_t i, j;
#ifdef USE_MTCP
  int ret;
#endif

  if (parse_params(argc, argv, &params) != 0) {
    return -1;
  }

#ifdef USE_MTCP
  if ((ret = mtcp_init("/tmp/mtcp.conf")) != 0) {
    fprintf(stderr, "mtcp_init failed: %d\n", ret);
    return EXIT_FAILURE;
  }
#endif

  loop = (params.tx ? loop_send : loop_receive);

  if ((contexts = calloc(params.threads, sizeof(*contexts))) == NULL) {
    fprintf(stderr, "calloc with contexts failed\n");
    return EXIT_FAILURE;
  }
  if ((conn_cnts = calloc(params.conns * params.threads, sizeof(*conn_cnts)))
      == NULL)
  {
    fprintf(stderr, "calloc with conn_cnts failed\n");
    return EXIT_FAILURE;
  }

  for (i = 0; i < params.threads; i++) {
    if ((ctx = calloc(1, sizeof(*ctx))) == NULL) {
      fprintf(stderr, "calloc context failed\n");
      return EXIT_FAILURE;
    }
    if ((ctx->conn_cnts = calloc(params.conns, sizeof(*ctx->conn_cnts)))
        == NULL)
    {
      fprintf(stderr, "calloc context counts failed\n");
      return EXIT_FAILURE;
    }

    contexts[i] = ctx;
    ctx->cn = i;
    ctx->conns_closed = params.conns;

    if (pthread_create(&thread, NULL, loop, ctx) != 0) {
      fprintf(stderr, "pthread_create failed\n");
      return -1;
    }
  }

  printf("unidir ready\n");
  fflush(stdout);

  if (params.tx_delay > 0 && params.tx) {
    sleep(params.tx_delay);
  }
  start_tx = 1;

  while (1) {
    sleep(1);
    bytes = c_open = c_closed = 0;
#ifdef PRINT_STATS
    stat_batches = stat_ops = stat_short = 0;
#endif

    conn_min = UINT64_MAX;
    conn_max = 0;
    jfi_sosq = jfi = 0;

    for (i = 0; i < params.threads; i++) {
      ctx = contexts[i];
      bytes += __sync_fetch_and_and(&ctx->bytes, 0);
      c_open += ctx->conns_open;
      c_closed += ctx->conns_closed;

      for (j = 0; j < params.conns; j++) {
        conn_diff = ctx->conn_cnts[j] - conn_cnts[params.conns * i + j];
        conn_cnts[params.conns * i + j] = ctx->conn_cnts[j];
        if (conn_diff < conn_min) {
          conn_min = conn_diff;
        }
        if (conn_diff > conn_max) {
          conn_max = conn_diff;
        }
        jfi += conn_diff;
        jfi_sosq += (double) conn_diff * conn_diff;
      }

#ifdef PRINT_STATS
      stat_batches += __sync_fetch_and_and(&ctx->batches, 0);
      stat_ops += __sync_fetch_and_and(&ctx->n_ops, 0);
      stat_short += __sync_fetch_and_and(&ctx->n_ops_short, 0);
#endif
    }

    if (jfi != 0) {
      jfi = (jfi * jfi) / (params.conns * params.threads * jfi_sosq);
    }

    printf("bytes=%"PRIu64" conns_open=%"PRIu64" conns_closed=%"PRIu64
        " conn_bytes_min=%"PRIu64" conn_bytes_max=%"PRIu64
        " jain_fairness=%lf\n", bytes, c_open, c_closed, conn_min, conn_max,
        jfi);

#ifdef PRINT_STATS
    printf("  stats: batches=%"PRIu64" ops=%"PRIu64" short=%"PRIu64"\n",
        stat_batches, stat_ops, stat_short);
#endif

    fflush(stdout);
  }

  return 0;
}
