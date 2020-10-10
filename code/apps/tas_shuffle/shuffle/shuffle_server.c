#include <assert.h>
#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h> //for exp()
#include <sys/time.h> //for gettimeofday()
#include <sys/epoll.h>
#include <netinet/tcp.h>
#include "csapp.h"

#define XCLOCK_GETTIME(tp)   ({  \
    if (clock_gettime(CLOCK_REALTIME, (tp)) != 0) {                   \
        printf("XCLOCK_GETTIME failed!\n");              \
        exit(-1);                                            \
    }})

#define XEPOLL_CTL(efd, flags, fd, event)   ({  \
    if (epoll_ctl(efd, flags, fd, event) < 0) { \
        perror ("epoll_ctl");                   \
        exit (-1);                              \
    }})


#define WRITE_BUF_SIZE 128  /* Per-connection internal buffer size for writes. */
#define READ_BUF_SIZE (1<<25) /* Read buffer size. 32MB. */
#define MAXEVENTS 1024  /* Maximum number of epoll events per call */

static uint8_t read_buf[READ_BUF_SIZE];

/*
 * Data structure to keep track of client connection state.
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
    char buffer[WRITE_BUF_SIZE];    
    /* Size of the data stored in the buffer. */
    size_t size;            

    /* Number of bytes requested for the current message. */
        int request_bytes;
    /* Number of bytes of the current message (write_msg) written. */
    int written_bytes;      
};

/* 
 * Data structure to keep track of active client connections.
 */
struct conn_pool { 
        /* The listening fild descriptor. */
        int listenfd;
    /* The epoll file descriptor. */
        int efd;
        /* The epoll events. */
        struct epoll_event events[MAXEVENTS];
    /* Number of ready events returned by epoll. */
    int nevents;            
    /* Doubly-linked list of active client connection objects. */
    struct conn *conn_head;
    /* Number of active client connections. */
    unsigned int nr_conns;
}; 

/* Set verbosity to 1 for debugging. */
static int verbose = 0;

/*  
 * open_listenfd - open and return a listening socket on port
 *     Returns -1 and sets errno on Unix error.
 */
int open_listen(int port) 
{
    int listenfd, optval=1;
    struct sockaddr_in serveraddr;
    int opts = 0;
  
    /* Create a socket descriptor */
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return -1;
 
    /* Eliminates "Address already in use" error from bind. */
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, 
                   (const void *)&optval , sizeof(int)) < 0)
        return -1;

    /* Farbod: Set listen socket non-blocking */
    opts = fcntl(listenfd, F_GETFL);
    if (opts < 0) {
        printf("fcntl error.");
        exit(-1);
    }
    opts = (opts | O_NONBLOCK);
    if (fcntl(listenfd, F_SETFL, opts) < 0) {
        printf("fcntl set error.");
        exit(-1);
    }


    /* Listenfd will be an endpoint for all requests to port
       on any IP address for this host */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET; 
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY); 
    serveraddr.sin_port = htons((unsigned short)port); 
    if (bind(listenfd, (SA *)&serveraddr, sizeof(serveraddr)) < 0)
        return -1;

    /* Make it a listening socket ready to accept connection requests */
    if (listen(listenfd, LISTENQ) < 0)
        return -1;
    return listenfd;
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
add_conn_list(struct conn *c, struct conn_pool *p)
{
    c->next = p->conn_head->next;
    c->prev = p->conn_head;
    p->conn_head->next->prev = c;
    p->conn_head->next = c;
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
 * p should be a connection pool and not be NULL.
 *
 * Effects:
 * Closes a client connection and cleans up the associated state. Removes it
 * from the doubly-linked list and frees the connection object.
 */
void
remove_client(struct conn *c, struct conn_pool *p)
{
    if (verbose)
        printf("Closing connection fd %d...\n", c->fd);

    /* Supposedly closing the file descriptor cleans up epoll,
         * but do it first anyways to be nice... */
         XEPOLL_CTL(p->efd, EPOLL_CTL_DEL, c->fd, NULL);

    /* Close the file descriptor. */
    Close(c->fd); 
    
    /* Decrement the number of connections. */
    p->nr_conns--;

    /* Remove the connection from the list. */
    remove_conn_list(c);

    /* Free the connection object. */
    Free(c);
}

/* 
 * Requires:
 * connfd should be a valid connection descriptor.
 * p should be a connection pool and not be NULL.
 *
 * Effects:
 * Allocates a new connection object and initializes the associated state. Adds
 * it to the doubly-linked list.
 */
static void 
add_client(int connfd, struct conn_pool *p) 
{
    struct conn *new_conn;
    struct epoll_event event;

    /* Allocate a new connection object. */
    new_conn = Malloc(sizeof(struct conn));

    new_conn->fd = connfd;
    new_conn->size = 0;

    /* No bytes have been requested or written yet. */
    new_conn->request_bytes = -1;
    new_conn->written_bytes = 0;

    /* Add this descriptor to the read descriptor set. */
    event.data.fd = connfd;
    event.data.ptr = new_conn;
    event.events = EPOLLIN;
    XEPOLL_CTL(p->efd, EPOLL_CTL_ADD, connfd, &event);

    /* Update the number of client connections. */
    p->nr_conns++;

    add_conn_list(new_conn, p);
}

/* 
 * Requires:
 * listenfd should be a valid listen file descriptor.
 * p should be a connection pool and not be NULL.
 *
 * Effects:
 * Accepts a new client connection. Sets the resulting connection file
 * descriptor to be non-blocking. Adds the client to the connection pool.
 */
static void
handle_new_connection(int listenfd, struct conn_pool *p)
{
    struct sockaddr_in clientaddr;
    socklen_t clientlen = sizeof(struct sockaddr_in);
    struct hostent *hp;
    char *haddrp;
    int connfd;
    int opts = 0;
    int flag = 1;

    /* Accept the new connection. */
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); 

    if (connfd == -1 && errno == EAGAIN) {
        /* Farbod: There is no connection waiting here */
        return;
    }
            
    /* Set the connection descriptor to be non-blocking. */
    opts = fcntl(connfd, F_GETFL);
    if (opts < 0) {
        printf("fcntl error.");
        exit(-1);
    }
    opts = (opts | O_NONBLOCK);
    if (fcntl(connfd, F_SETFL, opts) < 0) {
        printf("fcntl set error.");
        exit(-1);
    }

    /* Frabod: No Nagle */
    setsockopt(connfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    if (verbose) {
        /* Farbod: TAS does not support gethostbyaddr */
        // hp = Gethostbyaddr((const char *)&clientaddr.sin_addr.s_addr,
        //     sizeof(clientaddr.sin_addr.s_addr), AF_INET);
        // haddrp = inet_ntoa(clientaddr.sin_addr);
        printf("Accepted new connection request from %s (%s) new fd %d...\n",
            "?", "?", connfd);
           //  hp->h_name, haddrp, connfd);
    }

    /* Create new connection object and add it to the connection pool. */
    add_client(connfd, p);
}

/* 
 * Requires:
 * listenfd should be a valid listen file descriptor.
 * p should be a connection pool and not be NULL.
 *
 * Effects:
 * Initializes an empty connection pool. Allocates and initializes dummy list
 * heads.
 */
static void 
init_pool(int listenfd, struct conn_pool *p) 
{
        struct epoll_event event;

    /* Initially, there are no connected descriptors. */
    p->nr_conns = 0;                   

    /* Allocate and initialize the dummy connection head. */
    p->conn_head = Malloc(sizeof(struct conn));
    p->conn_head->next = p->conn_head;
    p->conn_head->prev = p->conn_head;

        /* Initialize epoll. */
        p->listenfd = listenfd;
        p->efd = epoll_create1(0);
        if (p->efd < 0) {
            printf ("epoll_create error!\n");
            exit(1);
        }
        event.data.ptr = p;
        event.data.fd = listenfd;
        event.events = EPOLLIN;
        XEPOLL_CTL(p->efd, EPOLL_CTL_ADD, listenfd, &event);
}

/*******************************************************************************
 * Read Messages.
 ******************************************************************************/

/* 
 * Requires:
 * p should be a connection pool and not be NULL.
 *
 * Effects:
 * Reads from each ready file descriptor in the read set and handles the
 * incoming messages appropriately.
 */
static void
read_message(struct conn *c, struct conn_pool *p)
{
    int n;

    /* Read from that socket. */
    n = recv(c->fd, read_buf, READ_BUF_SIZE, 0);

    /* Data read. */
    if (n > 0) {

        c->size += n;
        if (verbose) {
            printf("Finished reading %d bytes from fd %d:\n", (int)c->size, c->fd);
        }
    }
    /* Error (possibly). */
    else if (n < 0) {
         /* If errno is EAGAIN, it just means we need to read again. */
        if (errno != EAGAIN)  {
        printf("close connection by error\n");
            remove_client(c, p);
    }
    }
    /* Connection closed by client. */
    else
        remove_client(c, p);
}

int main(int argc, char **argv) 
{
    int listenfd, port, i;
    struct conn_pool pool; 
    struct conn *connp;

    if (verbose)
            printf("Starting partition sender...\n");

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }
    port = atoi(argv[1]);

    listenfd = open_listen(port);
    if (listenfd < 0) {
        unix_error("open_listen error");
    }

    /* Initialize the connection pool. */
    init_pool(listenfd, &pool);

    if (verbose)
        printf("Listening for new connections at port %d...\n", port);

    while (1) {
        /* 
         * Wait until:
         * 1. New connection is requested.
         * 2. Data is available to be read from a socket. 
         */
        pool.nevents = epoll_wait (pool.efd, pool.events, MAXEVENTS, -1);
        for (i = 0; i < pool.nevents; i++) {
            if ((pool.events[i].events & EPOLLERR) ||
                (pool.events[i].events & EPOLLHUP)) {
                    /* An error has occured on this fd */
                fprintf (stderr, "epoll error\n");
                close (pool.events[i].data.fd);
                continue;
            }

            /* Handle Reads. */
            if (pool.events[i].events & EPOLLIN) {
                /* Check for new connection requests. */
                if (pool.events[i].data.fd == listenfd) {
                    handle_new_connection(listenfd, &pool);
                /* Check for sockets with new data to be read. */
                } else {
                    connp = (struct conn *) pool.events[i].data.ptr;
                    read_message(connp, &pool);
                }
            }
        }
    }
}
