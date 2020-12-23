#include <assert.h>
#include <sys/epoll.h>
#include "csapp.h"
#include "stdint.h"
#include <netinet/tcp.h>

#define WRITE_BUF_SIZE (1<<25) /* Write buffer size. 32MB. */
#define READ_BUF_SIZE 128	/* Per-connection internal buffer size for reads. */
#define MAXEVENTS 1024  /* Maximum number of epoll events per call */

#define XEPOLL_CTL(efd, flags, fd, event)   ({  \
    if (epoll_ctl(efd, flags, fd, event) < 0) { \
    perror ("epoll_ctl");                   \
    exit (-1);                              \
    }})

#define XCLOCK_GETTIME(tp)   ({  \
    if (clock_gettime(CLOCK_REALTIME, (tp)) != 0) {                   \
    printf("XCLOCK_GETTIME failed!\n");              \
    exit(-1);                                            \
    }})

static uint8_t write_buf[WRITE_BUF_SIZE];

/*
 * Data structure to keep track of server connection state.
 *
 * The connection objects are also maintained in a global doubly-linked list.
 * There is a dummy connection head at the beginning of the list.
 */
struct conn {
  /* Points to the previous connection object in the doubly-linked list. */
  struct conn *prev;
  /* Points to the next connection object in the doubly-linked list. */
  struct conn *next;
  /* File descriptor associated with this connection. */
  int fd;
  /* Internal buffer to temporarily store the contents of a read. */
  char buffer[READ_BUF_SIZE];
  /* Size of the data stored in the buffer. */
  size_t size;
  /* The shuffle that this connection is a part of. */
  struct shuffle *shuffle;

  /* Number of bytes requested for the current message. */
  size_t request_bytes;
  /* Number of bytes of the current message written. */
  size_t written_bytes;

  /* The startting time of the connection. */
  struct timespec start;
  /* The finishing time of the connection. */
  struct timespec finish;
};

/*
 * Data structure to keep track of each independent shuffle event.
 * Each shuffle holds the completion count and timers.
 */
struct shuffle {
  /* Points to the previous shuffle object in the doubly-linked list. */
  struct shuffle *prev;
  /* Points to the next shuffle object in the doubly-linked list. */
  struct shuffle *next;
  /* The server request unit. */
  uint64_t sru;
  /* The total number of requests sent out. */
  uint32_t numreqs;
  /* The number of completed requests so far. */
  uint32_t completed;
  /* The number of max simultaneous connections. */
  uint32_t slimit;
  /* Doubly-linked list of active server connection objects. */
  struct conn *conn_head;
  /* An array of the form [hostname, port]*. */
  char **hosts;

  /* The startting time of the shuffle. */
  struct timespec start;
  /* The finishing time of the shuffle. */
  struct timespec finish;
};

/*
 * Data structure to keep track of active server connections.
 */
struct shuffle_pool {
  /* The epoll file descriptor. */
  int efd;
  /* The epoll events. */
  struct epoll_event events[MAXEVENTS];
  /* Number of ready events returned by epoll. */
  int nevents;
  /* Doubly-linked list of active server connection objects. */
  struct shuffle *shuffle_head;
  /* Number of active shuffles. */
  //unsigned int nr_shuffles;
  /* Number of active connections. */
  unsigned int nr_conns;

};

/* Set verbosity to 1 for debugging. */
static int verbose = 0;

static inline unsigned int str_to_ip(char *str_ip)
{
  unsigned int result;
  unsigned int a,b,c,d;
  int ret;
  ret = sscanf(str_ip, "%d.%d.%d.%d", &a, &b, &c, &d);
  assert(ret == 4);
  result = a << 24 | b << 16 | c << 8 | d;
  return result;
}

/* Local function definitions. */
static void increment_and_check_shuffle(struct shuffle *inc, struct shuffle_pool *p);

double get_secs(struct timespec time)
{
  return (time.tv_sec) + (time.tv_nsec * 1e-9);
}

/*
 * open_server - open connection to server at <hostname, port>
 *   and return a socket descriptor ready for reading and writing.
 *   Returns -1 and sets errno on Unix error.
 *   Returns -2 and sets h_errno on DNS (gethostbyname) error.
 */
int open_server(char *hostname, int port)
{
  int serverfd;
  struct hostent *hp;
  struct sockaddr_in serveraddr;
  int opts = 0;
  int optval = 1;

  if ((serverfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    return -1; /* check errno for cause of error */

  /* Set the connection descriptor to be non-blocking. */
  opts = fcntl(serverfd, F_GETFL);
  if (opts < 0) {
    printf("fcntl error.\n");
    exit(-1);
  }
  opts = (opts | O_NONBLOCK);
  if (fcntl(serverfd, F_SETFL, opts) < 0) {
    printf("fcntl set error.\n");
    exit(-1);
  }

  /* Set the connection to allow for reuse */
  if (setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
    printf("setsockopt error.\n");
    exit(-1);
  }

  /* Fill in the server's IP address and port */
  if ((hp = gethostbyname(hostname)) == NULL)
    return -2; /* check h_errno for cause of error */
  bzero((char *) &serveraddr, sizeof(serveraddr));
  serveraddr.sin_family = AF_INET;
  //  bcopy((char *)hp->h_addr,
  //        (char *)&serveraddr.sin_addr.s_addr, hp->h_length);
  serveraddr.sin_addr.s_addr = htonl((unsigned int) str_to_ip(hostname));
  serveraddr.sin_port = htons(port);

  /* Establish a connection with the server */
  if (connect(serverfd, (SA *) &serveraddr, sizeof(serveraddr)) < 0) {
    if (errno !=EINPROGRESS) {
      printf("server connect error");
      perror("");
      return -1;
    }
  }
  return serverfd;
}

/*******************************************************************************
 * Maintaining Client Connections.
 ******************************************************************************/

/*
 * Requires:
 * c should be a connection object and not be NULL.
 * p should be a connection pool and not be NULL.
 *
 * Effects:
 * Adds the connection object to the tail of the doubly-linked list.
 */
  static void
add_conn_list(struct conn *c, struct shuffle *i)
{
  c->next = i->conn_head->next;
  c->prev = i->conn_head;
  i->conn_head->next->prev = c;
  i->conn_head->next = c;
}

/*
 * Requires:
 * c should be a connection object and not be NULL.
 *
 * Effects:
 * Removes the connection object from the doubly-linked list.
 */
  static void
remove_conn_list(struct conn *c)
{
  c->next->prev = c->prev;
  c->prev->next = c->next;
}


/*
 * Requires:
 * c should be a connection object and not be NULL.
 * inc should be an shuffle and not be NULL.
 * p should be an shuffle pool and not be NULL.
 *
 * Effects:
 * Closes a server connection and cleans up the associated state. Removes it
 * from the doubly-linked list and frees the connection object.
 */
  void
remove_server(struct conn *c, struct shuffle_pool *p)
{
  if (verbose)
    printf("Closing connection fd %d...\n", c->fd);

  /* Get the total number of TCP retransmissions */
  struct tcp_info info;
  int infoLen = sizeof(info);
  getsockopt(c->fd, SOL_TCP, TCP_INFO, (void *)&info, (socklen_t *)&infoLen);
  long packet_drops = info.tcpi_total_retrans;

  /* Supposedly closing the file descriptor cleans up epoll,
   * but do it first anyways to be nice... */
  XEPOLL_CTL(p->efd, EPOLL_CTL_DEL, c->fd, NULL);

  /* Close the file descriptor. */
  Close(c->fd);

  /* Decrement the number of connections. */
  p->nr_conns--;

  /* Remove the connection from the list. */
  remove_conn_list(c);

  /* Stop Timing and Log. */
  XCLOCK_GETTIME(&c->finish);
  double start = get_secs(c->start);
  double finish = get_secs(c->finish);
  double tot_time = finish - start;
  double bandwidth = (c->written_bytes * 8.0 * 1e-9)/(tot_time);
  printf("- [%.30g, %.30g, %.9g, %ld, %.9g, %ld, 1]\n", start, finish, tot_time, c->written_bytes, bandwidth, packet_drops);

  /* Increment and check the number of finished connections. */
  increment_and_check_shuffle(c->shuffle, p);

  /* Free the connection object. */
  Free(c);
}

/*
 * Requires:
 * connfd should be a valid connection descriptor.
 * inc should be an shuffle and not be NULL.
 *
 * Effects:
 * Allocates a new connection object and initializes the associated state. Adds
 * it to the doubly-linked list.
 */
  static void
add_server(int connfd, struct timespec start, struct shuffle *inc, struct shuffle_pool *p)
{
  struct conn *new_conn;
  struct epoll_event event;

  /* Allocate a new connection object. */
  new_conn = Malloc(sizeof(struct conn));

  new_conn->fd = connfd;
  new_conn->size = 0;
  new_conn->shuffle = inc;

  /* Bytes have been requested but not written yet. */
  new_conn->request_bytes = inc->sru;
  new_conn->written_bytes = 0;

  /* Add this descriptor to the write descriptor set. */
  event.data.fd = connfd;
  event.data.ptr = new_conn;
  event.events = EPOLLOUT;
  XEPOLL_CTL(p->efd, EPOLL_CTL_ADD, connfd, &event);

  /* Update the number of server connections. */
  p->nr_conns++;

  new_conn->start = start;

  add_conn_list(new_conn, inc);
}

/*
 * Requires:
 * hostname should be a valid hostname
 * port should be a valid port
 * p should be a connection pool and not be NULL.
 *
 * Effects:
 * Accepts a new server connection. Sets the resulting connection file
 * descriptor to be non-blocking. Adds the server to the connection pool.
 */
  static void
start_new_connection(char *hostname, int port, struct shuffle *inc, struct shuffle_pool *p)
{
  int connfd;
  struct timespec start;

  //XXX: Wrong spot for this
  /* Start Timing. */
  XCLOCK_GETTIME(&start);

  /* Accept the new connection. */
  connfd = open_server(hostname, port);
  if (connfd < 0) {
    printf("# Unable to open connection to (%s, %d)\n", hostname, port);
    return;
    //exit(-1);
  }

  if (verbose) {
    printf("Started new connection with (%s %d) new fd %d...\n",
        hostname, port, connfd);
  }

  /* Create new connection object and add it to the connection pool. */
  add_server(connfd, start, inc, p);
}


/*******************************************************************************
 * Manage shuffle Events.
 ******************************************************************************/

/*
 * Requires:
 * sru should be the server request unit in bytes.
 * numreqs should be the number of requests
 *
 * Effects:
 * Allocates an shuffle object and initializes it.
 */
  static struct shuffle *
alloc_shuffle(uint64_t sru, uint32_t numreqs, uint32_t slimit, char **hosts)
{
  struct shuffle *inc;

  if (verbose)
    printf("Allocating shuffle\n");

  inc = Malloc(sizeof(struct shuffle));
  inc->sru = sru;
  inc->numreqs = numreqs;
  inc->slimit = slimit;
  inc->completed = 0;
  inc->hosts = hosts;

  /* Allocate and initialize the dummy connection head. */
  inc->conn_head = Malloc(sizeof(struct conn));
  inc->conn_head->next = inc->conn_head;
  inc->conn_head->prev = inc->conn_head;

  /* Start Timing. */
  XCLOCK_GETTIME(&inc->start);

  return (inc);
}

/*
 * Requires:
 * inc should be an shuffle object and not be NULL.
 *
 * Effects:
 * Frees the shuffle object and the memory holding the contents of the message.
 */
  static void
free_shuffle(struct shuffle *inc)
{
  if (verbose)
    printf("Freeing shuffle\n");

  Free(inc->conn_head);
  Free(inc);
  exit(0);
}

/*
 * Requires:
 * inc should be an shuffle object and not be NULL.
 *
 * Effects:
 * Removes the shuffle object from the doubly-linked list.
 */
  static void
remove_shuffle_list(struct shuffle *inc)
{
  inc->next->prev = inc->prev;
  inc->prev->next = inc->next;
}

/*
 * Requires:
 * inc should be an shuffle object and not be NULL.
 *
 * Effects:
 * Adds the shuffle object to the tail of the doubly-linked list.
 */
  static void
add_shuffle_list(struct shuffle *inc, struct shuffle_pool *p)
{
  inc->next = p->shuffle_head;
  inc->prev = p->shuffle_head->prev;
  p->shuffle_head->prev->next = inc;
  p->shuffle_head->prev = inc;
}


/*
 * Requires:
 * sru is the server request unit
 * numreqs is the number of servers to the request from
 * slimit is the number of simultaneous shuffles
 * hosts is an array of the form [hostname, port]*
 * p is a valid shuffle pool and is not NULL
 *
 * Effects:
 * Starts the shuffle event.
 */
  static void
start_shuffle(uint64_t sru, uint32_t numreqs, uint32_t slimit,char **hosts, struct shuffle_pool *p)
{
  int i, port;
  char *hostname;
  struct shuffle *inc;

  /* Alloc the shuffle and add it to the shuffle_pool */
  inc = alloc_shuffle(sru, numreqs, slimit, hosts);
  add_shuffle_list(inc, p);
  //p->nr_shuffles++;

  //XXX Need to check that slimit is always <= numreqs

  /* Connect to the allowed number of servers */
  for (i = 0; i < (slimit*2); i=i+2) {
    hostname = hosts[i];
    port = atoi(hosts[i + 1]);
    if (verbose)
      printf("Starting new connection to (%s %d)...\n", hostname, port);
    start_new_connection(hostname, port, inc, p);
  }
}

/*
 * Requires:
 * inc is a valid shuffle object ans is not NULL
 *
 * Effects:
 * Starts the remaining shuffle connections after the slimit connections are done.
 */
  static void
resume_shuffle(struct shuffle *inc, struct shuffle_pool *p)
{
  int i, port;
  char *hostname;
  //TODO
  /* 1. Find the number of connections to launch --> if inc->numreqs < slimit, launch inc->numreqs else launch slimit*/
  uint32_t numremreqs;
  if ((inc->numreqs - inc->completed) < inc->slimit)
    numremreqs = inc->numreqs - inc->completed;
  else
    numremreqs = inc->slimit;

  for(i = (inc->completed*2); i < ((inc->completed+numremreqs)*2); i=i+2){
    hostname = inc->hosts[i];
    port = atoi(inc->hosts[i + 1]);
    if (verbose)
      printf("Starting new connection to (%s %d)...\n", hostname, port);
    start_new_connection(hostname, port, inc, p);
  }
}

/*
 * Requires:
 * inc is a valid shuffle object ans is not NULL
 * p is a valid shuffle pool and is not NULL
 *
 * Effects:
 * Finishes the shuffle event.
 */
static void
//finish_shuffle(struct shuffle *inc, struct shuffle_pool *p)
finish_shuffle(struct shuffle *inc)
{
  /* Assert that there are no connections left for this shuffle. */
  assert(inc->conn_head->next == inc->conn_head);

  /* Assert that the shuffle finished. */
  assert(inc->numreqs == inc->completed);

  /* Remove this shuffle from the pool. */
  remove_shuffle_list(inc);
  //p->nr_shuffles--;

  if (verbose)
    printf("Finished receiving %ld bytes from each of the %d servers\n",
        inc->sru, inc->numreqs);

  /* Stop Timing and Log. */
  XCLOCK_GETTIME(&inc->finish);
  double start = get_secs(inc->start);
  double finish = get_secs(inc->finish);
  double tot_time = finish - start;
  double bandwidth = (inc->sru * inc->numreqs * 8.0 * 1e-9)/(tot_time);
  printf("- [%.30g, %.30g, %.9g, %ld, %.9g, -1, %d]\n", start, finish, tot_time, inc->sru * inc->numreqs, bandwidth, inc->numreqs);

  /* Free the shuffle. */
  free_shuffle(inc);
}

/*
 * Requires:
 * inc is a valid shuffle object ans is not NULL
 *
 * Effects:
 * Increments the completed count for the shuffle.
 * Finishes the shuffle event if it is finished.
 */
  static void
increment_and_check_shuffle(struct shuffle *inc, struct shuffle_pool *p)
{
  inc->completed++;
  if (inc->completed == inc->numreqs)
    finish_shuffle(inc);
  else if (inc-> completed == inc->slimit) /* Max simultaneous shuffles are done, but we still have few more. */
    resume_shuffle(inc, p);
}

/*
 * Requires:
 * p should be an shuffle pool and not be NULL.
 *
 * Effects:
 * Initializes an empty shuffle pool. Allocates and initializes dummy list
 * heads.
 */
  static void
init_pool(struct shuffle_pool *p)
{
  /* Initially, there are no connected descriptors. */
  //p->nr_shuffles = 0;
  p->nr_conns = 0;

  /* Allocate and initialize the dummy connection head. */
  p->shuffle_head = Malloc(sizeof(struct shuffle));
  p->shuffle_head->next = p->shuffle_head;
  p->shuffle_head->prev = p->shuffle_head;

  /* Init epoll. */
  p->efd = epoll_create1(0);
  if (p->efd < 0) {
    printf ("epoll_create error!\n");
    exit(1);
  }
}


/*******************************************************************************
 * Write Messages.
 ******************************************************************************/

/*
 * Requires:
 * p should be a connection pool and not be NULL.
 *
 * Effects:
 * Writes the appropriate messages to each ready file descriptor in the write set.
 */
  static void
write_message(struct conn *c, struct shuffle_pool *p)
{
  struct timespec now;
  XCLOCK_GETTIME(&now);
  int n;

  /* Perform the write system call. */
  // n = write(c->fd, write_buf, c->request_bytes - c->written_bytes);
  n = write(c->fd, write_buf, 3000);

  /* Data written. */
  if (n > 0) {

    /* Update the bytes written count. */
    c->written_bytes += n;

    /* Check if entire msg has been written on this connection. */
    // if (c->written_bytes == c->request_bytes) {
    // printf("now: %ld, start: %ld, duration: %ld\n",
    //         now.tv_sec, c->start.tv_sec, c->request_bytes);
    if (now.tv_sec - c->start.tv_sec >= c->request_bytes) {
      if (verbose) {
        printf("Finished writing %d bytes to fd %d.\n",
            (int)c->request_bytes, c->fd);
      }
      /* The shuffle message has been sent to a server, update epoll.
       * Remove it from the write set and close the connection. */
      remove_server(c, p);
    }
  }
  /* Error (possibly). */
  else if (n < 0) {
    /* If errno is EAGAIN, it just means we have to write again. */
    if (errno != EAGAIN) {
      printf(" close by error\n");
      remove_server(c, p);
    }
  }
  /* Connection closed by server. */
  else {
    printf(" close by server\n");
    remove_server(c, p);
  }
}


int main(int argc, char **argv)
{
  // uint64_t sru;
  uint64_t duration; // seconds
  int slimit;
  struct shuffle_pool pool;
  struct conn *connp;
  int i;

  /* initialize random() */
  srandom(time(0));

  if (verbose)
    printf("Starting shuffle client...\n");

  if ((argc % 2) != 1 || argc < 5) {
    fprintf(stderr, "usage: %s <server-request-unit-bytes> <max_simultaneous_flows> [<hostname> <port>]+\n", argv[0]);
    exit(0);
  }

  // sru = atol(argv[1]);
  duration = atol(argv[1]);
  slimit = atoi(argv[2]);

  /* print the command invoked with args */
  printf("# %s", argv[0]);
  for(i=1 ; i<argc ; i++){
    printf(" %s", argv[i]);
  }
  printf("\n");

  /* Initialize the connection pool. */
  init_pool(&pool);

  /* Start a shuffle. */
  start_shuffle(duration, (argc-3)/2, slimit, &argv[3], &pool);

  printf("- [startSeconds, endSeconds, totTime, totBytes, bandwidthGbps, numPacketDrop, numSockets]\n");
  while(1)
  {
    /*
     * Wait until:
     * 1. Data is available to be read from a socket.
     */
    pool.nevents = epoll_wait (pool.efd, pool.events, MAXEVENTS, 0);
    for (i = 0; i < pool.nevents; i++) {
      if ((pool.events[i].events & EPOLLERR) ||
          (pool.events[i].events & EPOLLHUP)) {
        /* An error has occured on this fd */
        fprintf (stderr, "epoll error\n");
        perror("");
        close (pool.events[i].data.fd);
        continue;
      }

      /* Handle Writes. */
      if (pool.events[i].events & EPOLLOUT) {
        connp = (struct conn *) pool.events[i].data.ptr;
        write_message(connp, &pool);
      }
    }
  }

  return (0);
}
