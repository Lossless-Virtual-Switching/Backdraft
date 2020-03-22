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

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <tas_ll.h>
#include <utils.h>

#include "../common/microbench.h"
#include "unidir.h"

#define BATCH_SIZE 32
#define MAX_PENDING 32

struct connection {
  struct flextcp_connection conn;
  size_t allocated;
  struct connection *next;
  struct connection *next_tx;
  int id;
};

struct context {
  struct flextcp_context ctx;
  struct flextcp_listener listen;
  struct connection *conns;
  struct connection *conns_unused;
  struct connection *tx_first;
  struct connection *tx_last;

  uint64_t bytes;
  uint64_t sum;
  uint64_t *conn_cnts;

  uint32_t conns_open;
  uint32_t conns_closed;
  volatile uint32_t kop;
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

static void *loop_receive(void *data)
{
  struct context *ctx = data;
  struct connection *c;
  struct flextcp_event evs[BATCH_SIZE];
  struct flextcp_event *ev;
  int num, i;
  size_t len;
  void *buf;
  uint64_t bytes_rx_bump;

  ctx->conns_closed = params.conns;

  /* initialize listener, if necessary */
  if (params.ip == 0) {
    if (flextcp_listen_open(&ctx->ctx, &ctx->listen, params.port,
          params.conns, 0) != 0)
    {
      fprintf(stderr, "flextcp_listen_open failed\n");
      exit(-1);
    }
  }

  while (1) {
    bytes_rx_bump = 0;

    num = flextcp_context_poll(&ctx->ctx, BATCH_SIZE, evs);
    for (i = 0;i < num; i++) {
      ev = &evs[i];
      switch (ev->event_type) {
        case FLEXTCP_EV_CONN_OPEN:
          c = (struct connection *) ev->ev.conn_open.conn;
          c->next = ctx->conns;
          ctx->conns = c;
          ctx->conns_open++;
          break;

        case FLEXTCP_EV_LISTEN_OPEN:
          break;
        case FLEXTCP_EV_LISTEN_NEWCONN:
          if (ctx->conns_closed > 0) {
            c = ctx->conns_unused;
            if (flextcp_listen_accept(&ctx->ctx, &ctx->listen, &c->conn) != 0) {
              fprintf(stderr, "flextcp_listen_accept failed\n");
              exit(-1);
            }
            ctx->conns_closed--;
            ctx->conns_unused = c->next;
          }
          break;
        case FLEXTCP_EV_LISTEN_ACCEPT:
          c = (struct connection *) ev->ev.listen_accept.conn;
          c->next = ctx->conns;
          ctx->conns = c;
          ctx->conns_open++;
          break;

        case FLEXTCP_EV_CONN_RECEIVED:
          c = (struct connection *) ev->ev.conn_received.conn;
          len = ev->ev.conn_received.len;
          buf = ev->ev.conn_received.buf;
          ctx->sum += touch(buf, len);
          bytes_rx_bump += len;
          ctx->conn_cnts[c->id] += len;
          if (flextcp_connection_rx_done(&ctx->ctx, &c->conn, len) != 0) {
            fprintf(stderr, "flextcp_connection_rx_done(%p, %zu) failed\n", c,
                len);
            exit(-1);
          }
          break;

        default:
          fprintf(stderr, "loop_receive: unexpected event (%u)\n", ev->event_type);
          break;
      }
    }

    while (params.ip != 0 && ctx->conns_open < params.conns &&
        ctx->conns_closed > 0 &&
        (params.conns - ctx->conns_open - ctx->conns_closed) < MAX_PENDING)
    {
      c = ctx->conns_unused;
      if (flextcp_connection_open(&ctx->ctx, &c->conn, ntohl(params.ip),
            params.port) != 0)
      {
        fprintf(stderr, "flextcp_connection_open failed\n");
        exit(-1);
      }
      ctx->conns_closed--;
      ctx->conns_unused = c->next;
    }

    if (params.op_delay > 0) {
      ctx->kop = kill_cycles(params.op_delay * bytes_rx_bump / params.bytes,
          ctx->kop);
    }

    __sync_fetch_and_add(&ctx->bytes, bytes_rx_bump);
  }
}

static void ctx_tx_add(struct context *ctx, struct connection *c)
{
  c->next_tx = NULL;
  if (ctx->tx_last != NULL) {
    ctx->tx_last->next_tx = c;
  } else {
    ctx->tx_first = c;
  }
  ctx->tx_last = c;
}

static void *loop_send(void *data)
{
  struct context *ctx = data;
  struct connection *c;
  struct flextcp_event evs[BATCH_SIZE];
  struct flextcp_event *ev;
  int num, i, more_tx;
  size_t len;
  ssize_t ret;
  void *buf;
  uint64_t bytes_rx_bump, bytes_tx_bump;

  ctx->conns_closed = params.conns;
  ctx->tx_first = ctx->tx_last = NULL;

  /* initialize listener, if necessary */
  if (params.ip == 0) {
    if (flextcp_listen_open(&ctx->ctx, &ctx->listen, params.port, params.conns, 0)
        != 0)
    {
      fprintf(stderr, "flextcp_listen_open failed\n");
      exit(-1);
    }
  }

  while (1) {
    bytes_rx_bump = bytes_tx_bump = 0;

    num = flextcp_context_poll(&ctx->ctx, BATCH_SIZE, evs);
    for (i = 0;i < num; i++) {
      ev = &evs[i];
      switch (ev->event_type) {
        case FLEXTCP_EV_CONN_OPEN:
          c = (struct connection *) ev->ev.conn_open.conn;
          c->next = ctx->conns;
          ctx->conns = c;
          ctx->conns_open++;
          ctx_tx_add(ctx, c);
          break;

        case FLEXTCP_EV_LISTEN_OPEN:
          break;
        case FLEXTCP_EV_LISTEN_NEWCONN:
          if (ctx->conns_closed > 0) {
            c = ctx->conns_unused;
            if (flextcp_listen_accept(&ctx->ctx, &ctx->listen, &c->conn) != 0) {
              fprintf(stderr, "flextcp_listen_accept failed\n");
              exit(-1);
            }
            ctx->conns_closed--;
            ctx->conns_unused = c->next;
          }
          break;
        case FLEXTCP_EV_LISTEN_ACCEPT:
          c = (struct connection *) ev->ev.listen_accept.conn;
          c->next = ctx->conns;
          ctx->conns = c;
          ctx->conns_open++;
          ctx_tx_add(ctx, c);
          break;

        case FLEXTCP_EV_CONN_RECEIVED:
          c = (struct connection *) ev->ev.conn_received.conn;
          len = ev->ev.conn_received.len;
          bytes_rx_bump += len;
          if (flextcp_connection_rx_done(&ctx->ctx, &c->conn, len) != 0) {
            fprintf(stderr, "flextcp_connection_rx_done(%p, %zu) failed\n", c,
                len);
            exit(-1);
          }
          break;

        case FLEXTCP_EV_CONN_SENDBUF:
          c = (struct connection *) ev->ev.conn_sendbuf.conn;
          ctx_tx_add(ctx, c);
          break;

        default:
          fprintf(stderr, "loop_receive: unexpected event (%u)\n", ev->event_type);
          break;
      }
    }

    /* transmit */
    for (c = ctx->tx_first, i = 0; c != NULL && i < BATCH_SIZE && start_tx;
        i++, c = ctx->tx_first)
    {
      ctx->tx_first = c->next_tx;
      if (ctx->tx_first == NULL) {
        ctx->tx_last = NULL;
      }

      ret = flextcp_connection_tx_alloc(&c->conn, params.bytes, &buf);
      if (ret < 0) {
        fprintf(stderr, "flextcp_connection_tx_alloc failed\n");
        exit(-1);
      }
      if (ret > 0) {
        more_tx = 1;
        memset(buf, 1, ret);
      } else {
        more_tx = 0;
      }

      c->allocated += ret;
      if (c->allocated > 0) {
        if (flextcp_connection_tx_send(&ctx->ctx, &c->conn, c->allocated) != 0) {
          more_tx = 1;
        } else {
          bytes_tx_bump += c->allocated;
          ctx->conn_cnts[c->id] += c->allocated;
          c->allocated = 0;
        }
      }

      if (more_tx) {
        ctx_tx_add(ctx, c);
      }
    }

    /* open more connections if necessary */
    while (params.ip != 0 && ctx->conns_open < params.conns &&
        ctx->conns_closed > 0 &&
        (params.conns - ctx->conns_open - ctx->conns_closed) < MAX_PENDING)
    {
      c = ctx->conns_unused;
      if (flextcp_connection_open(&ctx->ctx, &c->conn, ntohl(params.ip),
            params.port) != 0)
      {
        fprintf(stderr, "flextcp_connection_open failed\n");
        exit(-1);
      }
      ctx->conns_closed--;
      ctx->conns_unused = c->next;
    }

    if (params.op_delay > 0) {
      ctx->kop = kill_cycles(params.op_delay * bytes_tx_bump / params.bytes,
          ctx->kop);
    }
    __sync_fetch_and_add(&ctx->bytes, bytes_tx_bump);
  }
}

int main(int argc, char *argv[])
{
  struct context *ctx;
  struct context **contexts;
  struct connection *c;
  uint32_t n, i, j;
  uint64_t bytes, c_open, c_closed, *conn_cnts, conn_min, conn_max, conn_diff;
  double jfi, jfi_sosq;
  pthread_t thread;
  void *(*loop)(void *);

  if (parse_params(argc, argv, &params) != 0) {
    return -1;
  }

  loop = (params.tx ? loop_send : loop_receive);

  if (flextcp_init()) {
    fprintf(stderr, "flextcp_init failed\n");
    return -1;
  }

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
    }
    if ((ctx->conn_cnts = calloc(params.conns, sizeof(*ctx->conn_cnts)))
        == NULL)
    {
      fprintf(stderr, "calloc context counts failed\n");
      return EXIT_FAILURE;
    }

    contexts[i] = ctx;

    for (n = 0; n < params.conns; n++) {
      if ((c = calloc(1, sizeof(*c))) == NULL) {
        fprintf(stderr, "calloc failed\n");
        return -1;
      }
      c->id = n;
      c->next = ctx->conns_unused;
      ctx->conns_unused = c;
    }

    ctx->conns_closed = params.conns;

    if (flextcp_context_create(&ctx->ctx)) {
      fprintf(stderr, "flextcp_context_create failed\n");
      exit(-1);
    }

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
    }
    printf("bytes=%"PRIu64" conns_open=%"PRIu64" conns_closed=%"PRIu64
        " conn_bytes_min=%"PRIu64" conn_bytes_max=%"PRIu64
        " jain_fairness=%lf\n", bytes, c_open, c_closed, conn_min, conn_max,
        jfi);

    fflush(stdout);
  }


  return 0;
}
