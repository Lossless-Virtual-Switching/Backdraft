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

static uint32_t max_pending = 1;
static uint32_t max_conn_pending = 1;
static uint32_t message_size = 64;
static uint32_t num_conns = 1;
static uint32_t num_msgs = 0;
static uint32_t openall_delay = 0;
static struct sockaddr_in *addrs;
static size_t addrs_num;
static volatile int start_running = 0;
static FILE *file = NULL;
static uint64_t exp_begin_ts = 0;

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

#define sec_in_ns 1000000000UL
#define DURATION_BETWEEN_PACKETS (sec_in_ns / 1000)
void wait_to_send_next_packet(uint64_t ns)
{
  struct timespec rqt = {
    .tv_sec = ns / sec_in_ns,
    .tv_nsec = ns % sec_in_ns,
  };
  while(nanosleep(&rqt, &rqt) != 0){}
}

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
    if (exp_begin_ts == 0)
      exp_begin_ts = get_nanos();
    double now = (get_nanos() - exp_begin_ts) / 1000000000.0;
    fprintf(file, "%f %lu\n", now, nanos / 1000);
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

    if ((events & SS_EPOLLIN) == SS_EPOLLIN &&
        conn_receive(c, co) != 0)
    {
        return;
    }

    wait_to_send_next_packet(DURATION_BETWEEN_PACKETS);

    /* send out requests */
    if (conn_send(c, co) != 0) {
        return;
    }

    if ((events & SS_EPOLLHUP) != 0) {
        conn_error(c, co, "error on hup");
        return;
    }
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
        "5) result_file_path \n"
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
        "5) result_file_path: [not shown]\n"
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
    int num_threads, i;
    struct core *cs;
    long double tp;

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
    } else {
      print_usage_guide();
      fprintf(stderr, "Error: result file was not specified\n");
      return EXIT_FAILURE;
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

    for (i = 0; i < num_threads; i++) {
        cs[i].id = i;
        if (pthread_create(&cs[i].pthread, NULL, thread_run, cs + i)) {
            fprintf(stderr, "pthread_create failed\n");
            return EXIT_FAILURE;
        }
    }

    if (openall_delay != 0) {
        sleep(openall_delay);
        start_running = 1;
    }

    while (1) {
        sleep(1);
  tp = cs[0].messages;
  // not accurate
        printf("tp: %Lf\n", tp);
  fflush(file);
        cs[0].messages = 0;
    }
    fclose(file);

#ifdef USE_MTCP
    mtcp_destroy();
#endif
}
