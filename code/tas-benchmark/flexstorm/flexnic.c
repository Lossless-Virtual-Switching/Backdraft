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

// RAS: The only important function is dpdk_thread

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
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <inttypes.h>

#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

#ifdef USE_DPDK
#include <rte_config.h>
#include <rte_common.h>
#include <rte_log.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_memzone.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_launch.h>
#include <rte_atomic.h>
#include <rte_cycles.h>
#include <rte_prefetch.h>
#include <rte_lcore.h>
#include <rte_per_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_interrupts.h>
#include <rte_pci.h>
#include <rte_random.h>
#include <rte_debug.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_ring.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_errno.h>
#include <rte_spinlock.h>
#endif

#define FLEXNIC
#define FLEXNIC_EMULATION
#define USE_TCP

#define DROP_IF_FULL

#include "storm.h"
#include "hash.h"
#include "worker.h"

#define BATCH_SIZE	64			// in tuples
#define BATCH_DELAY	(2 * PROC_FREQ)		// in cycles
#define LINK_RTT	(130)			// in us

static struct flexnic_queue *task2inqueue[MAX_TASKS];

#ifdef USE_DPDK
#define MBUF_SIZE (2048 + sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM)
#define NB_MBUF   2048

#define MAX(a,b)	((a) < (b) ? (b) : (a))

/*
 * Configurable number of RX/TX ring descriptors
 */
#define RTE_TEST_RX_DESC_DEFAULT 128
#define RTE_TEST_TX_DESC_DEFAULT 512
static uint16_t nb_rxd = RTE_TEST_RX_DESC_DEFAULT;
static uint16_t nb_txd = RTE_TEST_TX_DESC_DEFAULT;

/* ethernet addresses of ports */
static struct ether_addr l2fwd_ports_eth_addr[RTE_MAX_ETHPORTS];

static const struct rte_eth_conf port_conf = {
  .rxmode = {
    .split_hdr_size = 0,
    .header_split   = 0, /**< Header Split disabled */
    .hw_ip_checksum = 0, /**< IP checksum offload disabled */
    .hw_vlan_filter = 0, /**< VLAN filtering disabled */
    .jumbo_frame    = 0, /**< Jumbo Frame Support disabled */
    .hw_strip_crc   = 0, /**< CRC stripped by hardware */
    .mq_mode = ETH_MQ_RX_RSS,
  },
  .txmode = {
    .mq_mode = ETH_MQ_TX_NONE,
  },
  .rx_adv_conf = {
    .rss_conf = {
      .rss_hf = ETH_RSS_IPV4,
    },
  }
};

struct rte_mempool * l2fwd_pktmbuf_pool = NULL;

#define PACK_STRUCT_FIELD(x)	x

/** Ethernet header */
struct eth_hdr {
#if ETH_PAD_SIZE
  PACK_STRUCT_FIELD(uint8_t padding[ETH_PAD_SIZE]);
#endif
  PACK_STRUCT_FIELD(struct eth_addr dest);
  PACK_STRUCT_FIELD(struct eth_addr src);
  PACK_STRUCT_FIELD(uint16_t type);
} __attribute__ ((packed));

#define SIZEOF_ETH_HDR (14 + ETH_PAD_SIZE)

struct ip_addr {
  PACK_STRUCT_FIELD(uint32_t addr);
} __attribute__ ((packed));

typedef struct ip_addr ip_addr_p_t;

struct ip_hdr {
  /* version / header length */
  PACK_STRUCT_FIELD(uint8_t _v_hl);
  /* type of service */
  PACK_STRUCT_FIELD(uint8_t _tos);
  /* total length */
  PACK_STRUCT_FIELD(uint16_t _len);
  /* identification */
  PACK_STRUCT_FIELD(uint16_t _id);
  /* fragment offset field */
  PACK_STRUCT_FIELD(uint16_t _offset);
  /* time to live */
  PACK_STRUCT_FIELD(uint8_t _ttl);
  /* protocol*/
  PACK_STRUCT_FIELD(uint8_t _proto);
  /* checksum */
  PACK_STRUCT_FIELD(uint16_t _chksum);
  /* source and destination IP addresses */
  PACK_STRUCT_FIELD(ip_addr_p_t src);
  PACK_STRUCT_FIELD(ip_addr_p_t dest); 
} __attribute__ ((packed));

#define IPH_V(hdr)  ((hdr)->_v_hl >> 4)
#define IPH_HL(hdr) ((hdr)->_v_hl & 0x0f)
#define IPH_TOS(hdr) ((hdr)->_tos)
#define IPH_LEN(hdr) ((hdr)->_len)
#define IPH_ID(hdr) ((hdr)->_id)
#define IPH_OFFSET(hdr) ((hdr)->_offset)
#define IPH_TTL(hdr) ((hdr)->_ttl)
#define IPH_PROTO(hdr) ((hdr)->_proto)
#define IPH_CHKSUM(hdr) ((hdr)->_chksum)

#define IPH_VHL_SET(hdr, v, hl) (hdr)->_v_hl = (((v) << 4) | (hl))
#define IPH_TOS_SET(hdr, tos) (hdr)->_tos = (tos)
#define IPH_LEN_SET(hdr, len) (hdr)->_len = (len)
#define IPH_ID_SET(hdr, id) (hdr)->_id = (id)
#define IPH_OFFSET_SET(hdr, off) (hdr)->_offset = (off)
#define IPH_TTL_SET(hdr, ttl) (hdr)->_ttl = (uint8_t)(ttl)
#define IPH_PROTO_SET(hdr, proto) (hdr)->_proto = (uint8_t)(proto)
#define IPH_CHKSUM_SET(hdr, chksum) (hdr)->_chksum = (chksum)

#define IP_HLEN 20

#define IP_PROTO_IP      0
#define IP_PROTO_ICMP    1
#define IP_PROTO_IGMP    2
#define IP_PROTO_IPENCAP 4
#define IP_PROTO_UDP     17
#define IP_PROTO_UDPLITE 136
#define IP_PROTO_TCP     6
#define IP_PROTO_DCCP	 33

#define ETHTYPE_IP        0x0800U

#define DCCP_TYPE_DATA	2
#define DCCP_TYPE_ACK	3

struct dccp_hdr {
  uint16_t src, dst;
  uint8_t data_offset;
  uint8_t ccval_cscov;
  uint16_t checksum;
  uint8_t res_type_x;
  uint8_t seq_high;
  uint16_t seq_low;
} __attribute__ ((packed));

struct dccp_ack {
  struct dccp_hdr hdr;
  uint32_t ack;
} __attribute__ ((packed));

struct pkt_dccp_headers {
    struct eth_hdr eth;
    struct ip_hdr ip;
    struct dccp_hdr dccp;
} __attribute__ ((packed));

struct pkt_dccp_ack_headers {
    struct eth_hdr eth;
    struct ip_hdr ip;
    struct dccp_ack dccp;
} __attribute__ ((packed));

#ifdef USE_TCP
struct tcp_hdr {
  uint16_t src;
  uint16_t dest;
  uint32_t seqno;
  uint32_t ackno;
  uint16_t _hdrlen_rsvd_flags;
  uint16_t wnd;
  uint16_t chksum;
  uint16_t urgp;
};

#define TCP_FIN 0x01U
#define TCP_SYN 0x02U
#define TCP_RST 0x04U
#define TCP_PSH 0x08U
#define TCP_ACK 0x10U
#define TCP_URG 0x20U
#define TCP_ECE 0x40U
#define TCP_CWR 0x80U

#define TCP_FLAGS 0x3fU

/* Length of the TCP header, excluding options. */
#define TCP_HLEN 20

#define TCPH_HDRLEN(phdr) (ntohs((phdr)->_hdrlen_rsvd_flags) >> 12)
#define TCPH_FLAGS(phdr)  (ntohs((phdr)->_hdrlen_rsvd_flags) & TCP_FLAGS)

#define TCPH_HDRLEN_SET(phdr, len) (phdr)->_hdrlen_rsvd_flags = htons(((len) << 12) | TCPH_FLAGS(phdr))
#define TCPH_FLAGS_SET(phdr, flags) (phdr)->_hdrlen_rsvd_flags = (((phdr)->_hdrlen_rsvd_flags & PP_HTONS((u16_t)(~(u16_t)(TCP_FLAGS)))) | htons(flags))
#define TCPH_HDRLEN_FLAGS_SET(phdr, len, flags) (phdr)->_hdrlen_rsvd_flags = htons(((len) << 12) | (flags))

#define TCPH_SET_FLAG(phdr, flags ) (phdr)->_hdrlen_rsvd_flags = ((phdr)->_hdrlen_rsvd_flags | htons(flags))
#define TCPH_UNSET_FLAG(phdr, flags) (phdr)->_hdrlen_rsvd_flags = htons(ntohs((phdr)->_hdrlen_rsvd_flags) | (TCPH_FLAGS(phdr) & ~(flags)) )

#define TCP_TCPLEN(seg) ((seg)->len + ((TCPH_FLAGS((seg)->tcphdr) & (TCP_FIN | TCP_SYN)) != 0))

/** This returns a TCP header option for MSS in an u32_t */
#define TCP_BUILD_MSS_OPTION(mss) htonl(0x02040000 | ((mss) & 0xFFFF))

struct pkt_tcp_headers {
    struct eth_hdr eth;
    struct ip_hdr ip;
    struct tcp_hdr tcp;
} __attribute__ ((packed));
#endif

struct connection {
  uint32_t cwnd, pipe;
  uint32_t seq;
  int32_t lastack;
  size_t acks;
} __attribute__ ((aligned (64)));

#define MAX_EXECUTOR_PER_THREAD		32
#define MAX_DPDK_LCORES			2

static struct pkt_dccp_headers packet_dccp_header;
static uint32_t arranet_myip = 0;
static struct connection connections[MAX_WORKERS];
static int task2worker[MAX_TASKS];
static struct executor *slave_executors[RTE_MAX_LCORE][MAX_EXECUTOR_PER_THREAD];
static size_t exc_cnt[RTE_MAX_LCORE];
static size_t acks_sent = 0;
static rte_spinlock_t global_lock = RTE_SPINLOCK_INITIALIZER;
static uint64_t retrans_timeout, link_rtt = LINK_RTT;
static int myworker = -1;
#endif

static const char *progname = NULL;

static void *create_shmsiszed(const char *name, size_t size)
{
    int fd;
    void *p;

    if ((fd = shm_open(name, O_CREAT | O_RDWR | O_EXCL, 0666)) == -1) {
        perror("shm_open failed");
        abort();
    }
    if (ftruncate(fd, size) != 0) {
        perror("ftruncate failed");
        abort();
    }

    if ((p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) ==
            (void *) -1)
    {
        perror("mmap failed");
        abort();
    }

    close(fd);
    return p;
}

void tuple_send(struct tuple *t, struct executor *self)
{
  assert(!"Don't call in FlexNIC module");
}

#define THREAD_AFFINITY

#ifdef USE_DPDK
static int dpdk_thread(void *arg)
{
  struct executor *myself = arg;

  printf("%s: DPDK thread RX queue %d, TX queue %d\n", progname, myself->rx_id, myself->taskid);

  for(;;) {
    uint64_t start_outq = rdtsc();

    // RAS: Pull a batch of packets from the NIC RX queue
    int stats[MAX_TASKS];
    memset(stats, 0, sizeof(stats));
    int batch_size = 0;
    struct rte_mbuf *packets[BATCH_SIZE];
    while(batch_size < BATCH_SIZE && rdtsc() - start_outq < BATCH_DELAY) {
      if(BATCH_SIZE - batch_size < 32) {
	break;
      }
      uint16_t recved = rte_eth_rx_burst(0, myself->rx_id, &packets[batch_size], BATCH_SIZE - batch_size);
      batch_size += recved;
    }

    for(int i = 0; i < batch_size; i++) {
#ifndef USE_TCP
      struct pkt_dccp_headers *p = rte_pktmbuf_mtod(packets[i], struct pkt_dccp_headers *);
#else
      struct pkt_tcp_headers *p = rte_pktmbuf_mtod(packets[i], struct pkt_tcp_headers *);
#endif

      // RAS: Mark invalid if not IP or DCCP/TCP. Drop/give to slow-path.
#ifndef USE_TCP
      if(ntohs(p->eth.type) != ETHTYPE_IP || p->ip._proto != IP_PROTO_DCCP) {
#else
      if(ntohs(p->eth.type) != ETHTYPE_IP || p->ip._proto != IP_PROTO_TCP) {
#endif
	rte_pktmbuf_free(packets[i]);
	packets[i] = NULL;
	continue;
      }

      // RAS: We don't expect this to be implemented in FlexNIC
      // Process ACKs
#ifndef USE_TCP
      if(((p->dccp.res_type_x >> 1) & 15) == DCCP_TYPE_ACK) {
	struct pkt_dccp_ack_headers *ack = (void *)p;
	int srcworker = ntohs(p->dccp.src);
	assert(srcworker < MAX_WORKERS);

	assert(ntohl(ack->dccp.ack) < (1 << 24));

	// Wraparound?
	if((int32_t)ntohl(ack->dccp.ack) < connections[srcworker].lastack &&
	   connections[srcworker].lastack - (int32_t)ntohl(ack->dccp.ack) > connections[srcworker].pipe &&
	   connections[srcworker].lastack > (1 << 23)) {
	  connections[srcworker].lastack = -((1 << 24) - connections[srcworker].lastack);
	}

	if(connections[srcworker].lastack < (int32_t)ntohl(ack->dccp.ack)) {
	  int32_t oldpipe = __sync_sub_and_fetch(&connections[srcworker].pipe,
						 (int32_t)ntohl(ack->dccp.ack) - connections[srcworker].lastack);
	  if(oldpipe < 0) {
	    connections[srcworker].pipe = 0;
	  }

	  // Reset RTO
	  retrans_timeout = rdtsc() + link_rtt * PROC_FREQ_MHZ;
	  link_rtt = LINK_RTT;
	}

	if((int32_t)ntohl(ack->dccp.ack) > connections[srcworker].lastack + 1) {
	  /* printf("%s: Congestion event for %d! ack %u, lastack + 1 = %u\n", */
	  /* 	 progname, srcworker, ntohl(ack->dccp.ack), */
	  /* 	 connections[srcworker].lastack + 1); */
	  // Congestion event! Shrink congestion window
	  uint32_t oldcwnd = connections[srcworker].cwnd, newcwnd;
	  do {
	    newcwnd = oldcwnd;
	    if(oldcwnd >= 2) {
	      newcwnd = __sync_val_compare_and_swap(&connections[srcworker].cwnd, oldcwnd, oldcwnd / 2);
	    } else {
	      break;
	    }
	  } while(oldcwnd != newcwnd);
	} else {
	  /* printf("%s: Increasing congestion window for %d\n", progname, srcworker); */
	  // Increase congestion window
	  /* __sync_fetch_and_add(&connections[srcworker].cwnd, 1); */
	  connections[srcworker].cwnd++;
	}

	connections[srcworker].lastack = MAX(connections[srcworker].lastack, (int32_t)ntohl(ack->dccp.ack));
	connections[srcworker].acks++;

	// Free packet
	rte_pktmbuf_free(packets[i]);
	packets[i] = NULL;
	continue;
      }
#else
      // ACK received
      if(TCPH_FLAGS(&p->tcp) & TCP_ACK) {
	// RAS: Put ACK to single app-level ACK queue
	// Include steering and connection information
	assert(!"NYI");
      }

      // ECN info received
      if(TCPH_FLAGS(&p->tcp) & TCP_ECE) {
	// RAS: Put ECN info into OS-level ECN queue
	// Include connection information
      }
#endif

      // RAS: Send ACK
      struct rte_mbuf *mbuf = rte_pktmbuf_alloc(l2fwd_pktmbuf_pool);
      assert(mbuf != NULL);
#ifndef USE_TCP
      struct pkt_dccp_ack_headers *header = (void *)
	rte_pktmbuf_append(mbuf, sizeof(struct pkt_dccp_ack_headers));
      memcpy(header, &packet_dccp_header, sizeof(struct pkt_dccp_headers));
      header->eth.dest = p->eth.src;
      header->dccp.hdr.src = p->dccp.dst;
      header->dccp.hdr.res_type_x = DCCP_TYPE_ACK << 1;
      uint32_t seq = (p->dccp.seq_high << 16) | ntohs(p->dccp.seq_low);
      header->dccp.ack = htonl(seq);
      header->dccp.hdr.data_offset = 4;
#else
      struct pkt_tcp_headers *header = (void *)
	rte_pktmbuf_append(mbuf, sizeof(struct pkt_tcp_headers));
      memcpy(header, &packet_tcp_header, sizeof(struct pkt_tcp_headers));
      header->eth.dest = p->eth.src;
      assert(!"NYI");
#endif
      mbuf->ol_flags = PKT_TX_IP_CKSUM | PKT_TX_IPV4;
      mbuf->l2_len = 6;
      mbuf->l3_len = 20;
      uint16_t sent = rte_eth_tx_burst(0, myself->taskid, &mbuf, 1);
      assert(sent == 1);
      acks_sent++;

      // Decapsulate packet headers
      struct tuple *t = (void *)(p + 1);
      stats[t->task]++;

      /* printf("%s: Received packet to task %d, from task %d\n", progname, t->task, t->fromtask); */
    }

#ifdef DEBUG_PERF
    uint64_t wait_outq = rdtsc();
    myself->wait_outq += wait_outq - start_outq;
#endif

    assert(stats[0] == 0);
    for(int i = 1; i < MAX_TASKS; i++) {
      if(stats[i] == 0) {
	continue;
      }

#ifdef DEBUG_PERF
      uint64_t start_inq = rdtsc();
#endif

#ifdef FLEXNIC_DEBUG
      printf("%s: tuple %d -> %d\n", progname,
	     outq->elems[outq->tail].fromtask,
	     outq->elems[outq->tail].task);
#endif

      // RAS: Find inqueue
      struct flexnic_queue *inq = task2inqueue[i];

      // RAS: Reserve inqueue space
      int head;
      bool failed;
      do {
	failed = false;
	head = inq->head;
	int myhead = head;
	for(int j = 0; j < stats[i]; j++) {
	  if(inq->elems[myhead].task != 0) {
#ifdef DEBUG_PERF
	    __sync_fetch_and_add(&inq->full, 1);
#endif
	    failed = true;
	    break;
	  } else {
	    myhead = (myhead + 1) % MAX_ELEMS;
	  }
	}

#ifdef DROP_IF_FULL
	if(failed) {
	  break;
	}
#endif
      } while(failed ||
	      !__sync_bool_compare_and_swap(&inq->head, head,
					    (inq->head + stats[i]) % MAX_ELEMS));

#ifdef DROP_IF_FULL
      if(failed) {
	continue;
      }
#endif

#ifdef DEBUG_PERF
      uint64_t wait_inq = rdtsc();
      myself->wait_inq += wait_inq - start_inq;
#endif

      int trans = 0;
      for(int j = 0; j < batch_size; j++) {
	// Skip invalid packets
	if(packets[j] == NULL) {
	  continue;
	}

	// RAS: Strip ETH/IP/DCCP headers
#ifndef USE_TCP
	struct pkt_dccp_headers *p = rte_pktmbuf_mtod(packets[j], struct pkt_dccp_headers *);
#else
	struct pkt_tcp_headers *p = rte_pktmbuf_mtod(packets[j], struct pkt_tcp_headers *);
#endif
	struct tuple *t = (void *)(p + 1);

	// RAS: Match task number in tuple
	if(t->task == i) {
	  /* printf("%s: Putting tuple into %d's queue\n", progname, t->task); */
	  // Copy tuple to inqueue
	  memcpy(&inq->elems[head], t, sizeof(struct tuple));
	  head = (head + 1) % MAX_ELEMS;

#ifdef USE_TCP
	  // RAS: Copy sequence number to core's sequence queue
	  // Core has to check that sequence numbers are increasing
	  // If not, then re-copy object appropriately
	  assert(!"NYI");
#endif

	  myself->tuples++;

	  // Mark packet done
	  rte_pktmbuf_free(packets[j]);
	  packets[j] = NULL;

	  // Bail if transferred all of the relevant tuples
	  trans++;
	  if(trans == stats[i]) {
	    break;
	  }
	}
      }

#ifdef DEBUG_PERF
      uint64_t memcpy_time = rdtsc();
      myself->memcpy_time += memcpy_time - wait_inq;
#endif
    }

#ifdef DROP_IF_FULL
    // Go over batch once more to free dropped packets
    for(int j = 0; j < batch_size; j++) {
      // Skip invalid packets
      if(packets[j] == NULL) {
	continue;
      }

      // Mark packet done
      rte_pktmbuf_free(packets[j]);
      packets[j] = NULL;
    }
#endif

#ifdef DEBUG_PERF
    myself->batch_size += batch_size;
    myself->batches++;
#endif
  }

  return 0;
}
#endif

#ifdef USE_DPDK
static int flexnic_thread(void *arg)
#else
static void *flexnic_thread(void *arg)
#endif
{
#ifndef USE_DPDK
  struct executor *myself = arg;
#else
  uintptr_t slaveid = (uintptr_t)arg;
#endif

#ifdef THREAD_AFFINITY
#if defined(BIGFISH) || defined(BIGFISH_FLEXNIC)
  // Pin to core
  printf("%s: Pinning thread to core %d\n", progname, myself->taskid);
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(myself->taskid, &set);
  int r = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &set);
  assert(r == 0);
#endif
#endif

#ifdef USE_DPDK
  printf("%s: Slave %" PRIuPTR " has %zu queues\n", progname, slaveid, exc_cnt[slaveid]);
#endif

#ifndef USE_DPDK
  for(;;) {
#else
    for(int exc_id = 0;; exc_id = (exc_id + 1) % exc_cnt[slaveid]) {
      struct executor *myself = slave_executors[slaveid][exc_id];
#endif
    struct flexnic_queue *outq = myself->outqueue;
    uint64_t start_outq = rdtsc();

    // RAS: Pull tuples from application TX queue in batches
    int stats[MAX_TASKS];
    memset(stats, 0, sizeof(stats));
    int batch_size = 0;
    int tail = outq->tail;
    while(batch_size < BATCH_SIZE && rdtsc() - start_outq < BATCH_DELAY) {
      if(outq->elems[tail].task != 0) {
	stats[outq->elems[tail].task]++;
	batch_size++;
	tail = (tail + 1) % MAX_ELEMS;
      } else {
#ifdef DEBUG_PERF
	outq->empty++;
#endif
      }
    }

#ifdef DEBUG_PERF
    uint64_t wait_outq = rdtsc();
    myself->wait_outq += wait_outq - start_outq;
#endif

    assert(stats[0] == 0);
    for(int i = 1; i < MAX_TASKS; i++) {
      if(stats[i] == 0) {
	continue;
      }

#ifdef DEBUG_PERF
      uint64_t start_inq = rdtsc();
#endif

#ifdef FLEXNIC_DEBUG
      printf("%s: tuple %d -> %d\n", progname,
	     outq->elems[outq->tail].fromtask,
	     outq->elems[outq->tail].task);
#endif

      int head = -1;
      // RAS: This is for internal delivery. Probably not implemented on FlexNIC.
      // Find inqueue
      struct flexnic_queue *inq = task2inqueue[i];
#ifdef USE_DPDK
      int worker = task2worker[i];
      if(worker == myworker) {
#endif
      // Reserve inqueue space
      bool failed;
      do {
	failed = false;
	head = inq->head;
	int myhead = head;
	for(int j = 0; j < stats[i]; j++) {
	  if(inq->elems[myhead].task != 0) {
#ifdef DEBUG_PERF
	    __sync_fetch_and_add(&inq->full, 1);
#endif
	    failed = true;
	    break;
	  } else {
	    myhead = (myhead + 1) % MAX_ELEMS;
	  }
	}

#ifdef DROP_IF_FULL
	if(failed) {
	  break;
	}
#endif
      } while(failed ||
	      !__sync_bool_compare_and_swap(&inq->head, head,
					    (inq->head + stats[i]) % MAX_ELEMS));

#ifdef DROP_IF_FULL
      if(failed) {
	continue;
      }
#endif
#ifdef USE_DPDK
      }
#endif

#ifdef DEBUG_PERF
      uint64_t wait_inq = rdtsc();
      myself->wait_inq += wait_inq - start_inq;
#endif

      tail = outq->tail;
      int trans = 0;
      for(int j = 0; j < batch_size; j++) {
	if(outq->elems[tail].task == i) {
#ifdef USE_DPDK
	  if(worker == myworker) {
#endif
	    // Copy tuple to inqueue
	    assert(head != -1);
	    memcpy(&inq->elems[head], &outq->elems[tail], sizeof(struct tuple));
	    head = (head + 1) % MAX_ELEMS;
	    myself->tuples++;
#ifdef USE_DPDK
	  } else {
	    // Handle RTO - timeout?
	    if(rdtsc() >= retrans_timeout) {
	      connections[worker].pipe = 0;
	      __sync_fetch_and_add(&link_rtt, link_rtt);
	    }

	    // RAS: Expect this to be implemented in software, not FlexNIC.
	    // Only transfer if pipe < cwnd and buffers available. Otherwise, drop
	    if(connections[worker].pipe < connections[worker].cwnd) {
	    /* if(true) { */
	      struct rte_mbuf *mbuf = rte_pktmbuf_alloc(l2fwd_pktmbuf_pool);
	      if(mbuf != NULL) {
		struct pkt_dccp_headers *header = (void *)rte_pktmbuf_append(mbuf, sizeof(struct pkt_dccp_headers));
		memcpy(header, &packet_dccp_header, sizeof(struct pkt_dccp_headers));
		void *target = rte_pktmbuf_append(mbuf, sizeof(struct tuple));
		memcpy(target, &outq->elems[tail], sizeof(struct tuple));
		header->dccp.dst = htons(worker);
		header->eth.dest = workers[worker].mac;
		header->ip.src.addr = myself->workerid;

		rte_spinlock_lock(&global_lock);
		retrans_timeout = rdtsc() + link_rtt * PROC_FREQ_MHZ;
		link_rtt = LINK_RTT;
		uint32_t seq = __sync_fetch_and_add(&connections[worker].seq, 1);
		header->dccp.seq_high = seq >> 16;
		header->dccp.seq_low = htons(seq & 0xffff);
		/* printf("seq = %x, seq_high = %x, seq_low = %x\n", seq, header->dccp.seq_high, header->dccp.seq_low); */
		/* printf("%s: Sending to worker %d, task %d\n", progname, worker, i); */
		mbuf->ol_flags = PKT_TX_IP_CKSUM | PKT_TX_IPV4;
		mbuf->l2_len = 6;
		mbuf->l3_len = 20;
		/* while(rte_eth_tx_burst(0, myself->taskid, &mbuf, 1) < 1); */
		while(rte_eth_tx_burst(0, 0, &mbuf, 1) < 1);
		rte_spinlock_unlock(&global_lock);

		__sync_fetch_and_add(&connections[worker].pipe, 1);
		myself->tuples++;
	      }
	    }
	  }
#endif

	  // Bail if transferred all of the relevant tuples
	  trans++;
	  if(trans == stats[i]) {
	    break;
	  }
	}

	tail = (tail + 1) % MAX_ELEMS;
      }

#ifdef DEBUG_PERF
      uint64_t memcpy_time = rdtsc();
      myself->memcpy_time += memcpy_time - wait_inq;
#endif
    }

#ifdef DEBUG_PERF
    uint64_t batchdone_start = rdtsc();
#endif

    // Mark batch done in outqueue
    tail = outq->tail;
    outq->tail = (outq->tail + batch_size) % MAX_ELEMS;
    for(int j = 0; j < batch_size; j++) {
      outq->elems[tail].task = 0;
      tail = (tail + 1) % MAX_ELEMS;
    }

#ifdef DEBUG_PERF
    uint64_t batchdone_time = rdtsc();
    myself->batchdone_time += batchdone_time - batchdone_start;

    myself->batch_size += batch_size;
    myself->batches++;
#endif
  }

#ifdef USE_DPDK
  return 0;
#else
  return NULL;
#endif
}

int main(int argc, char *argv[])
{
  int r;

  umask(0);

  // RAS: We first create as many packet queues as there are worker threads in the application.
  // Create all flexnic queues
  for(int j = 0; j < MAX_WORKERS; j++) {
    for(int i = 0; i < MAX_EXECUTORS; i++) {
      char filename[128];

      r = snprintf(filename, 128, "worker_%d_inq_%d", j, i);
      assert(r >= 0);
      workers[j].executors[i].inqueue = create_shmsiszed(filename, sizeof(struct flexnic_queue));

      r = snprintf(filename, 128, "worker_%d_outq_%d", j, i);
      assert(r >= 0);
      workers[j].executors[i].outqueue = create_shmsiszed(filename, sizeof(struct flexnic_queue));
    }
  }

  // Cache mapping from worker to inqueue
  for(int j = 0; j < MAX_WORKERS && workers[j].hostname != NULL; j++) {
    for(int i = 0; i < MAX_EXECUTORS && workers[j].executors[i].execute != NULL; i++) {
      int taskid = workers[j].executors[i].taskid;
      assert(task2inqueue[taskid] == NULL);
      task2inqueue[taskid] = workers[j].executors[i].inqueue;
    }
  }

#ifdef USE_DPDK
  int ret;
  uint8_t nb_ports;
  uint8_t portid;
  unsigned lcore_id;
  int slaves[RTE_MAX_LCORE];

  int dpdk_threads = MAX_DPDK_LCORES;
  int sender_threads = 0;
  for(int j = 0; j < MAX_WORKERS && workers[j].hostname != NULL; j++) {
    for(int i = 0; i < MAX_EXECUTORS && workers[j].executors[i].execute != NULL; i++) {
      sender_threads++;
    }
  }

  for(int i = 0; i < MAX_WORKERS; i++) {
    connections[i].cwnd = 4;
  }

  for(int i = 0; i < MAX_WORKERS && workers[i].hostname != NULL; i++) {
    for(int j = 0; j < MAX_EXECUTORS && workers[i].executors[j].execute != NULL; j++) {
      task2worker[workers[i].executors[j].taskid] = i;
    }
  }

  /* init EAL */
  ret = rte_eal_init(argc, argv);
  if (ret < 0)
    rte_exit(EXIT_FAILURE, "Invalid EAL arguments\n");
  argc -= ret;
  argv += ret;

  /* create the mbuf pool */
  l2fwd_pktmbuf_pool =
    rte_mempool_create("mbuf_pool", NB_MBUF,
		       MBUF_SIZE, 32,
		       sizeof(struct rte_pktmbuf_pool_private),
		       rte_pktmbuf_pool_init, NULL,
		       rte_pktmbuf_init, NULL,
		       rte_socket_id(), 0);
  if (l2fwd_pktmbuf_pool == NULL) {
    printf("%s\n", rte_strerror(rte_errno));
    rte_exit(EXIT_FAILURE, "Cannot init mbuf pool\n");
  }

  nb_ports = rte_eth_dev_count();
  if (nb_ports == 0)
    rte_exit(EXIT_FAILURE, "No Ethernet ports - bye\n");

  if (nb_ports > RTE_MAX_ETHPORTS)
    nb_ports = RTE_MAX_ETHPORTS;

  /* Initialise each port */
  for (portid = 0; portid < nb_ports; portid++) {
    /* init port */
    printf("Initializing port %u...\n", (unsigned) portid);
    fflush(stdout);
    ret = rte_eth_dev_configure(portid, dpdk_threads, sender_threads + dpdk_threads, &port_conf);
    if (ret < 0)
      rte_exit(EXIT_FAILURE, "Cannot configure device: err=%d, port=%u\n",
	       ret, (unsigned) portid);

    rte_eth_macaddr_get(portid,&l2fwd_ports_eth_addr[portid]);

    /* init one RX queue */
    for(int i = 0; i < dpdk_threads; i++) {
      fflush(stdout);
      ret = rte_eth_rx_queue_setup(portid, i, nb_rxd,
				   rte_eth_dev_socket_id(portid),
				   NULL,
				   l2fwd_pktmbuf_pool);
      if (ret < 0)
	rte_exit(EXIT_FAILURE, "rte_eth_rx_queue_setup:err=%d, port=%u\n",
		 ret, (unsigned) portid);
    }

    printf("Initializing %d TX queues\n", sender_threads + dpdk_threads);

    for(int i = 0; i < sender_threads + dpdk_threads; i++) {
      fflush(stdout);
      ret = rte_eth_tx_queue_setup(portid, i, nb_txd,
				   rte_eth_dev_socket_id(portid),
				   NULL);
      if (ret < 0)
	rte_exit(EXIT_FAILURE, "rte_eth_tx_queue_setup:err=%d, port=%u\n",
		 ret, (unsigned) portid);
    }

    /* Start device */
    ret = rte_eth_dev_start(portid);
    if (ret < 0)
      rte_exit(EXIT_FAILURE, "rte_eth_dev_start:err=%d, port=%u\n",
	       ret, (unsigned) portid);

    printf("done: \n");

    printf("Port %u, MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n\n",
	   (unsigned) portid,
	   l2fwd_ports_eth_addr[portid].addr_bytes[0],
	   l2fwd_ports_eth_addr[portid].addr_bytes[1],
	   l2fwd_ports_eth_addr[portid].addr_bytes[2],
	   l2fwd_ports_eth_addr[portid].addr_bytes[3],
	   l2fwd_ports_eth_addr[portid].addr_bytes[4],
	   l2fwd_ports_eth_addr[portid].addr_bytes[5]);
  }

  /***** Initialize IP/Ethernet packet header template *****/
  {
    struct pkt_dccp_headers *p = &packet_dccp_header;

    // Initialize Ethernet header
    memcpy(&p->eth.src, l2fwd_ports_eth_addr[0].addr_bytes, ETHARP_HWADDR_LEN);
    p->eth.type = htons(ETHTYPE_IP);

    // Initialize IP header
    p->ip._v_hl = 69;
    p->ip._tos = 0;
    p->ip._id = htons(3);
    p->ip._offset = 0;
    p->ip._ttl = 0xff;
    p->ip._proto = IP_PROTO_DCCP;
    p->ip._chksum = 0;
    p->ip.src.addr = arranet_myip;
    p->ip._len = htons(sizeof(struct tuple) + sizeof(struct dccp_hdr) + IP_HLEN);

    p->dccp.data_offset = 3;
    p->dccp.res_type_x = DCCP_TYPE_DATA << 1;
    p->dccp.ccval_cscov = 1;
  }

  /* launch per-lcore init on every lcore */
  int dpdk_lcores[MAX_DPDK_LCORES];
  int dpdk_cnt = 0;
  RTE_LCORE_FOREACH_SLAVE(lcore_id) {
    dpdk_lcores[dpdk_cnt++] = lcore_id;
    if(dpdk_cnt >= MAX_DPDK_LCORES) {
      break;
    }
  }

  for(int i = 0; i < RTE_MAX_LCORE;) {
    RTE_LCORE_FOREACH_SLAVE(lcore_id) {
      if(i >= RTE_MAX_LCORE) {
	break;
      }
      bool found = false;
      for(int j = 0; j < MAX_DPDK_LCORES; j++) {
	if(lcore_id == dpdk_lcores[j]) {
	  found = true;
	  break;
	}
      }
      if(found) {
	continue;
      }
      slaves[i] = lcore_id;
      i++;
    }
  }
#endif

  assert(argc > 1);
  progname = argv[1];

  // Start/assign FlexNIC threads
  int cnt = 0;
  for(int j = 0; j < MAX_WORKERS && workers[j].hostname != NULL; j++) {
#ifdef USE_DPDK
    if(!strcmp(progname, workers[j].hostname)) {
      assert(myworker == -1);
      myworker = j;
    }
#endif
    for(int i = 0; i < MAX_EXECUTORS && workers[j].executors[i].execute != NULL; i++) {
#ifdef USE_DPDK
      assert(cnt < RTE_MAX_LCORE);
      // XXX: Hack to allow enough cores
#ifndef NORMAL_QUEUE
      if((!strcmp(progname, workers[j].hostname) && workers[j].executors[i].taskid < 20) ||
      	 !strcmp(progname, "128.208.6.106") || !strcmp(progname, "192.168.26.22")) {
#else
      if((!strcmp(progname, workers[j].hostname) && workers[j].executors[i].taskid <= 22) ||
	 !strcmp(progname, "128.208.6.106") || !strcmp(progname, "192.168.26.22")) {
#endif
	slave_executors[slaves[cnt]][exc_cnt[slaves[cnt]]++] = &workers[j].executors[i];
	printf("%s: Assigning worker %d executor %d task %d to slave %d, count %zu\n",
	       progname, j, i, workers[j].executors[i].taskid, slaves[cnt], exc_cnt[slaves[cnt]]);
	workers[j].executors[i].taskid = cnt;
	workers[j].executors[i].workerid = j;
      }
#else
#ifdef THREAD_AFFINITY
      workers[j].executors[i].taskid = 31 - cnt;
#endif
      pthread_t thread;
      int r = pthread_create(&thread, NULL, flexnic_thread, &workers[j].executors[i]);
      assert(r == 0);
#endif

      cnt++;
    }
  }

#ifdef USE_DPDK
  RTE_LCORE_FOREACH_SLAVE(lcore_id) {
    if(exc_cnt[lcore_id] > 0) {
      rte_eal_remote_launch(flexnic_thread, (void *)(uintptr_t)lcore_id, lcore_id);
    }
  }

  // Start DPDK threads
  static struct executor dpdk_executor[MAX_DPDK_LCORES];

  for(int i = 0; i < MAX_DPDK_LCORES; i++) {
    printf("%s: Slave %d is DPDK thread %d\n", progname, dpdk_lcores[i], i);
    dpdk_executor[i].taskid = cnt++;
    dpdk_executor[i].rx_id = i;
    rte_eal_remote_launch(dpdk_thread, &dpdk_executor[i], dpdk_lcores[i]);
  }
#endif

  // Print periodic statistics
  size_t lasttuples = 0;
  for(;;) {
    sleep(1);

#ifdef DEBUG_PERF
    printf("%s: ", progname);
#endif
    size_t sum = 0;
    for(int j = 0; j < MAX_WORKERS && workers[j].hostname != NULL; j++) {
      for(int i = 0; i < MAX_EXECUTORS && workers[j].executors[i].execute != NULL; i++) {
	sum += workers[j].executors[i].tuples;
	/* printf("%d,%d:%zu ", j, i, */
	/*        workers[j].executors[i].tuples - workers[j].executors[i].lasttuples); */
	workers[j].executors[i].lasttuples = workers[j].executors[i].tuples;

#ifdef DEBUG_PERF
	if(workers[j].executors[i].tuples > 0) {
	  printf("%d,%d: T %zu BS %zu (O %zu, I %zu, M %zu, D %zu) ", j, i,
		 workers[j].executors[i].tuples,
		 workers[j].executors[i].batch_size / workers[j].executors[i].batches,
		 workers[j].executors[i].wait_outq / workers[j].executors[i].tuples,
		 workers[j].executors[i].wait_inq / workers[j].executors[i].tuples,
		 workers[j].executors[i].memcpy_time / workers[j].executors[i].tuples,
		 workers[j].executors[i].batchdone_time / workers[j].executors[i].tuples
		 );
	}
#endif
      }
    }
#if defined(DEBUG_PERF) && defined(USE_DPDK)
    size_t recv_sum = 0;
    for(int i = 0; i < MAX_DPDK_LCORES; i++) {
	recv_sum += dpdk_executor[i].tuples;
	dpdk_executor[i].lasttuples = dpdk_executor[i].tuples;

	if(dpdk_executor[i].tuples > 0) {
	  printf("D%d: T %zu BS %zu (O %zu, I %zu, M %zu) ", i,
	     dpdk_executor[i].tuples,
	     dpdk_executor[i].batch_size / dpdk_executor[i].batches,
	     dpdk_executor[i].wait_outq / dpdk_executor[i].tuples,
	     dpdk_executor[i].wait_inq / dpdk_executor[i].tuples,
	     dpdk_executor[i].memcpy_time / dpdk_executor[i].tuples
	     );
	}
    }
#endif
#ifdef DEBUG_PERF
    printf("\n");
#endif

    size_t tuples = sum - lasttuples;
#ifdef USE_DPDK
    printf("%s: Tuples/s: %zu, Gbits/s: %.2f, ", progname,
	   tuples, (tuples * (sizeof(struct tuple) + sizeof(struct pkt_dccp_headers)) * 8) / 1000000000.0);

#	ifdef DEBUG_PERF
    for(int i = 0; i < MAX_WORKERS; i++) {
      printf("pipe,cwnd,acks,lastack[%d] = %u, %u, %zu, %d, ", i,
	     connections[i].pipe, connections[i].cwnd, connections[i].acks, connections[i].lastack);
    }
    printf("acks sent %zu, rtt %" PRIu64 "\n", acks_sent, link_rtt);
#else
    printf("\n");
#	endif

#else
    printf("%s: Tuples/s: %zu, Gbits/s: %.2f\n", progname,
	   tuples, (tuples * sizeof(struct tuple) * 8) / 1000000000.0);
#endif

    lasttuples = sum;
  }

  return 0;
}
