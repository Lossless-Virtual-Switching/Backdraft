#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <rte_mbuf.h>
#include <rte_cycles.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>

#include "vport.h"
#include "app.h"
#include "zipf.h"
#include "set.h"
#include "llring.h"

#define NUM_MBUFS 131072
#define TX_RING_SIZE 512
#define MBUF_CACHE_SIZE 250

#define CNT_FLOWS 9000
#define DPFQ 1

// ===============================================
typedef struct {
  int flow_id;
  int count_pkt;
} flow_pkt_t;

static flow_pkt_t *new_flow_pkt(int fid, int pkts)
{
  flow_pkt_t *new = malloc(sizeof(flow_pkt_t));
  new->flow_id = fid;
  new->count_pkt = pkts;
  return new;
}
// ===============================================

static struct rte_mempool *tx_pool;

static int flow_id_to_qid(int flow_id, int count_queues, int count_flows)
{
  int qid = ((count_queues - 1) * flow_id) / count_flows;
  return qid;
}

static void worker(struct vport *port)
{
  uint32_t exp_duration = 30; // seconds
  uint64_t exp_end_cycle;
  uint64_t current_cycle = 0;

  int burst = 32;
  int sent;
  int tx_num;
  int qid;
  int count_queues;
  struct rte_mbuf *tx_buf[burst];
  struct rte_mbuf *ctrl_buf;
  struct rte_mbuf *buf;

  struct rte_ether_addr my_eth;
  struct rte_ether_addr his_eth;

  struct rte_ether_hdr *eth_hdr;
  struct rte_ipv4_hdr *ipv4_hdr;
  struct rte_udp_hdr *udp_hdr;

  uint32_t payload_length = 0;
  uint32_t src_ip;
  uint32_t dst_ip;
  uint16_t src_port;
  uint16_t dst_port;

  struct zipfgen *q_index_zipf;

  char doorbell_q = 1; // enable using doorbell queue

  const int count_flows = CNT_FLOWS;
  struct zipfgen *flow_id_zipf;
  int current_flow = 0;
  int num[count_flows];

  count_queues = port->bar->num_out_q;

  int flow_queue_map[count_flows + 1];
  hash_set_t *queue_flows[count_queues]; // what flow ids are on which queue
  uint64_t collisions[count_queues];
  list_t *queue_view[count_queues];
  int prev_queue_size[count_queues];
  size_t current_q_size;

  int i;
  int rc;
  int64_t active_flows = 0;

  if (doorbell_q)
    assert(count_queues > 2);

  qid = doorbell_q ? 1 : 0;

  if (doorbell_q) {
    q_index_zipf = new_zipfgen(count_queues - 1, 2);
  } else {
    q_index_zipf = new_zipfgen(count_queues, 2);
  }

  flow_id_zipf = new_zipfgen(count_flows, 1);

  my_eth = (struct rte_ether_addr) {{0x1c, 0x34, 0xda, 0x41, 0xc6, 0xfc}};
  his_eth = (struct rte_ether_addr) {{0x1c, 0x34, 0xda, 0x41, 0xc6, 0xfc}};
  src_ip = 0x0a0a0103;
  dst_ip = 0x0a0a0104;
  src_port = 1234;
  dst_port = 1235;

  for (i = 0; i < count_queues; i++) {
    queue_flows[i] = new_hash_set(1 << 10);
    collisions[i] = 0;
    assert(queue_flows[i] != NULL);
    queue_view[i] = new_list();
    prev_queue_size[i] = 0;
  }

  for (i = 0; i < count_flows; i++) {
    num[i] = i;
    flow_queue_map[i] = i % count_queues;
    if (doorbell_q && flow_queue_map[i] == 0)
      flow_queue_map[i] = 1;
  }
  flow_queue_map[count_flows] = 1;

  // for (int k = 0; k < 1000; k++) {
  exp_end_cycle = rte_get_tsc_cycles() + exp_duration * rte_get_tsc_hz();
  while (current_cycle < exp_end_cycle) {
    current_cycle = rte_get_tsc_cycles();
    if (rte_pktmbuf_alloc_bulk(tx_pool, tx_buf, burst)) {
      /* allocating failed */
      // printf("allocating packest failed\n");
      // k--; // the burst not sent
      continue;
    }

    current_flow = flow_id_zipf->gen(flow_id_zipf);
    // current_flow = (current_flow + 1) % count_flows;

    if (DPFQ) {
      qid = flow_queue_map[current_flow];
      if (queue_flows[qid]->size > 1) {
        // there are multiple flows on this queue
        int min = INT_MAX;
        int selected = -1;
        for (i = 1; i < count_queues; i++) {
          if (prev_queue_size[i] < min) {
            min = prev_queue_size[i];
            selected = i;
            if (min == 0)
              break;
          }
        }
        qid = selected;
        flow_queue_map[current_flow] = qid;
      }
      // if (qid < 1|| qid >= count_queues) {
      //   printf("something is wrong %d\n", qid);
      //   fflush(stdout);
      // }
    } else {
      qid = flow_id_to_qid(current_flow, count_queues, count_flows);
      if (qid == 0 && doorbell_q)
        qid = 1;
    }

    // check if there is packet of some flow in the buffer
    size_t tmp_q_size;
    tmp_q_size = prev_queue_size[qid];
    // checking the queue size
    // this line is a low level structure exposed for testing
    // if really needing this an api should be prepared.
    // because client is not the owner tx queue will be receiveres
    // rx queue hence inc_qs is used.
    current_q_size = llring_count(port->bar->inc_qs[qid]);
    if (current_q_size < tmp_q_size) {
      list_node_t *tmp;
      int delta = tmp_q_size - current_q_size;
      int pkts;
      list_node_t *node = queue_view[qid]->head;
      flow_pkt_t *x;
      int found;
      int looking;
      while (current_q_size < tmp_q_size) {
        x = node->value;
        pkts = x->count_pkt;
        if(pkts > delta) {
          tmp_q_size -= delta;
          x->count_pkt -= delta;
        } else {
          delta -= pkts;
          tmp_q_size -= pkts;
          looking = x->flow_id;
          found = 0;
          for (tmp = node; tmp != queue_view[qid]->tail; tmp = tmp->next) {
            x = tmp->value;
            if (x->flow_id == looking) {
              found = 1;
              break;
            }
          }
          tmp = node;
          node = node->next;
          if (!found) {
            // remove flow id from set of flows for this qid
            remove_hash_set(queue_flows[qid], &num[looking],
                            sizeof(num[looking]));
            active_flows--;
            assert(active_flows >= 0U);
            // printf("remove from hash set %d\n", looking);
          }
          remove_list(queue_view[qid], tmp->value);
        }
      }
    }
    prev_queue_size[qid] = current_q_size;
    // =================================================

    rc = add_hash_set(queue_flows[qid], &num[current_flow],
                      sizeof(num[current_flow]));
    if (rc == 0)
      active_flows++;
    // rc = 0 means that a new flow is added to the set
    if (rc == 0 && queue_flows[qid]->size > 1) {
      collisions[qid]++;
    }

    for (i = 0; i < burst; i++) {
      buf = tx_buf[i];

      // ether header
      eth_hdr = rte_pktmbuf_mtod(buf, struct rte_ether_hdr *);

      rte_ether_addr_copy(&my_eth, &eth_hdr->s_addr);
      rte_ether_addr_copy(&his_eth, &eth_hdr->d_addr);
      eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

      // ipv4 header
      ipv4_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);
      ipv4_hdr->version_ihl = 0x45;
      ipv4_hdr->type_of_service = 0;
      ipv4_hdr->total_length =
          rte_cpu_to_be_16(sizeof(struct rte_ipv4_hdr) +
                           sizeof(struct rte_udp_hdr) + payload_length);
      ipv4_hdr->packet_id = 0;
      ipv4_hdr->fragment_offset = 0;
      ipv4_hdr->time_to_live = 64;
      ipv4_hdr->next_proto_id = IPPROTO_UDP;
      ipv4_hdr->hdr_checksum = 0;
      ipv4_hdr->src_addr = rte_cpu_to_be_32(src_ip);
      ipv4_hdr->dst_addr = rte_cpu_to_be_32(dst_ip);

      // upd header
      udp_hdr = (struct rte_udp_hdr *)(ipv4_hdr + 1);
      udp_hdr->src_port = rte_cpu_to_be_16(src_port);
      udp_hdr->dst_port = rte_cpu_to_be_16(dst_port);
      udp_hdr->dgram_len =
          rte_cpu_to_be_16(sizeof(struct rte_udp_hdr) + payload_length);
      udp_hdr->dgram_cksum = 0;

      /* payload */
      buf->l2_len = RTE_ETHER_HDR_LEN;
      buf->l3_len = sizeof(struct rte_ipv4_hdr);
      buf->ol_flags = PKT_TX_IP_CKSUM | PKT_TX_IPV4;
    }
    tx_num = burst;

send_again:
    sent = send_packets_vport(port, qid, (void **)tx_buf, tx_num);
    // sent = send_packets_vport(port, 0, (void **)tx_buf, tx_num);

    // ===================================
    // Update queue view
    flow_pkt_t *item = new_flow_pkt(current_flow, sent);
    rc = append_list(queue_view[qid], item);
    if (rc != 0)
      fprintf(stderr, "failed to append to list\n");
    prev_queue_size[qid] += sent;
    //=====================================

    // send ctrl packet
    if (doorbell_q) {
      // allocate control packet
      while ((ctrl_buf = rte_pktmbuf_alloc(tx_pool)) == NULL) {
        // failed to allocate ctrl packet
        continue;
      }
      // write queue id in ctrl packet
      uint16_t *ptr = rte_pktmbuf_mtod(ctrl_buf, uint16_t *);
      *ptr = qid;
      // printf("qid: %d\n", qid);

      while (send_packets_vport(port, 0, (void **)(&ctrl_buf), 1) == 0) {
        // failed to send ctrl packet
      }
    }

    if (sent != tx_num) {
      // retry
      tx_num -= sent;
      for (int j = 0; j < tx_num; j++)
        tx_buf[j] = tx_buf[j + sent];
      goto send_again;
    }

    // use next queue for next batch
    // qid = (qid + 1) % count_queues;
    // if (doorbell_q) {
    //   // from 1 to count_queue -1;
    //   qid = q_index_zipf->gen(q_index_zipf);
    // } else {
    //   // from 0  to count_queue -1;
    //   qid = q_index_zipf->gen(q_index_zipf) - 1;
    // }

    // update stats on screen
    uint64_t current_ts = rte_get_tsc_cycles();
    static uint64_t  last_update_ts = 0;
    if (current_ts > last_update_ts + rte_get_tsc_hz()) {
      printf("active flow: %ld\n", active_flows);
      last_update_ts = current_ts;
    }
  }

  printf("\n\nClient done\n");
  printf("sent pkts\n");
  printf("Flow Collision Stats:\n");
  long int total = 0;
  for (i = 0; i < count_queues; i++) {
    // printf("qid: %d    num collision: %ld\n", i, collisions[i]);
    total += collisions[i];
  }
  printf("total collisions: %ld\n", total);



  free_zipfgen(q_index_zipf);
}

/* Client main function
 * */
int client_main(int argc, char *argv[])
{
  char port_path[PORT_DIR_LEN];
  FILE *vport_fp;
  size_t bar_address;
  struct vport *port;
  char port_name[PORT_NAME_LEN];

  if (argc < 2) {
    printf("client: need one parameter, port name\n");
    return -1;
  }
  printf("port name: %s\n", argv[1]);
  strncpy(port_name, argv[1], PORT_NAME_LEN);

  // create tx mem pool
	tx_pool = rte_pktmbuf_pool_create("MBUF_TX_POOL", NUM_MBUFS, MBUF_CACHE_SIZE,
																		0, RTE_MBUF_DEFAULT_BUF_SIZE,
                                    rte_socket_id());
  if (tx_pool == NULL)
    rte_exit(EXIT_FAILURE, "Error can not create tx mbuf pool\n");

  /// attach to vport
  snprintf(port_path, PORT_DIR_LEN, "%s/%s/%s",
           TMP_DIR, VPORT_DIR_PREFIX, port_name);
  printf("port file: %s\n", port_path);
  vport_fp = fopen(port_path, "r");
  if(!fread(&bar_address, 8, 1, vport_fp)) {
    printf("file is empty (or another error)\n");
    rte_exit(EXIT_FAILURE, "Error port file has an issue\n");
  }
  fclose(vport_fp);
  port = from_vbar_addr(bar_address);

  printf("Client connected to vport with %d queues\n", port->bar->num_out_q);
  printf("#queue: %d, size queue: %d, #flow: %d, dpfq: %d\n",
      port->bar->num_out_q, SLOTS_PER_LLRING, CNT_FLOWS, DPFQ);

  worker(port);

  free_vport(port);

  return 0;
}
