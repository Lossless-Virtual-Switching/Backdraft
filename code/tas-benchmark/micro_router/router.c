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

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <rte_config.h>
#include <rte_memcpy.h>
#include <rte_malloc.h>
#include <rte_lcore.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_spinlock.h>
#include <rte_ip.h>
#include <rte_cycles.h>

#include <utils.h>
#include <utils_rng.h>
#include <packet_defs.h>

#define BUFFER_SIZE 1500
#define PERTHREAD_MBUFS 2048
#define MBUF_SIZE (BUFFER_SIZE + sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM)
#define RX_DESCRIPTORS 128
#define TX_DESCRIPTORS 128

#define TIMESTAMP_BITS 32
#define TIMESTAMP_MASK 0xFFFFFFFF

#define BATCH_SIZE 32

struct thread {
  struct utils_rng rng;
  uint32_t ts_virtual;
  uint32_t ts_real;
  uint16_t qid;
};

struct queue {
  struct rte_mbuf **bufs;
  uint32_t head;
  uint32_t num;
  uint32_t len;
  uint32_t stat_qlen;

  uint64_t mac;
  uint32_t rate;
  beui32_t ip;
  uint32_t ts;
};

enum qdiscs {
  QD_TAILDROP,
  QD_RED,
  QD_DCTCP,
};

static int network_init(unsigned num_rx, unsigned num_tx);
static int network_rx_thread_init(unsigned queue, struct rte_mempool *pool);
static int network_tx_thread_init(unsigned queue);
static struct rte_mempool *mempool_alloc(size_t count);
static inline int queue_disc(struct thread *t, struct queue *q,
    struct pkt_ip *pi);

static const uint8_t port_id = 0;
static const struct rte_eth_conf port_conf = {
    .rxmode = {
      .mq_mode = ETH_MQ_RX_RSS,
    },
    .txmode = {
      .mq_mode = ETH_MQ_TX_NONE,
    },
    .rx_adv_conf = {
      .rss_conf = {
        .rss_hf = ETH_RSS_TCP,
      },
    }
  };

static struct rte_eth_dev_info eth_devinfo;
static struct ether_addr eth_addr;
static size_t queues_num;
static struct queue *queues;

static uint64_t stat_tail_drops = 0;
static uint64_t stat_qd_drops = 0;
static uint64_t stat_rnd_drops = 0;

static enum qdiscs qdisc = QD_TAILDROP;
static uint32_t red_min = 50;
static uint32_t red_max = 150;
static uint32_t red_prob = UINT32_MAX / 10;
static int red_smooth = 0;
static int red_ecn = 0;
static uint32_t dctcp_k = 64;
static uint32_t rnd_drop_prob = 0;

static int network_init(unsigned num_rx, unsigned num_tx)
{
  int ret;
  unsigned count;

  /* make sure there is only one port */
  count = rte_eth_dev_count();
  if (count == 0) {
    fprintf(stderr, "No ethernet devices\n");
    return -1;
  } else if (count > 1) {
    fprintf(stderr, "Multiple ethernet devices\n");
    return -1;
  }

  /* initialize port */
  ret = rte_eth_dev_configure(port_id, num_rx, num_tx, &port_conf);
  if (ret < 0) {
    fprintf(stderr, "rte_eth_dev_configure failed\n");
    return -1;
  }

  /* get mac address and device info */
  rte_eth_macaddr_get(port_id, &eth_addr);
  rte_eth_dev_info_get(port_id, &eth_devinfo);
  eth_devinfo.default_txconf.txq_flags = ETH_TXQ_FLAGS_NOVLANOFFL;

  return 0;
}

static int network_rx_thread_init(unsigned queue, struct rte_mempool *pool)
{
  int ret;

  /* initialize queue */
  ret = rte_eth_rx_queue_setup(port_id, queue, RX_DESCRIPTORS,
          rte_socket_id(), &eth_devinfo.default_rxconf, pool);
  if (ret != 0) {
    return -1;
  }

  return 0;
}

static int network_tx_thread_init(unsigned queue)
{
  int ret;

  /* initialize queue */
  ret = rte_eth_tx_queue_setup(port_id, queue, TX_DESCRIPTORS,
          rte_socket_id(), &eth_devinfo.default_txconf);
  if (ret != 0) {
    fprintf(stderr, "network_tx_thread_init: rte_eth_tx_queue_setup failed\n");
    return -1;
  }

  return 0;
}

static struct rte_mempool *mempool_alloc(size_t bufs)
{
  static unsigned pool_id = 0;
  unsigned n;
  char name[32];
  n = __sync_fetch_and_add(&pool_id, 1);
  snprintf(name, 32, "mbuf_pool_%u\n", n);
  return rte_mempool_create(name, bufs, MBUF_SIZE, 32,
          sizeof(struct rte_pktmbuf_pool_private), rte_pktmbuf_pool_init, NULL,
          rte_pktmbuf_init, NULL, rte_socket_id(), 0);
}

static inline int is_local_ip(beui32_t bip)
{
  uint32_t i;
  uint32_t ip = f_beui32(bip);

  if ((ip & 0xff) != 254)
    return 0;

  for (i = 0; i < queues_num; i++) {
    if ((f_beui32(queues[i].ip) >> 8) == (ip >> 8))
      return 1;
  }

  return 0;
}

static inline void handle_rx_arp(struct thread *t, struct rte_mbuf *mb)
{
  struct pkt_arp *pa = rte_pktmbuf_mtod(mb, struct pkt_arp *);
  beui32_t sip;

  if (f_beui16(pa->arp.oper) != ARP_OPER_REQUEST ||
      f_beui16(pa->arp.htype) != ARP_HTYPE_ETHERNET ||
      f_beui16(pa->arp.ptype) != ARP_PTYPE_IPV4 ||
      pa->arp.hlen != 6 || pa->arp.plen != 4 ||
      !is_local_ip(pa->arp.tpa))
  {
    rte_pktmbuf_free(mb);
    return;
  }

  fprintf(stderr, "handle_rx_arp: answering ARP for %x\n",
      f_beui32(pa->arp.tpa));
  memcpy(&pa->eth.dest, &pa->arp.sha, ETH_ADDR_LEN);
  memcpy(&pa->arp.tha, &pa->arp.sha, ETH_ADDR_LEN);
  memcpy(&pa->eth.src, &eth_addr, ETH_ADDR_LEN);
  memcpy(&pa->arp.sha, &eth_addr, ETH_ADDR_LEN);
  sip = pa->arp.spa;
  pa->arp.spa = pa->arp.tpa;
  pa->arp.tpa = sip;
  pa->arp.oper = t_beui16(ARP_OPER_REPLY);

  if (rte_eth_tx_burst(port_id, t->qid, &mb, 1) != 1) {
    fprintf(stderr, "handle_rx_arp: tx failed\n");
    rte_pktmbuf_free(mb);
  }
}

static inline struct queue *find_queue(beui32_t dip)
{
  uint32_t i;
  for (i = 0; i < queues_num; i++) {
    if (queues[i].ip.x == dip.x)
      return &queues[i];
  }
  return NULL;
}

static inline uint32_t queue_new_ts(struct thread *t, struct queue *q,
    uint32_t bytes)
{
  return t->ts_virtual + ((uint64_t) bytes * 8 * 1000000) / q->rate;
}

static inline uint32_t timestamp(void)
{
  static uint64_t freq = 0;
  uint64_t cycles = rte_get_tsc_cycles();

  if (freq == 0)
    freq = rte_get_tsc_hz();

  cycles *= 1000000000ULL;
  cycles /= freq;
  return cycles;
}

/** Relative timestamp, ignoring wrap-arounds */
static inline int64_t rel_time(uint32_t cur_ts, uint32_t ts_in)
{
  uint64_t ts = ts_in;
  const uint64_t middle = (1ULL << (TIMESTAMP_BITS - 1));
  uint64_t start, end;

  if (cur_ts < middle) {
    /* negative interval is split in half */
    start = (cur_ts - middle) & TIMESTAMP_MASK;
    end = (1ULL << TIMESTAMP_BITS);
    if (start <= ts && ts < end) {
      /* in first half of negative interval, smallest timestamps */
      return ts - start - middle;
    } else {
      /* in second half or in positive interval */
      return ts - cur_ts;
    }
  } else if (cur_ts == middle) {
    /* intervals not split */
    return ts - cur_ts;
  } else {
    /* higher interval is split */
    start = 0;
    end = ((cur_ts + middle) & TIMESTAMP_MASK) + 1;
    if (start <= cur_ts && ts < end) {
      /* in second half of positive interval, largest timestamps */
      return ts + ((1ULL << TIMESTAMP_BITS) - cur_ts);
    } else {
      /* in negative interval or first half of positive interval */
      return ts - cur_ts;
    }
  }
}

static inline int timestamp_lessthaneq(struct thread *t, uint32_t a,
    uint32_t b)
{
  return rel_time(t->ts_virtual, a) <= rel_time(t->ts_virtual, b);
}

static inline void handle_rx_ip(struct thread *t, struct rte_mbuf *mb)
{
  struct pkt_ip *pi = rte_pktmbuf_mtod(mb, struct pkt_ip *);
  struct queue *q;
  uint32_t max_ts;

  /* random drops, if enabled */
  if (rnd_drop_prob != 0) {
    if (utils_rng_gen32(&t->rng) <= rnd_drop_prob) {
      stat_rnd_drops++;
      goto drop;
    }
  }

  if (memcmp(&pi->eth.dest, &eth_addr, ETH_ADDR_LEN) != 0 ||
      (q = find_queue(pi->ip.dest)) == NULL)
  {
    goto drop;
  }

  if (q->num >= q->len) {
    stat_tail_drops++;
    goto drop;
  }


  /* queuing discipline based drops/marks */
  if (queue_disc(t, q, pi) != 0) {
    stat_qd_drops++;
    goto drop;
  }

  /* update macs */
  memcpy(&pi->eth.src, &eth_addr, ETH_ADDR_LEN);
  memcpy(&pi->eth.dest, &q->mac, ETH_ADDR_LEN);

  /* add to queue */
  q->bufs[q->head] = mb;
  q->num++;
  if (++q->head >= q->len) {
    q->head -= q->len;
  }

  /* if queue was formerly empty, ensure it has a reasonable ts */
  if (q->num == 1) {
    /* make sure queue has a reasonable next_ts:
     *  - not in the past
     *  - not more than if it just sent max_chunk at the current rate
     */
    max_ts = queue_new_ts(t, q, BUFFER_SIZE);
    if (timestamp_lessthaneq(t, q->ts, t->ts_virtual)) {
        q->ts = t->ts_virtual;
    } else if (!timestamp_lessthaneq(t, q->ts, max_ts)) {
        q->ts = max_ts;
    }

  }
  return;

drop:
  rte_pktmbuf_free(mb);
  return;
}

static void receive_packets(struct thread *t)
{
  unsigned num, i;
  struct rte_mbuf *mbs[BATCH_SIZE];
  struct eth_hdr *eh;

  num = rte_eth_rx_burst(port_id, t->qid, mbs, BATCH_SIZE);

  for (i = 0; i < num; i++) {
    eh = rte_pktmbuf_mtod(mbs[i], struct eth_hdr *);
    if (f_beui16(eh->type) == ETH_TYPE_IP) {
      handle_rx_ip(t, mbs[i]);
    } else if (f_beui16(eh->type) == ETH_TYPE_ARP) {
      handle_rx_arp(t, mbs[i]);
    } else {
      rte_pktmbuf_free(mbs[i]);
    }
  }
}

static inline struct queue *min_queue(struct thread *t)
{
  struct queue *q = NULL;
  int64_t q_rts, rts;
  size_t i;

  for (i = 0; i < queues_num; i++) {
    if (queues[i].num > 0 && (q == NULL ||
          (rts = rel_time(t->ts_virtual, queues[i].ts)) < q_rts))
    {
      q = &queues[i];
      q_rts = rts;
    }
  }
  return q;
}

static void transmit_packets(struct thread *t)
{
  struct queue *q;
  uint32_t max_vts, cur_ts;
  struct rte_mbuf *mbs[BATCH_SIZE];
  size_t num = 0, i, pos;

  cur_ts = timestamp();
  max_vts = t->ts_virtual + (cur_ts - t->ts_real);

  /* find packets to send */
  while (num < BATCH_SIZE) {
    q = min_queue(t);
    if (q == NULL || !timestamp_lessthaneq(t, q->ts, max_vts)) {
      t->ts_virtual = max_vts;
      break;
    }

    if (q->num <= q->head) {
      pos = q->head - q->num;
    } else {
      pos = q->len + q->head - q->num;
    }
    mbs[num++] = q->bufs[pos];
    q->num--;

    t->ts_virtual = q->ts;

    q->ts = queue_new_ts(t, q, rte_pktmbuf_pkt_len(q->bufs[pos]));
  }

  /* update virtual timestamp correctly */
  if (num == BATCH_SIZE) {
    q = min_queue(t);
    if (q != NULL && timestamp_lessthaneq(t, q->ts, max_vts)) {
      t->ts_virtual = q->ts;
    } else {
      t->ts_virtual = max_vts;
    }
  }
  t->ts_real = cur_ts;

  /* send packets */
  if ((i = rte_eth_tx_burst(port_id, t->qid, mbs, num)) < num) {
    fprintf(stderr, "transmit_packets: dropping because of full tx queue "
        "(%zu)\n", num - i);
    for (; i < num; i++) {
      rte_pktmbuf_free(mbs[i]);
    }
  }

  /* measure queue lengths */
  for (i = 0; i < queues_num; i++) {
    queues[i].stat_qlen += (queues[i].num << 16) / 1024 -  queues[i].stat_qlen / 1024;
  }

}

static int run_thread(void *arg)
{
  struct thread t = {
      .qid = 0,
      .ts_virtual = 0,
      .ts_real = timestamp(),
    };
  utils_rng_init(&t.rng, 0x123456789abcdefULL);
  while (1) {
    receive_packets(&t);
    transmit_packets(&t);
  }
  return 0;
}

static inline int parse_int32(const char *s, uint32_t *pi)
{
  char *end;
  *pi = strtoul(s, &end, 10);
  if (!*s || *end)
    return -1;
  return 0;
}

static inline int parse_double(const char *s, double *pd)
{
  char *end;
  *pd = strtod(s, &end);
  if (!*s || *end)
    return -1;
  return 0;
}

static int parse_queue(char *arg, struct queue *q)
{
  char *mac, *len, *rate;
  uint32_t ip;

  /* separate parts by commas */
  if ((mac = strchr(arg, ',')) == NULL ||
      (len = strchr(mac + 1, ',')) == NULL ||
      (rate = strchr(len + 1, ',')) == NULL)
  {
    fprintf(stderr, "Not all 3 commas in argument found\n");
    return -1;
  }
  *mac = *len = *rate = 0;
  mac++;
  len++;
  rate++;

  /* parse IP */
  if (util_parse_ipv4(arg, &ip) != 0) {
    fprintf(stderr, "parsing IP failed (%s)\n", arg);
    return -1;
  }
  q->ip = t_beui32(ip);

  /* parse MAC */
  if (util_parse_mac(mac, &q->mac) != 0) {
    fprintf(stderr, "parsing IP failed (%s)\n", mac);
    return -1;
  }

  /* parse len */
  if (parse_int32(len, &q->len) != 0) {
    fprintf(stderr, "parsing len failed (%s)\n", len);
    return -1;
  }

  /* parse rate */
  if (parse_int32(rate, &q->rate) != 0) {
    fprintf(stderr, "parsing rate failed (%s)\n", rate);
    return -1;
  }
  q->rate *= 1000;

  return 0;
}

static int parse_args(int argc, char *argv[], size_t *pqlen_total)
{
  static const char *opts = "q:r:sek:d:";
  int c, done = 0;
  unsigned i;
  char *end, *end_2, *arg;
  double d;
  size_t qlen_total = 0;

  while (!done) {
    c = getopt(argc, argv, opts);
    switch (c) {
      case 'q':
        if (!strcmp(optarg, "taildrop")) {
          qdisc = QD_TAILDROP;
        } else if (!strcmp(optarg, "red")) {
          qdisc = QD_RED;
        } else if (!strcmp(optarg, "dctcp")) {
          qdisc = QD_DCTCP;
        } else {
          fprintf(stderr, "parsing queue disc failed\n");
          return -1;
        }
        break;
      case 'r':
        if ((end = strchr(optarg, ':')) == NULL ||
            (end_2 = strchr(end + 1, ':')) == NULL)
        {
          fprintf(stderr, "splitting red parameters failed\n");
          return -1;
        }
        *end = 0;
        *end_2 = 0;

        if (parse_int32(optarg, &red_min) != 0 ||
            parse_int32(end + 1, &red_max) != 0 ||
            parse_double(end_2 + 1, &d) != 0)
        {
          fprintf(stderr, "parsing red parameters failed\n");
          return -1;
        }
        red_prob = UINT32_MAX * d;
        break;
      case 's':
        red_smooth = 1;
        break;
      case 'e':
        red_ecn = 1;
        break;
      case 'k':
        if (parse_int32(optarg, &dctcp_k) != 0) {
          fprintf(stderr, "parsing dctcp k failed\n");
          return -1;
        }
        break;
      case 'd':
        if (parse_double(optarg, &d) != 0) {
          fprintf(stderr, "parsing drop probability failed\n");
          return -1;
        }
        rnd_drop_prob = UINT32_MAX * d;
        break;
      case -1:
        done = 1;
        break;
      case '?':
        return -1;
      default:
        abort();
    }
  }

  /* allocate queues */
  queues_num = argc - optind;
  if ((queues = calloc(queues_num, sizeof(*queues))) == NULL) {
    fprintf(stderr, "calloc queues failed\n");
    return -1;
  }

  /* parse args */
  for (i = 0; i < queues_num; i++) {
    arg = argv[optind + i];
    if (parse_queue(arg, &queues[i]) != 0) {
      fprintf(stderr, "Parsing queue (%s) failed\n", arg);
      fprintf(stderr, "expect IP,MAC,LEN,RATE\n");
      return -1;
    }

    /* allocate queue memory */
    if ((queues[i].bufs = calloc(queues[i].len, sizeof(*queues[i].bufs)))
        == NULL)
    {
      fprintf(stderr, "calloc queue bufs failed\n");
      return -1;
    }
    qlen_total += queues[i].len;
  }

  qlen_total += RX_DESCRIPTORS + TX_DESCRIPTORS;
  *pqlen_total = qlen_total;

  return 0;
}

int main(int argc, char *argv[])
{
  int n;
  unsigned threads = 1, core, i;
  size_t qlen_total;
  struct rte_mempool *pool;

  if ((n = rte_eal_init(argc, argv)) < 0) {
    return -1;
  }

  if (parse_args(argc - n, argv + n, &qlen_total) < 0) {
    return -1;
  }

  /* allocate pool */
  if ((pool = mempool_alloc(qlen_total)) == NULL) {
    fprintf(stderr, "mempool_alloc failed\n");
    return -1;
  }

  /* initialize networking */
  if (network_init(threads, threads) != 0) {
    fprintf(stderr, "network_init failed\n");
    return -1;
  }

  /* initialize queues */
  for (i = 0; i < threads; i++) {
    network_rx_thread_init(i, pool);
    network_tx_thread_init(i);
  }

  /* start device */
  if (rte_eth_dev_start(port_id) != 0) {
    fprintf(stderr, "rte_eth_dev_start failed\n");
    return -1;
  }

  /* start threads */
  i = 0;
  RTE_LCORE_FOREACH_SLAVE(core) {
    if (i++ < threads) {
      if (rte_eal_remote_launch(run_thread, NULL, core) != 0) {
        return -1;
      }
    }
  }

  printf("router ready\n");
  fflush(stdout);

  while (1) {
    printf("tail_drops=%"PRIu64" qd_drops=%"PRIu64" rnd_drops=%"PRIu64"  ",
        stat_tail_drops, stat_qd_drops, stat_rnd_drops);
    for (i = 0; i < queues_num; i++) {
      printf(" q[%u]={len=%u avglen=%u}", i, queues[i].num, queues[i].stat_qlen >> 16);
    }
    printf("\n");
    fflush(stdout);
    sleep(1);
  }

  return 0;
}

static inline int disc_red(struct thread *t, struct queue *q,
    struct pkt_ip *pi)
{
  uint32_t qlen, prob;
  uint64_t x;

  /* check if ECN enabled and packet not ECN-capable */
  if (red_ecn &&
      (IPH_ECN(&pi->ip) == IP_ECN_NONE || IPH_ECN(&pi->ip) == IP_ECN_CE))
  {
    return 0;
  }

  qlen = q->stat_qlen >> 16;

  /* below threshold, don't drop */
  if (qlen < red_min) {
    return 0;
  }

  if (qlen <= red_max) {
    /* dynamic probabilty */
    x = qlen - red_min;
    x *= red_prob;
    x /= red_max - red_min;
    prob = x;
  } else if (red_smooth) {
    /* above threshold: go smooth up to 100% */
    x = qlen - red_max;
    x *= UINT_MAX - red_prob;
    x /= q->len - red_max;
    prob = red_prob + x;
    if (prob < (uint32_t) x) {
      prob = UINT_MAX;
    }
  } else {
    /* above threshold: cap at threshold */
    prob = red_prob;
  }

  if (utils_rng_gen32(&t->rng) <= prob) {
    if (!red_ecn) {
      /* drop if ECN marking disabled */
      return -1;
    } else {
      /* mark ECN */
      IPH_ECN_SET(&pi->ip, IP_ECN_CE);
    }
  }
  return 0;
}

static inline int disc_dctcp(struct thread *t, struct queue *q,
    struct pkt_ip *pi)
{
  uint32_t qlen;

  if (IPH_ECN(&pi->ip) == IP_ECN_NONE || IPH_ECN(&pi->ip) == IP_ECN_CE) {
    return 0;
  }

  qlen = q->stat_qlen >> 16;
  if (qlen >= dctcp_k) {
    /* mark ECN */
    IPH_ECN_SET(&pi->ip, IP_ECN_CE);
  }

  return 0;
}

static inline int queue_disc(struct thread *t, struct queue *q,
    struct pkt_ip *pi)
{
  if (qdisc == QD_RED) {
    return disc_red(t, q, pi);
  } else if (qdisc == QD_DCTCP) {
    return disc_dctcp(t, q, pi);
  }

  return 0;
}
