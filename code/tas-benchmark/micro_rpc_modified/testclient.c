#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <netdb.h>
#include <errno.h>
#include <assert.h>
#include <locale.h>
#include <inttypes.h>

#include "../common/socket_shim.h"

#ifdef USE_MTCP
# include <mtcp_api.h>
# include <mtcp_epoll.h>
#else
# include <sys/epoll.h>
#endif

#define MIN(a,b) ((b) < (a) ? (b) : (a))

#define CONN_DEBUG(c, co, x...) do { } while (0)
/*#define CONN_DEBUG(c, co, x...) \
    do { printf("%d.%d: ", (int) c->id, co->fd); \
         printf(x); } while (0)*/

//#define PRINT_STATS
//#define PRINT_TSS
#ifdef PRINT_STATS
#   define STATS_ADD(c, f, n) c->f += n
#else
#   define STATS_ADD(c, f, n) do { } while (0)
#endif

#define HIST_START_US 0
#define HIST_BUCKET_US 1
#define HIST_BUCKETS (256 * 1024)


enum conn_state {
    CONN_CLOSED = 0,
    CONN_CONNECTING = 1,
    CONN_OPEN = 2,
    CONN_CLOSING = 3,
};

static uint32_t max_pending = 64;
static uint32_t max_conn_pending = 16;
static uint32_t message_size = 64;
static uint32_t num_conns = 8;
static uint32_t num_msgs = 0;
static uint32_t openall_delay = 0;
static struct sockaddr_in *addrs;
static size_t addrs_num;
static volatile int start_running = 0;
static FILE *file = NULL;

struct connection {
    enum conn_state state;
    int fd;
    int ep_wr;
    uint32_t pending;
    uint32_t tx_remain;
    uint32_t rx_remain;
    uint32_t tx_cnt;
    void *rx_buf;
    void *tx_buf;
    struct sockaddr_in *addr;
    // == per flow stats ===
    uint32_t addr_index;
    // =====================

    struct connection *next_closed;
#ifdef PRINT_STATS
    uint64_t cnt;
#endif

    // == flow duration ==
    uint64_t begin_ts; // connection establishment timestamp
    uint32_t duration; // max flow duration
    // ===================
    // == message per sec limit ==
    uint64_t last_budget_update;
    uint64_t budget;
    uint64_t packet_value; // time to packet conversion rate (how many ns is a packet)
    // ===================
};

struct core {
    struct connection *conns;
    uint64_t messages;
#ifdef PRINT_STATS
    uint64_t rx_calls;
    uint64_t rx_short;
    uint64_t rx_fail;
    uint64_t rx_bytes;
    uint64_t tx_calls;
    uint64_t tx_short;
    uint64_t tx_fail;
    uint64_t tx_bytes;
    uint64_t rx_cycles;
    uint64_t tx_cycles;
#endif
    uint32_t *hist;
    // == per flow stats ===
    uint32_t **hist2;
    uint64_t *messages2;
    // =====================
    int ep;
    ssctx_t sc;
#ifdef USE_MTCP
    mctx_t mc;
#endif
    uint64_t id;
    struct connection *closed_conns;
    uint32_t conn_pending;
    uint32_t conn_open;
    pthread_t pthread;
} __attribute__((aligned(64)));

static void open_all(struct core *c);

uint64_t max_flow_duration = 0;
int64_t max_message_ps = -1; // maximum message sent per second

void handle_sigint(int sig)
{
    fclose(file);
    printf("Caught signal %d\n", sig);
}

static inline uint64_t get_nanos(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000 * 1000 * 1000 + ts.tv_nsec;
}

static inline uint64_t get_nanos_stats(void)
{
#ifdef PRINT_TSS
    return get_nanos();
#else
    return 0;
#endif
}

static inline void record_latency(struct core *c, uint64_t nanos)
{
    size_t bucket = ((nanos / 1000) - HIST_START_US) / HIST_BUCKET_US;
    if (bucket >= HIST_BUCKETS) {
        bucket = HIST_BUCKETS - 1;
    }
    __sync_fetch_and_add(&c->hist[bucket], 1);
}


// used for per flow stats
static inline void record_latency2(struct core *c, struct connection *co,
        uint64_t nanos)
{
    uint32_t addr_index = co->addr_index;
    size_t bucket = ((nanos / 1000) - HIST_START_US) / HIST_BUCKET_US;
    if (bucket >= HIST_BUCKETS) {
        bucket = HIST_BUCKETS - 1;
    }
    __sync_fetch_and_add(&c->hist2[addr_index][bucket], 1);
}

#ifdef PRINT_STATS
static inline uint64_t read_cnt(uint64_t *p)
{
  uint64_t v = *p;
  __sync_fetch_and_sub(p, v);
  return v;
}
#endif

static inline void conn_connect(struct core *c, struct connection *co)
{
    uint64_t now;
    int fd, cn, ret;
    ssctx_t sc;
    ss_epev_t ev;

    cn = c->id;
    sc = c->sc;
    CONN_DEBUG(c, co, "Opening new connection\n");

    /* create socket */
    if ((fd = ss_socket(sc, AF_INET, SOCK_STREAM, IPPROTO_TCP))
        < 0)
    {
        perror("creating socket failed");
        fprintf(stderr, "[%d] socket failed\n", cn);
        abort();
    }

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
    ret = ss_connect(sc, fd, (struct sockaddr *) co->addr, sizeof(*co->addr));
    if (ret == 0) {
        /* success */
        CONN_DEBUG(c, co, "Connection succeeded\n");
        co->state = CONN_OPEN;
    } else if (ret < 0 && errno == EINPROGRESS) {
        /* still going on */
        CONN_DEBUG(c, co, "Connection pending: %d\n", fd);
        co->state = CONN_CONNECTING;
    } else {
        /* opening connection failed */
        fprintf(stderr, "[%d] connect failed: %d %d %d (%s)\n", cn, ret, errno, EADDRNOTAVAIL, strerror(errno));
        abort();
    }

    co->fd = fd;
    co->ep_wr = 1;
    co->pending = 0;
    co->tx_cnt = 0;
    co->rx_remain = message_size;
    co->tx_remain = message_size;
#ifdef PRINT_STATS
    co->cnt = 0;
#endif
    now = get_nanos();
    // == flow duration
    co->begin_ts = now;
    co->duration = max_flow_duration * 1000000L; // ns
    // message per sec limit
    co->last_budget_update = now;
    co->budget = max_message_ps;
    co->packet_value = 1000000000L / max_message_ps;
}

static void prepare_core(struct core *c)
{
    int cn = c->id;
    uint32_t i;
    size_t next_addr;
    uint8_t *buf;
    ssctx_t sc;
#ifdef USE_MTCP
    int ret;
#endif

    /* Affinitize threads */
#ifdef USE_MTCP
    if ((ret = mtcp_core_affinitize(cn)) != 0) {
        fprintf(stderr, "[%d] mtcp_core_affinitize failed: %d\n", cn, ret);
        abort();
    }

    if ((sc = mtcp_create_context(cn)) == NULL) {
        fprintf(stderr, "[%d] mtcp_create_context failed\n", cn);
        abort();
    }

    abort();
#if 0
    if ((ret = mtcp_init_rss(sc, INADDR_ANY, 1, addr.sin_addr.s_addr,
                    addr.sin_port)) != 0)
    {
        fprintf(stderr, "[%d] mtcp_init_rss failed\n", cn);
        abort();
    }
#endif
#else
    sc = NULL;
#endif
    c->sc = sc;

    /* create epoll */
    if ((c->ep = ss_epoll_create(sc, 4 * num_conns)) < 0) {
        fprintf(stderr, "[%d] epoll_create failed\n", c->ep);
        abort();
    }

    /* Allocate histogram */
    if ((c->hist = calloc(HIST_BUCKETS, sizeof(*c->hist))) == NULL) {
        fprintf(stderr, "[%d] allocating histogram failed\n", cn);
        abort();
    }
    // per flow ====
    // Allocate hist2
    if ((c->hist2 = calloc(addrs_num, sizeof(*c->hist2))) == NULL) {
        fprintf(stderr, "[%d] allocating histogram2-part1 failed\n", cn);
        abort();
    }
    for (i = 0; i < addrs_num; i++) {
        if ((c->hist2[i] = calloc(HIST_BUCKETS, sizeof(*c->hist2[i]))) == NULL) {
            fprintf(stderr, "[%d] allocating histogram2-part2 failed\n", cn);
            abort();
        }
    }
    // Allocate messages2
    if ((c->messages2 = calloc(addrs_num, sizeof(*c->messages2))) == NULL) {
        fprintf(stderr, "[%d] allocating messages2 failed\n", cn);
        abort();
    }
    // =======================

    /* Allocate connection structs */
    if ((c->conns = calloc(num_conns, sizeof(*c->conns))) == NULL) {
        fprintf(stderr, "[%d] allocating connection structs failed\n", cn);
        abort();
    }

    /* Initiate connections */
    c->closed_conns = NULL;
    next_addr = (num_conns * c->id) % addrs_num;
    for (i = 0; i < num_conns; i++) {
        if ((buf = malloc(message_size * 2)) == NULL) {
            fprintf(stderr, "[%d] allocating conn buffer failed\n", cn);
        }
        c->conns[i].rx_buf = buf;
        c->conns[i].tx_buf = buf + message_size;
        c->conns[i].state = CONN_CLOSED;
        c->conns[i].fd = -1;
        c->conns[i].addr = &addrs[next_addr];
        // per flow 
        c->conns[i].addr_index = next_addr;
        // ===========

        c->conns[i].next_closed = c->closed_conns;
        c->closed_conns = &c->conns[i];

        next_addr = (next_addr + 1) % addrs_num;
    }
}

static inline void conn_close(struct core *c, struct connection *co)
{
    if (co->state == CONN_OPEN || co->state == CONN_CLOSING)
        c->conn_open--;

    ss_close(c->sc, co->fd);
    co->state = CONN_CLOSED;
    co->fd = -1;

    co->next_closed = c->closed_conns;
    c->closed_conns = co;
}

static inline void conn_error(struct core *c, struct connection *co,
    const char *msg)
{
    CONN_DEBUG(c, co, "Closing connection on error\n");
    fprintf(stderr, "Closing connection %p on %p (%s) st=%u\n", co, c, msg, co->state);
    conn_close(c, co);
}

static inline int conn_receive(struct core *c, struct connection *co)
{
    int fd, ret;
    int cn;
    uint64_t latency;
    uint64_t *rx_ts;
    void *rx_buf;
    ssctx_t sc;
#ifdef PRINT_STATS
    uint64_t tsc;
#endif

    sc = c->sc;
    cn = c->id;
    fd = co->fd;

    ret = 1;
    rx_buf = co->rx_buf;
    // the first 8 byte of payload is ts
    rx_ts = rx_buf;
    do {
        STATS_ADD(c, rx_calls, 1);
#ifdef PRINT_STATS
        tsc = get_nanos_stats();
#endif
        ret = ss_read(sc, fd, rx_buf + message_size - co->rx_remain,
            co->rx_remain);
        STATS_ADD(c, rx_cycles, get_nanos_stats() - tsc);
        if (ret > 0) {
            STATS_ADD(c, rx_bytes, ret);
            /* received data */
            if (ret < message_size) {
                STATS_ADD(c, rx_short, 1);
            }
            co->rx_remain -= ret;
            if (co->rx_remain == 0) {
                /* received whole message */
		latency = get_nanos() - *rx_ts;
                __sync_fetch_and_add(&c->messages, 1);
                record_latency(c, latency);
                // per flow
                __sync_fetch_and_add(&c->messages2[co->addr_index], 1);
                record_latency2(c, co, latency);
                // ==========
                co->rx_remain = message_size;
                co->pending--;
#ifdef PRINT_STATS
                co->cnt++;
#endif
            }
        } else if (ret == 0) {
            /* end of stream */
            conn_close(c, co);
            printf("conn_close\n");
            return -1;
        } else if (ret < 0 && errno != EAGAIN) {
            /* errror, close connection */
            fprintf(stderr, "[%d] read failed: %d\n", cn, ret);
            conn_error(c, co, "read failed");
            return -1;
        } else if (ret < 0 && errno == EAGAIN) {
            /* nothing to receive */
            STATS_ADD(c, rx_fail, 1);
        }
    } while (co->pending > 0 && ret > 0);

    if (co->state == CONN_CLOSING && co->pending == 0) {
        conn_close(c, co);
        return 1;
    }

    return 0;
}

static inline int conn_send(struct core *c, struct connection *co)
{
    uint64_t now, delta_time;
    uint64_t refill;
    int fd, ret, wait_wr;
    int cn;
    uint64_t *tx_ts;
    void *tx_buf;
    ssctx_t sc;
    ss_epev_t ev;
#ifdef PRINT_STATS
    uint64_t tsc;
#endif

    sc = c->sc;
    cn = c->id;
    fd = co->fd;

    ret = 1;
    wait_wr = 0;
    tx_buf = co->tx_buf;
    tx_ts = tx_buf;
    while ((co->pending < max_pending || max_pending == 0) &&
        (co->tx_cnt < num_msgs || num_msgs == 0) && ret > 0) 
    {
        if (co->budget == 0) {
            // update budget
            now = get_nanos();
            delta_time = now - co->last_budget_update; 
            refill = delta_time / co->packet_value;
            co->budget += refill;
            if (co->budget > max_message_ps) {
                co->budget = max_message_ps;
            }
            co->last_budget_update = now - (delta_time % co->packet_value);
            continue;
        }

        /* timestamp if starting a new message */
        if (co->tx_remain == message_size) {
            *tx_ts = get_nanos();
        }

        STATS_ADD(c, tx_calls, 1);
#ifdef PRINT_STATS
        tsc = get_nanos_stats();
#endif
        ret = ss_write(sc, fd, tx_buf + message_size - co->tx_remain,
            co->tx_remain);
        STATS_ADD(c, tx_cycles, get_nanos_stats() - tsc);
        if (ret > 0) {
            STATS_ADD(c, tx_bytes, ret);
            /* sent some data */
            if (ret < message_size) {
                STATS_ADD(c, tx_short, 1);
            }
            co->tx_remain -= ret;
            if (co->tx_remain == 0) {
                /* sent whole message */
                co->pending++;
                co->tx_cnt++;

                if (co->budget > 0) {
                    // if budget is negative message per sec limit is not active
                    // message per second limit
                    // update budget 
                    now = get_nanos();
                    delta_time = now - co->last_budget_update; 
                    refill = delta_time / co->packet_value;
                    co->budget += refill;
                    if (co->budget > max_message_ps) {
                        co->budget = max_message_ps;
                    }
                    co->budget--; // pay the price of sending a message
                    co->last_budget_update = now; // - (delta_time % co->packet_value);
                }
                
                co->tx_remain = message_size;
                if ((co->pending < max_pending || max_pending == 0) &&
                    (co->tx_cnt < num_msgs || num_msgs == 0))
                {
                    /* send next message when epoll tells us to */
                    wait_wr = 1;
                    break;
                }
            }
        } else if (ret < 0 && errno != EAGAIN) {
            /* send failed */
            fprintf(stderr, "[%d] write failed: %d\n", cn, ret);
            conn_error(c, co, "write failed");
            return -1;
        } else if (ret < 0 && errno == EAGAIN) {
            /* send would block */
            wait_wr = 1;
            STATS_ADD(c, tx_fail, 1);
        }
    }

    /* make sure we epoll for write iff we're actually blocked on writes */
    if (wait_wr != co->ep_wr) {
        ev.data.ptr = co;
        ev.events = SS_EPOLLIN | SS_EPOLLHUP | SS_EPOLLERR |
            (wait_wr ? SS_EPOLLOUT : 0);
        if (ss_epoll_ctl(sc, c->ep, SS_EPOLL_CTL_MOD, fd, &ev) != 0) {
            fprintf(stderr, "[%d] epoll_ctl failed\n", cn);
            abort();
        }
        co->ep_wr = wait_wr;
    }

    /* shutdown if connection tx limit reached */
    if (num_msgs > 0 && co->tx_cnt >= num_msgs && co->state == CONN_OPEN) {
      if (shutdown(co->fd, SHUT_WR) != 0) {
        conn_error(c, co, "shutdown failed");
        return -1;
      }
      co->state = CONN_CLOSING;
    }

    return 0;
}

static inline void conn_events(struct core *c, struct connection *co,
        uint32_t events)
{
    int status;
    socklen_t slen;
#ifdef PRINT_STATS
    uint64_t tsc;
#endif
    if (co->state == CONN_CONNECTING)
        c->conn_pending--;

    /* check for errors on the connection */
    if ((events & SS_EPOLLERR) != 0) {
        conn_error(c, co, "error on epoll");
        return;
    }

    if (co->state == CONN_CONNECTING) {
        CONN_DEBUG(c, co, "Event on connecting connection\n");
        slen = sizeof(status);
        if (ss_getsockopt(c->sc, co->fd, SOL_SOCKET, SO_ERROR, &status, &slen)
            < 0)
        {
            perror("getsockopt failed");
            fprintf(stderr, "[%d] getsockopt for conn status failed\n",
                (int) c->id);
            abort();
        }

        if (status != 0) {
            conn_error(c, co, "getsockopt for connect failed");
            return;
        }

        CONN_DEBUG(c, co, "Connection successfully opened\n");
        co->state = CONN_OPEN;
        c->conn_open++;
    }

    /* receive responses */
    if (! ((events & SS_EPOLLIN) == SS_EPOLLIN &&
        conn_receive(c, co) != 0))
    {
        /* send out requests */
        if (conn_send(c, co) == 0) {
            if ((events & SS_EPOLLHUP) != 0) {
                conn_error(c, co, "error on hup");
            }
        }

    }

    // check for connection timeout
    // uint64_t now = get_nanos();
    // if (co->state == CONN_OPEN && co->duration > 0 &&
    //     now - co->begin_ts > co->duration) {
    //     // stop connection from sending any more data
    //     // this connection will be closed when there is no more receive events.
    //     if (shutdown(co->fd, SHUT_WR) != 0) {
    //       conn_error(c, co, "shutdown failed");
    //       return;
    //     }
    //     printf("duration: %ld\n", max_flow_duration);
    //     co->state = CONN_CLOSING;
    // }
}

static inline void connect_more(struct core *c)
{
  struct connection *co;
  while ((co = c->closed_conns) != NULL &&
      (c->conn_pending < max_conn_pending ||
       max_conn_pending == 0))
  {
    c->closed_conns = co->next_closed;

    conn_connect(c, co);
    c->conn_pending++;
  }
}

static void open_all(struct core *c)
{
    int i, ret, ep, status;
    struct connection *co;
    ssctx_t sc;
    socklen_t slen;
    ss_epev_t evs[max_conn_pending];
    ss_epev_t ev;

    ep = c->ep;
    sc = c->sc;

    while (c->conn_open != num_conns) {
        connect_more(c);

        /* epoll, wait for events */
        if ((ret = ss_epoll_wait(sc, ep, evs, max_conn_pending, -1)) < 0) {
            fprintf(stderr, "[%ld] epoll_wait failed\n", c->id);
            abort();
        }

        for (i = 0; i < ret; i++) {
            co = evs[i].data.ptr;

            assert(co->state == CONN_CONNECTING);
            c->conn_pending--;

            /* check for errors on the connection */
            if ((evs[i].events & (SS_EPOLLERR | SS_EPOLLHUP)) != 0) {
                conn_error(c, co, "error/hup on epoll");
                continue;
            }

            CONN_DEBUG(c, co, "Event on connecting connection\n");
            slen = sizeof(status);
            if (ss_getsockopt(c->sc, co->fd, SOL_SOCKET, SO_ERROR, &status, &slen)
                < 0)
            {
                perror("getsockopt failed");
                fprintf(stderr, "[%d] getsockopt for conn status failed\n",
                    (int) c->id);
                abort();
            }

            if (status != 0) {
                conn_error(c, co, "getsockopt for connect failed");
                return;
            }

            CONN_DEBUG(c, co, "Connection successfully opened\n");
            co->state = CONN_OPEN;
            c->conn_open++;

            ev.data.ptr = co;
            ev.events = SS_EPOLLIN | SS_EPOLLHUP | SS_EPOLLERR;
            if (ss_epoll_ctl(sc, c->ep, SS_EPOLL_CTL_MOD, co->fd, &ev) != 0) {
                fprintf(stderr, "[%ld] epoll_ctl failed\n", c->id);
                abort();
            }
            co->ep_wr = 0;
        }
    }

    for (i = 0; i < (int) num_conns; i++) {
        co = &c->conns[i];
        ev.data.ptr = co;
        ev.events = SS_EPOLLIN | SS_EPOLLHUP | SS_EPOLLERR | SS_EPOLLOUT;
        if (ss_epoll_ctl(sc, c->ep, SS_EPOLL_CTL_MOD, co->fd, &ev) != 0) {
            fprintf(stderr, "[%ld] epoll_ctl failed\n", c->id);
            abort();
        }
        co->ep_wr = 1;
    }
}

static void *thread_run(void *arg)
{
    struct core *c = arg;
    int i, cn, ret, ep, num_evs;
    struct connection *co;
    ssctx_t sc;
    ss_epev_t *evs;

    prepare_core(c);

    if (openall_delay != 0) {
        open_all(c);
        while (!start_running);
    }

    cn = c->id;
    ep = c->ep;
    sc = c->sc;

    num_evs = 4 * num_conns;
    if ((evs = calloc(num_evs, sizeof(*evs))) == NULL) {
        fprintf(stderr, "[%d] malloc failed\n", cn);
    }

    while (1) {
        if (c->closed_conns != NULL)
            connect_more(c);

        /* epoll, wait for events */
        if ((ret = ss_epoll_wait(sc, ep, evs, num_evs, -1)) < 0) {
            fprintf(stderr, "[%d] epoll_wait failed\n", cn);
            abort();
        }

        for (i = 0; i < ret; i++) {
            co = evs[i].data.ptr;
            conn_events(c, co, evs[i].events);
        }
    }
}

static inline void hist_fract_buckets(uint32_t *hist, uint64_t total,
        double *fracs, size_t *idxs, size_t num)
{
    size_t i, j;
    uint64_t sum = 0, goals[num];
    for (j = 0; j < num; j++) {
        goals[j] = total * fracs[j];
    }
    for (i = 0, j = 0; i < HIST_BUCKETS && j < num; i++) {
        sum += hist[i];
        for (; j < num && sum >= goals[j]; j++) {
            idxs[j] = i;
        }
    }
}

static inline int hist_value(size_t i)
{
    if (i == HIST_BUCKETS - 1) {
        return -1;
    }

    return i * HIST_BUCKET_US + HIST_START_US;
}

// static int parse_addrs(const char *ip, char *ports)
// {
//   size_t i;
//   char *end;
// 
//   addrs_num = 1;
//   for (i = 0; ports[i] != 0; i++)
//     if (ports[i] == ',')
//       addrs_num++;
// 
//   if ((addrs = calloc(addrs_num, sizeof(*addrs))) == NULL) {
//     perror("parse_addrs: alloc failed");
//     return -1;
//   }
// 
//   for (i = 0; i < addrs_num; i++) {
//     addrs[i].sin_family = AF_INET;
//     addrs[i].sin_addr.s_addr = inet_addr(ip);
//     addrs[i].sin_port = htons(strtoul(ports, &end, 10));
// 
//     if (*end == ',') {
//       ports = end + 1;
//     } else if (*end == 0) {
//       assert(i == addrs_num - 1);
//     } else {
//       abort();
//     }
//   }
// 
//   return 0;
// }

int parse_ip_port_pair(char *list[])
{
    size_t i;
    char *split_point;
    char ip[20];
    uint64_t port;
    char* str;
    size_t ip_len;

    // allocate addrs
    if ((addrs = calloc(addrs_num, sizeof(*addrs))) == NULL) {
      perror("parse_ip_port_pair: alloc failed");
      return 1;
    }

    for (i = 0; i < addrs_num; i++) {
        str = list[i];

        // find `:` index
        split_point = strchr(str, ':');
        if (split_point == NULL) {
            fprintf(stderr, "Failed parsing < %s >\n", str);
            exit(EXIT_FAILURE);
        }
        ip_len = (size_t)(split_point - str);
        memcpy(ip, str, ip_len);
        ip[ip_len] = '\0';
        port = strtoul(split_point + 1, NULL, 10);

        addrs[i].sin_family = AF_INET;
        addrs[i].sin_addr.s_addr = inet_addr(ip);
        addrs[i].sin_port = htons(port);
        printf("ip: %s, port: %ld\n", ip, port);
    }
    return 0;
}

void print_usage_guide()
{
    fprintf(stderr,
        "usage: ./testclient <expects parameters defined below> \n"
        "1) count_address (should be more than 1) \n"
        "2) (ip:port)+ (one or more ip:port should be given seperated by space) \n"
        "3) cores \n"
        "4) config (mtcp) \n"
        "5) [result_file_path] \n"
        "6) [message-size] (default=64) \n"
        "7) [max-pendding] \n"
        "8) [total-connections] \n"
        "9) [flow-duration] \n"
        "10) [max-message-per-sec] \n"
        "11) [open all delay] \n"
        "12) [MAX-MSGS-CONN] \n"
        "13) [MAX-PEND-CONNS] \n\n");
    /*
    "Usage: ./testclient IP PORT CORES CONFIG RESULT_PATH"
        "MESSAGE-SIZE MAX-PENDING TOTAL-CONNS FLOW-DURATION MAX-MESSAGE-PER-SEC "
        "[OPENALL-DELAY] [MAX-MSGS-CONN] [MAX-PEND-CONNS]\n");
    */
}

void print_client_config()
{
    printf("Test Client Config Values:\n"
        "1) count_address: %ld \n"
        "2) (ip:port)+: [not shown] \n"
        "3) cores: [not shown[not shown]]\n"
        "4) config (mtcp): [not shown]\n"
        "5) [result_file_path]: [not shown]\n"
        "6) [message-size]: %d \n"
        "7) [max-pendding]: %d \n"
        "8) [total-connections]: %d \n"
        "9) [flow-duration]: %ld \n"
        "10) [max-message-per-sec]: %ld \n"
        "11) [open all delay]: [not shown] \n"
        "12) [MAX-MSGS-CONN]: [not shown] \n"
        "13) [MAX-PEND-CONNS]: [not shown] \n\n", addrs_num, message_size,
        max_pending, num_conns, max_flow_duration, max_message_ps);
        fflush(stdout);
}

int main(int argc, char *argv[])
{
#ifdef USE_MTCP
    int ret;
#endif
    int parse_i;
    int num_threads, i, j;
    struct core *cs;
    uint64_t t_prev, t_cur;
    long double *ttp, tp, tp_total;
    uint32_t *hist, hx;
    uint64_t msg_total, open_total;
    double fracs[6] = { 0.5, 0.9, 0.95, 0.99, 0.999, 0.9999 };
    size_t fracs_pos[sizeof(fracs) / sizeof(fracs[0])];
    // == per flow ==
    int k;
    long double *perflow_tp;
    uint32_t **hist2;
    uint64_t *msg_total2;
    const int count_fracs = sizeof(fracs) / sizeof(fracs[0]); 
    size_t **fracs_pos2;
    // =============

    setlocale(LC_NUMERIC, "");

    if (argc < 5) {
        print_usage_guide();
        return EXIT_FAILURE;
    }

    parse_i = 1;
    // Count address
    addrs_num = atoi(argv[parse_i]);
    if (addrs_num == 0) {
        print_usage_guide();
        fprintf(stderr, "Error: count_address should be at least 1\n");
        return EXIT_FAILURE;
    }
    parse_i++;

    // Parse address
    if (parse_ip_port_pair(&argv[parse_i])) {
        print_usage_guide();
        fprintf(stderr, "Error: could not parse ip:port pairs. check the count of pairs!\n");
        return EXIT_FAILURE;
    }
    parse_i += addrs_num;

    // Cpus
    num_threads = atoi(argv[parse_i]);
    parse_i++;

#ifdef USE_MTCP
    if ((ret = mtcp_init(argv[parse_i])) != 0) {
        fprintf(stderr, "mtcp_init failed: %d\n", ret);
        return EXIT_FAILURE;
    }
#endif
    parse_i++;

    // Result file
    if(argc >= parse_i + 1) {
            file = fopen(argv[parse_i], "w");
            // signal(SIGINT, handle_sigint); 
    }
    parse_i++;

    // Message size
    if (argc >= parse_i + 1) {
        message_size = atoi(argv[parse_i]);
    }
    parse_i++;

    if (argc >= parse_i + 1) {
        max_pending = atoi(argv[parse_i]);
    }
    parse_i++;

    if (argc >= parse_i + 1) {
        num_conns = atoi(argv[parse_i]);
    }
    parse_i++;

    if (argc >= parse_i + 1) {
        max_flow_duration = atoi(argv[parse_i]);
    }
    parse_i++;

    if (argc >= parse_i + 1) {
        max_message_ps = atoi(argv[parse_i]);
    }
    parse_i++;

    if (argc >= parse_i + 1) {
        openall_delay = atoi(argv[parse_i]);
    }
    parse_i++;

    if (argc >= parse_i + 1) {
        num_msgs = atoi(argv[parse_i]);
    }
    parse_i++;

    if (argc >= parse_i + 1) {
        max_conn_pending = atoi(argv[parse_i]);
    }
    parse_i++;

    print_client_config();

    assert(sizeof(*cs) % 64 == 0);
    cs = calloc(num_threads, sizeof(*cs));
    ttp = calloc(num_threads, sizeof(*ttp));
    if (cs == NULL || ttp == NULL) {
        fprintf(stderr, "allocating thread handles failed\n");
        return EXIT_FAILURE;
    }

    if ((hist = calloc(HIST_BUCKETS, sizeof(*hist))) == NULL) {
        fprintf(stderr, "allocating total histogram failed\n");
        abort();
    }


    for (i = 0; i < num_threads; i++) {
        cs[i].id = i;
        if (pthread_create(&cs[i].pthread, NULL, thread_run, cs + i)) {
            fprintf(stderr, "pthread_create failed\n");
            return EXIT_FAILURE;
        }
    }

    // per flow
    perflow_tp = calloc(addrs_num, sizeof(*perflow_tp));
    if (perflow_tp == NULL) {
        fprintf(stderr, "failed to allocate perflow_tp\n");
        abort();
    }

    hist2 = calloc(addrs_num, sizeof(*hist2));
    if (hist2 == NULL) {
        fprintf(stderr, "failed to allocate hist2-p1\n");
        abort();
    }

    for (i = 0; i < addrs_num; i++) {
            hist2[i] = calloc(HIST_BUCKETS, sizeof(*hist2[i]));
            if (hist2[i] == NULL) {
                    fprintf(stderr, "failed to allocate hist2-p2 f:[%d]", i);
            }
    }

    msg_total2 = calloc(addrs_num, sizeof(*msg_total2));
    if (msg_total2 == NULL) {
        fprintf(stderr, "failed to allocate msg_total2\n");
        abort();
    }

    fracs_pos2 = malloc(addrs_num * sizeof(*fracs_pos2));
    if (fracs_pos2 == NULL) {
        fprintf(stderr, "failed to allocate fracs_pos2\n");
        abort();
    }

    for (i = 0; i < addrs_num; i++) {
            fracs_pos2[i] = calloc(count_fracs, sizeof(*fracs_pos2[i]));
            if (fracs_pos2[i] == NULL) {
                fprintf(stderr, "failed to allocate fracs_pos2-part2\n");
                abort();
            }
    }
    // =================

    if (openall_delay != 0) {
        sleep(openall_delay);
        start_running = 1;
    }

    t_prev = get_nanos();
    while (1) {
        sleep(1);
        t_cur = get_nanos();
        tp_total = 0;
        msg_total = 0;
        open_total = 0;
        for (i = 0; i < num_threads; i++) {
            tp = cs[i].messages;
            open_total += cs[i].conn_open;
            cs[i].messages = 0;
            tp /= (double) (t_cur - t_prev) / 1000000000.;
            ttp[i] = tp;
            tp_total += tp;

            for (j = 0; j < HIST_BUCKETS; j++) {
                hx = cs[i].hist[j];
                msg_total += hx;
                hist[j] += hx;
            }
        }
        // per flow ===
	for (k = 0; k < addrs_num; k++) {
	    perflow_tp[k] = 0;
	    msg_total2[k] = 0;
	    for (j = 0; j < HIST_BUCKETS; j++)
                hist2[k][j] = 0;
	}

        for (i = 0; i < num_threads; i++) {
            for (k = 0; k < addrs_num; k++) {
                tp = cs[i].messages2[k];
                cs[i].messages2[k] = 0;
                tp /= (double) (t_cur - t_prev) / 1000000000.;
                perflow_tp[k] += tp;

                for (j = 0; j < HIST_BUCKETS; j++) {
                    hist2[k][j] += cs[i].hist2[k][j]; 
                    msg_total2[k] += cs[i].hist2[k][j]; 
                }
            }
        }
        // ==========

        hist_fract_buckets(hist, msg_total, fracs, fracs_pos,
                sizeof(fracs) / sizeof(fracs[0]));

        // per flow =====
        for (k = 0; k < addrs_num; k++) {
            hist_fract_buckets(hist2[k], msg_total2[k], fracs, fracs_pos2[k],
                    count_fracs);
        }
        // ===============


        printf("TP: total=%'.2Lf mbps  50p=%d us  90p=%d us  95p=%d us  "
                "99p=%d us  99.9p=%d us  99.99p=%d us  flows=%lu",
                tp_total * message_size * 8 / 1000000.,
                hist_value(fracs_pos[0]), hist_value(fracs_pos[1]),
                hist_value(fracs_pos[2]), hist_value(fracs_pos[3]),
                hist_value(fracs_pos[4]), hist_value(fracs_pos[5]),
                open_total);

        if(file) {
                fprintf(file, "TP: total=%'.2Lf mbps  50p=%d us  90p=%d us  95p=%d us  "
                "99p=%d us  99.9p=%d us  99.99p=%d us  flows=%lu\n",
                tp_total * message_size * 8 / 1000000.,
                hist_value(fracs_pos[0]), hist_value(fracs_pos[1]),
                hist_value(fracs_pos[2]), hist_value(fracs_pos[3]),
                hist_value(fracs_pos[4]), hist_value(fracs_pos[5]),
                open_total);
                fflush(file);
        }

#ifdef PRINT_PERCORE
        for (i = 0; i < num_threads; i++) {
            printf("core[%d]=%'.2Lf mbps  ", i,
                    ttp[i] * message_size * 8 / 1000000.);
        }
#endif
        printf("\n");

        // per flow ===
        for (k = 0; k < addrs_num; k++) {
            printf("flow=%d tp=%'.2Lf mbps tp=%'.2Lf pps  50p=%d us  90p=%d us  95p=%d us  "
                    "99p=%d us  99.9p=%d us  99.99p=%d us\n",
                    k,
                    perflow_tp[k] * message_size * 8 / 1000000.,
                    perflow_tp[k],
                    hist_value(fracs_pos2[k][0]), hist_value(fracs_pos2[k][1]),
                    hist_value(fracs_pos2[k][2]), hist_value(fracs_pos2[k][3]),
                    hist_value(fracs_pos2[k][4]), hist_value(fracs_pos2[k][5]));
            if(file) {
                fprintf(file, "flow=%d tp=%'.2Lf mbps tp=%'.2Lf pps  50p=%d us  90p=%d us  95p=%d us  "
                        "99p=%d us  99.9p=%d us  99.99p=%d us\n",
                        k,
                        perflow_tp[k] * message_size * 8 / 1000000.,
                        perflow_tp[k],
                        hist_value(fracs_pos2[k][0]), 
                        hist_value(fracs_pos2[k][1]),
                        hist_value(fracs_pos2[k][2]),
                        hist_value(fracs_pos2[k][3]),
                        hist_value(fracs_pos2[k][4]),
                        hist_value(fracs_pos2[k][5]));
                fflush(file);
            }
        }
        // ===========

#ifdef PRINT_STATS
        printf("stats:\n");
        for (i = 0; i < num_threads; i++) {
            uint64_t rx_calls = read_cnt(&cs[i].rx_calls);
            uint64_t rx_cycles = read_cnt(&cs[i].rx_cycles);
            uint64_t tx_calls = read_cnt(&cs[i].tx_calls);
            uint64_t tx_cycles = read_cnt(&cs[i].tx_cycles);
            uint64_t rxc = (rx_calls == 0 ? 0 : rx_cycles / rx_calls);
            uint64_t txc = (tx_calls == 0 ? 0 : tx_cycles / tx_calls);

            printf("    core %2d: (rt=%"PRIu64", rs=%"PRIu64", rf=%"PRIu64
                ", rxc=%"PRIu64", rb=%"PRIu64", tt=%"PRIu64", ts=%"PRIu64", tf=%"PRIu64
                ", txc=%"PRIu64", tb=%"PRIu64")\n", i,
                rx_calls, read_cnt(&cs[i].rx_short), read_cnt(&cs[i].rx_fail),
                rxc, read_cnt(&cs[i].rx_bytes),
                tx_calls, read_cnt(&cs[i].tx_short), read_cnt(&cs[i].tx_fail),
                txc, read_cnt(&cs[i].tx_bytes));
        }
        for (i = 0; i < num_threads; i++) {
            for (j = 0; j < num_conns; j++) {
                printf("      t[%d].conns[%d]:  pend=%u  rx_r=%u  tx_r=%u  cnt=%"
                        PRIu64" fd=%d\n",
                        i, j, cs[i].conns[j].pending, cs[i].conns[j].rx_remain,
                        cs[i].conns[j].tx_remain, cs[i].conns[j].cnt,
                        cs[i].conns[j].fd);
            }
        }
#endif

        fflush(stdout);
        memset(hist, 0, sizeof(*hist) * HIST_BUCKETS);

        t_prev = t_cur;
    }
    fclose(file);

#ifdef USE_MTCP
    mtcp_destroy();
#endif
}
