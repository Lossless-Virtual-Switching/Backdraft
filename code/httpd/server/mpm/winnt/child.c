/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifdef WIN32

#include "apr.h"
#include <process.h>
#include "httpd.h"
#include "http_main.h"
#include "http_log.h"
#include "http_config.h"  /* for read_config */
#include "http_core.h"    /* for get_remote_host */
#include "http_connection.h"
#include "http_vhost.h"   /* for ap_update_vhost_given_ip */
#include "apr_portable.h"
#include "apr_thread_proc.h"
#include "apr_getopt.h"
#include "apr_strings.h"
#include "apr_lib.h"
#include "apr_shm.h"
#include "apr_thread_mutex.h"
#include "ap_mpm.h"
#include "ap_config.h"
#include "ap_listen.h"
#include "mpm_default.h"
#include "mpm_winnt.h"
#include "mpm_common.h"
#include <malloc.h>
#include "apr_atomic.h"
#include "apr_buckets.h"
#include "scoreboard.h"

#ifdef __MINGW32__
#include <mswsock.h>

#ifndef WSAID_ACCEPTEX
#define WSAID_ACCEPTEX \
  {0xb5367df1, 0xcbac, 0x11cf, {0x95, 0xca, 0x00, 0x80, 0x5f, 0x48, 0xa1, 0x92}}
typedef BOOL (WINAPI *LPFN_ACCEPTEX)(SOCKET, SOCKET, PVOID, DWORD, DWORD, DWORD, LPDWORD, LPOVERLAPPED);
#endif /* WSAID_ACCEPTEX */

#ifndef WSAID_GETACCEPTEXSOCKADDRS
#define WSAID_GETACCEPTEXSOCKADDRS \
  {0xb5367df2, 0xcbac, 0x11cf, {0x95, 0xca, 0x00, 0x80, 0x5f, 0x48, 0xa1, 0x92}}
typedef VOID (WINAPI *LPFN_GETACCEPTEXSOCKADDRS)(PVOID, DWORD, DWORD, DWORD,
                                                 struct sockaddr **, LPINT,
                                                 struct sockaddr **, LPINT);
#endif /* WSAID_GETACCEPTEXSOCKADDRS */

#endif /* __MINGW32__ */

#if APR_HAVE_IPV6
#define PADDED_ADDR_SIZE (sizeof(SOCKADDR_IN6)+16)
#else
#define PADDED_ADDR_SIZE (sizeof(SOCKADDR_IN)+16)
#endif

APLOG_USE_MODULE(mpm_winnt);

/*
 * Pool for managing the passing of winnt_conn_ctx_t between the accept
 * and worker threads (see ctxpool_head below). Note that the actual order
 * of how the contexts are processed is a LIFO stack, not a FIFO queue,
 * as LIFO order may significantly reduce the memory usage.
 *
 * Every completion context in the pool has an associated allocator, and
 * every allocator has its ap_max_mem_free memory limit which is not given
 * back to the OS. Once the pool grows, it cannot shrink back, and every
 * allocator in each of the pooled completion contexts keeps up to its
 * max_free amount of memory. The pool can only grow when a server has
 * to serve multiple concurrent connections at once.
 *
 * Consider a server that doesn't see many concurrent connections most
 * of the time, but has occasional spikes when it has to deal with
 * concurrency. During such spikes, the size of the pool grows. The
 * difference between LIFO and FIFO shows up after such spikes, when the
 * server is back to light load.
 *
 * With FIFO order, every completion context in the pool will be used in
 * a round-robin manner, thus using *every* available allocator one by one
 * and claiming up to (N * ap_max_mem_free memory) from the OS.  With LIFO
 * order, only the completion contexts that are close to the top of the
 * stack will be used and reused for subsequent connections.  Hence, only
 * a small part of the allocators will be used, and this can prevent all
 * other allocators from unnecessarily acquiring memory from the OS (and
 * keeping it).
 */
typedef struct winnt_conn_ctx_t_s {
    struct winnt_conn_ctx_t_s *next;
    OVERLAPPED overlapped;
    apr_socket_t *sock;
    SOCKET accept_socket;
    char buff[2*PADDED_ADDR_SIZE];
    struct sockaddr *sa_server;
    int sa_server_len;
    struct sockaddr *sa_client;
    int sa_client_len;
    apr_pool_t *ptrans;
    apr_bucket_alloc_t *ba;
    apr_bucket *data;
#if APR_HAVE_IPV6
    short socket_family;
#endif
} winnt_conn_ctx_t;

typedef enum {
    IOCP_CONNECTION_ACCEPTED = 1,
    IOCP_SHUTDOWN = 2
} io_state_e;

static apr_pool_t *pchild;
static HANDLE listener_shutdown_event;
static int workers_may_exit = 0;
static HANDLE max_requests_per_child_event;

static apr_thread_mutex_t  *child_lock;
static apr_thread_mutex_t  *ctxpool_lock;
static winnt_conn_ctx_t *ctxpool_head = NULL;
static apr_uint32_t num_completion_contexts = 0;
static apr_uint32_t max_num_completion_contexts = 0;
static HANDLE ThreadDispatchIOCP = NULL;
static HANDLE ctxpool_wait_event = NULL;

static void mpm_recycle_completion_context(winnt_conn_ctx_t *context)
{
    /* Recycle the completion context.
     * - clear the ptrans pool
     * - put the context on the ctxpool to be consumed by the accept thread
     * Note:
     * context->accept_socket may be in a disconnected but reusable
     * state so -don't- close it.
     */
    if (context) {
        HANDLE saved_event;

        apr_pool_clear(context->ptrans);
        context->ba = apr_bucket_alloc_create(context->ptrans);
        context->next = NULL;

        saved_event = context->overlapped.hEvent;
        memset(&context->overlapped, 0, sizeof(context->overlapped));
        context->overlapped.hEvent = saved_event;
        ResetEvent(context->overlapped.hEvent);

        apr_thread_mutex_lock(ctxpool_lock);
        if (!ctxpool_head) {
            SetEvent(ctxpool_wait_event);
        }
        context->next = ctxpool_head;
        ctxpool_head = context;
        apr_thread_mutex_unlock(ctxpool_lock);
    }
}

static apr_status_t mpm_get_completion_context(winnt_conn_ctx_t **context_p)
{
    apr_status_t status;
    winnt_conn_ctx_t *context = NULL;

    *context_p = NULL;
    while (1) {
        /* Do we have an available context in the pool? */
        apr_thread_mutex_lock(ctxpool_lock);
        if (ctxpool_head) {
            context = ctxpool_head;
            ctxpool_head = ctxpool_head->next;
        } else {
            ResetEvent(ctxpool_wait_event);
        }
        apr_thread_mutex_unlock(ctxpool_lock);

        if (!context) {
            /* We failed to grab a context from the pool, consider allocating
             * a new one. There may be up to (ap_threads_per_child + num_listeners)
             * contexts in the system at once.
             */
            if (num_completion_contexts >= max_num_completion_contexts) {
                DWORD rv;
                HANDLE events[2];
                /* All workers are busy, need to wait for one */
                static int reported = 0;
                if (!reported) {
                    ap_log_error(APLOG_MARK, APLOG_ERR, 0, ap_server_conf, APLOGNO(00326)
                                 "Server ran out of threads to serve "
                                 "requests. Consider raising the "
                                 "ThreadsPerChild setting");
                    reported = 1;
                }

                /* Wait for a worker to free a context. Once per second, give
                 * the caller a chance to check for shutdown. If the wait
                 * succeeds, get the context off the context pool. It must be
                 * available, since there's only one consumer.
                 */
                events[0] = ctxpool_wait_event;
                events[1] = listener_shutdown_event;
                rv = WaitForMultipleObjects(2, events, FALSE, 1000);
                if (rv == WAIT_OBJECT_0) {
                    continue;
                }
                else if (rv == WAIT_OBJECT_0 + 1) {
                    /* Got the exit event */
                    return APR_SUCCESS;
                }
                else if (rv == WAIT_TIMEOUT) {
                    /* Workers are busy, write a diagnostic message and retry */
                    ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, ap_server_conf, APLOGNO(00327)
                                 "mpm_get_completion_context: Failed to get a "
                                 "free context within 1 second");
                    continue;
                }
                else {
                    /* should be the unexpected, generic WAIT_FAILED */
                    status = APR_FROM_OS_ERROR(rv);
                    ap_log_error(APLOG_MARK, APLOG_WARNING, status,
                                 ap_server_conf, APLOGNO(00328)
                                 "mpm_get_completion_context: "
                                 "WaitForSingleObject failed to get free context");
                    return status;
                }
            } else {
                /* Allocate another context.
                 * Note: Multiple failures in the next two steps will cause
                 * the pchild pool to 'leak' storage. I don't think this
                 * is worth fixing...
                 */
                apr_allocator_t *allocator;

                apr_thread_mutex_lock(child_lock);
                context = (winnt_conn_ctx_t *)apr_pcalloc(pchild,
                                                     sizeof(winnt_conn_ctx_t));


                context->overlapped.hEvent = CreateEvent(NULL, TRUE,
                                                         FALSE, NULL);
                if (context->overlapped.hEvent == NULL) {
                    /* Hopefully this is a temporary condition ... */
                    status = apr_get_os_error();
                    ap_log_error(APLOG_MARK, APLOG_WARNING, status,
                                 ap_server_conf, APLOGNO(00329)
                                 "mpm_get_completion_context: "
                                 "CreateEvent failed.");

                    apr_thread_mutex_unlock(child_lock);
                    return status;
                }

                /* Create the transaction pool */
                apr_allocator_create(&allocator);
                apr_allocator_max_free_set(allocator, ap_max_mem_free);
                status = apr_pool_create_ex(&context->ptrans, pchild, NULL,
                                            allocator);
                if (status != APR_SUCCESS) {
                    ap_log_error(APLOG_MARK, APLOG_WARNING, status, ap_server_conf, APLOGNO(00330)
                                 "mpm_get_completion_context: Failed "
                                 "to create the transaction pool.");
                    CloseHandle(context->overlapped.hEvent);

                    apr_thread_mutex_unlock(child_lock);
                    return status;
                }
                apr_allocator_owner_set(allocator, context->ptrans);
                apr_pool_tag(context->ptrans, "transaction");

                context->accept_socket = INVALID_SOCKET;
                context->ba = apr_bucket_alloc_create(context->ptrans);
                apr_atomic_inc32(&num_completion_contexts);

                apr_thread_mutex_unlock(child_lock);
                break;
            }
        } else {
            /* Got a context from the context pool */
            break;
        }
    }

    *context_p = context;
    return APR_SUCCESS;
}

typedef enum {
    ACCEPT_FILTER_NONE = 0,
    ACCEPT_FILTER_CONNECT = 1
} accept_filter_e;

static const char * accept_filter_to_string(accept_filter_e accf)
{
    switch (accf) {
    case ACCEPT_FILTER_NONE:
        return "none";
    case ACCEPT_FILTER_CONNECT:
        return "connect";
    default:
        return "";
    }
}

static accept_filter_e get_accept_filter(const char *protocol)
{
    core_server_config *core_sconf;
    const char *name;

    core_sconf = ap_get_core_module_config(ap_server_conf->module_config);
    name = apr_table_get(core_sconf->accf_map, protocol);
    if (!name) {
        ap_log_error(APLOG_MARK, APLOG_WARNING, 0, ap_server_conf,
                     APLOGNO(02531) "winnt_accept: Listen protocol '%s' has "
                     "no known accept filter. Using 'none' instead",
                     protocol);
        return ACCEPT_FILTER_NONE;
    }
    else if (strcmp(name, "data") == 0) {
        ap_log_error(APLOG_MARK, APLOG_INFO, 0, ap_server_conf,
                     APLOGNO(03458) "winnt_accept: 'data' accept filter is no "
                     "longer supported. Using 'connect' instead");
        return ACCEPT_FILTER_CONNECT;
    }
    else if (strcmp(name, "connect") == 0) {
        return ACCEPT_FILTER_CONNECT;
    }
    else if (strcmp(name, "none") == 0) {
        return ACCEPT_FILTER_NONE;
    }
    else {
        ap_log_error(APLOG_MARK, APLOG_WARNING, 0, ap_server_conf, APLOGNO(00331)
                     "winnt_accept: unrecognized AcceptFilter '%s', "
                     "only 'data', 'connect' or 'none' are valid. "
                     "Using 'none' instead", name);
        return ACCEPT_FILTER_NONE;
    }
}

/* Windows NT/2000 specific code...
 * Accept processing for on Windows NT uses a producer/consumer queue
 * model. An accept thread accepts connections off the network then issues
 * PostQueuedCompletionStatus() to awake a thread blocked on the ThreadDispatch
 * IOCompletionPort.
 *
 * winnt_accept()
 *    One or more accept threads run in this function, each of which accepts
 *    connections off the network and calls PostQueuedCompletionStatus() to
 *    queue an io completion packet to the ThreadDispatch IOCompletionPort.
 * winnt_get_connection()
 *    Worker threads block on the ThreadDispatch IOCompletionPort awaiting
 *    connections to service.
 */
#define MAX_ACCEPTEX_ERR_COUNT 10

static unsigned int __stdcall winnt_accept(void *lr_)
{
    ap_listen_rec *lr = (ap_listen_rec *)lr_;
    apr_os_sock_info_t sockinfo;
    winnt_conn_ctx_t *context = NULL;
    DWORD BytesRead = 0;
    SOCKET nlsd;
    LPFN_ACCEPTEX lpfnAcceptEx = NULL;
    LPFN_GETACCEPTEXSOCKADDRS lpfnGetAcceptExSockaddrs = NULL;
    GUID GuidAcceptEx = WSAID_ACCEPTEX;
    GUID GuidGetAcceptExSockaddrs = WSAID_GETACCEPTEXSOCKADDRS;
    apr_status_t rv;
    accept_filter_e accf;
    int err_count = 0;
    HANDLE events[3];
#if APR_HAVE_IPV6
    SOCKADDR_STORAGE ss_listen;
    int namelen = sizeof(ss_listen);
#endif
    u_long zero = 0;

    apr_os_sock_get(&nlsd, lr->sd);

#if APR_HAVE_IPV6
    if (getsockname(nlsd, (struct sockaddr *)&ss_listen, &namelen) == SOCKET_ERROR) {
        ap_log_error(APLOG_MARK, APLOG_ERR, apr_get_netos_error(),
                     ap_server_conf, APLOGNO(00332)
                     "winnt_accept: getsockname error on listening socket, "
                     "is IPv6 available?");
        return 1;
   }
#endif

    accf = get_accept_filter(lr->protocol);
    if (accf == ACCEPT_FILTER_CONNECT)
    {
        if (WSAIoctl(nlsd, SIO_GET_EXTENSION_FUNCTION_POINTER,
                     &GuidAcceptEx, sizeof GuidAcceptEx, 
                     &lpfnAcceptEx, sizeof lpfnAcceptEx, 
                     &BytesRead, NULL, NULL) == SOCKET_ERROR) {
            ap_log_error(APLOG_MARK, APLOG_ERR, apr_get_netos_error(),
                         ap_server_conf, APLOGNO(02322)
                         "winnt_accept: failed to retrieve AcceptEx, try 'AcceptFilter none'");
            return 1;
        }
        if (WSAIoctl(nlsd, SIO_GET_EXTENSION_FUNCTION_POINTER,
                     &GuidGetAcceptExSockaddrs, sizeof GuidGetAcceptExSockaddrs,
                     &lpfnGetAcceptExSockaddrs, sizeof lpfnGetAcceptExSockaddrs,
                     &BytesRead, NULL, NULL) == SOCKET_ERROR) {
            ap_log_error(APLOG_MARK, APLOG_ERR, apr_get_netos_error(),
                         ap_server_conf, APLOGNO(02323)
                         "winnt_accept: failed to retrieve GetAcceptExSockaddrs, try 'AcceptFilter none'");
            return 1;
        }
        /* first, high priority event is an already accepted connection */
        events[1] = listener_shutdown_event;
        events[2] = max_requests_per_child_event;
    }
    else /* accf == ACCEPT_FILTER_NONE */
    {
reinit: /* target of connect upon too many AcceptEx failures */

        /* last, low priority event is a not yet accepted connection */
        events[0] = listener_shutdown_event;
        events[1] = max_requests_per_child_event;
        events[2] = CreateEvent(NULL, FALSE, FALSE, NULL);

        /* The event needs to be removed from the accepted socket,
         * if not removed from the listen socket prior to accept(),
         */
        rv = WSAEventSelect(nlsd, events[2], FD_ACCEPT);
        if (rv) {
            ap_log_error(APLOG_MARK, APLOG_ERR,
                         apr_get_netos_error(), ap_server_conf, APLOGNO(00333)
                         "WSAEventSelect() failed.");
            CloseHandle(events[2]);
            return 1;
        }
    }

    ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, ap_server_conf, APLOGNO(00334)
                 "Child: Accept thread listening on %pI using AcceptFilter %s",
                 lr->bind_addr, accept_filter_to_string(accf));

    while (1) {
        if (!context) {
            rv = mpm_get_completion_context(&context);
            if (rv) {
                /* We have an irrecoverable error, tell the child to die */
                SetEvent(exit_event);
                break;
            }
            else if (rv == APR_SUCCESS && !context) {
                /* Normal exit */
                break;
            }
        }

        if (accf == ACCEPT_FILTER_CONNECT)
        {
            char *buf;

            /* Create and initialize the accept socket */
#if APR_HAVE_IPV6
            if (context->accept_socket == INVALID_SOCKET) {
                context->accept_socket = socket(ss_listen.ss_family, SOCK_STREAM,
                                                IPPROTO_TCP);
                context->socket_family = ss_listen.ss_family;
            }
            else if (context->socket_family != ss_listen.ss_family) {
                closesocket(context->accept_socket);
                context->accept_socket = socket(ss_listen.ss_family, SOCK_STREAM,
                                                IPPROTO_TCP);
                context->socket_family = ss_listen.ss_family;
            }
#else
            if (context->accept_socket == INVALID_SOCKET)
                context->accept_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#endif

            if (context->accept_socket == INVALID_SOCKET) {
                ap_log_error(APLOG_MARK, APLOG_WARNING, apr_get_netos_error(),
                             ap_server_conf, APLOGNO(00336)
                             "winnt_accept: Failed to allocate an accept socket. "
                             "Temporary resource constraint? Try again.");
                Sleep(100);
                continue;
            }

            buf = context->buff;

            /* AcceptEx on the completion context. The completion context will be
             * signaled when a connection is accepted.
             */
            if (!lpfnAcceptEx(nlsd, context->accept_socket, buf, 0,
                              PADDED_ADDR_SIZE, PADDED_ADDR_SIZE, &BytesRead,
                              &context->overlapped)) {
                rv = apr_get_netos_error();
                if ((rv == APR_FROM_OS_ERROR(WSAECONNRESET)) ||
                    (rv == APR_FROM_OS_ERROR(WSAEACCES))) {
                    /* We can get here when:
                     * 1) the client disconnects early
                     * 2) handshake was incomplete
                     */
                    closesocket(context->accept_socket);
                    context->accept_socket = INVALID_SOCKET;
                    continue;
                }
                else if ((rv == APR_FROM_OS_ERROR(WSAEINVAL)) ||
                         (rv == APR_FROM_OS_ERROR(WSAENOTSOCK))) {
                    /* We can get here when:
                     * 1) TransmitFile does not properly recycle the accept socket (typically
                     *    because the client disconnected)
                     * 2) there is VPN or Firewall software installed with
                     *    buggy WSAAccept or WSADuplicateSocket implementation
                     * 3) the dynamic address / adapter has changed
                     * Give five chances, then fall back on AcceptFilter 'none'
                     */
                    closesocket(context->accept_socket);
                    context->accept_socket = INVALID_SOCKET;
                    ++err_count;
                    if (err_count > MAX_ACCEPTEX_ERR_COUNT) {
                        ap_log_error(APLOG_MARK, APLOG_ERR, rv, ap_server_conf, APLOGNO(00337)
                                     "Child: Encountered too many AcceptEx "
                                     "faults accepting client connections. "
                                     "Possible causes: dynamic address renewal, "
                                     "or incompatible VPN or firewall software. ");
                        ap_log_error(APLOG_MARK, APLOG_NOTICE, rv, ap_server_conf, APLOGNO(00338)
                                     "winnt_mpm: falling back to "
                                     "'AcceptFilter none'.");
                        err_count = 0;
                        accf = ACCEPT_FILTER_NONE;
                    }
                    continue;
                }
                else if ((rv != APR_FROM_OS_ERROR(ERROR_IO_PENDING)) &&
                         (rv != APR_FROM_OS_ERROR(WSA_IO_PENDING))) {
                    closesocket(context->accept_socket);
                    context->accept_socket = INVALID_SOCKET;
                    ++err_count;
                    if (err_count > MAX_ACCEPTEX_ERR_COUNT) {
                        ap_log_error(APLOG_MARK, APLOG_ERR, rv, ap_server_conf, APLOGNO(00339)
                                     "Child: Encountered too many AcceptEx "
                                     "faults accepting client connections.");
                        ap_log_error(APLOG_MARK, APLOG_NOTICE, rv, ap_server_conf, APLOGNO(00340)
                                     "winnt_mpm: falling back to "
                                     "'AcceptFilter none'.");
                        err_count = 0;
                        accf = ACCEPT_FILTER_NONE;
                        goto reinit;
                    }
                    continue;
                }

                err_count = 0;
                events[0] = context->overlapped.hEvent;

                do {
                    rv = WaitForMultipleObjectsEx(3, events, FALSE, INFINITE, TRUE);
                } while (rv == WAIT_IO_COMPLETION);

                if (rv == WAIT_OBJECT_0) {
                    if ((context->accept_socket != INVALID_SOCKET) &&
                        !GetOverlappedResult((HANDLE)context->accept_socket,
                                             &context->overlapped,
                                             &BytesRead, FALSE)) {
                        ap_log_error(APLOG_MARK, APLOG_WARNING,
                                     apr_get_os_error(), ap_server_conf, APLOGNO(00341)
                             "winnt_accept: Asynchronous AcceptEx failed.");
                        closesocket(context->accept_socket);
                        context->accept_socket = INVALID_SOCKET;
                    }
                }
                else {
                    /* listener_shutdown_event triggered or event handle was closed */
                    closesocket(context->accept_socket);
                    context->accept_socket = INVALID_SOCKET;
                    break;
                }

                if (context->accept_socket == INVALID_SOCKET) {
                    continue;
                }
            }
            err_count = 0;

            /* Potential optimization; consider handing off to the worker */

            /* Inherit the listen socket settings. Required for
             * shutdown() to work
             */
            if (setsockopt(context->accept_socket, SOL_SOCKET,
                           SO_UPDATE_ACCEPT_CONTEXT, (char *)&nlsd,
                           sizeof(nlsd))) {
                ap_log_error(APLOG_MARK, APLOG_WARNING, apr_get_netos_error(),
                             ap_server_conf, APLOGNO(00342)
                             "setsockopt(SO_UPDATE_ACCEPT_CONTEXT) failed.");
                /* Not a failure condition. Keep running. */
            }

            /* Get the local & remote address
             * TODO; error check
             */
            lpfnGetAcceptExSockaddrs(buf, 0, PADDED_ADDR_SIZE, PADDED_ADDR_SIZE,
                                     &context->sa_server, &context->sa_server_len,
                                     &context->sa_client, &context->sa_client_len);
        }
        else /* accf == ACCEPT_FILTER_NONE */
        {
            /* There is no socket reuse without AcceptEx() */
            if (context->accept_socket != INVALID_SOCKET)
                closesocket(context->accept_socket);

            /* This could be a persistent event per-listener rather than
             * per-accept.  However, the event needs to be removed from
             * the target socket if not removed from the listen socket
             * prior to accept(), or the event select is inherited.
             * and must be removed from the accepted socket.
             */

            do {
                rv = WaitForMultipleObjectsEx(3, events, FALSE, INFINITE, TRUE);
            } while (rv == WAIT_IO_COMPLETION);


            if (rv != WAIT_OBJECT_0 + 2) {
                /* not FD_ACCEPT;
                 * listener_shutdown_event triggered or event handle was closed
                 */
                break;
            }

            context->sa_server = (void *) context->buff;
            context->sa_server_len = sizeof(context->buff) / 2;
            context->sa_client_len = context->sa_server_len;
            context->sa_client = (void *) (context->buff
                                         + context->sa_server_len);

            context->accept_socket = accept(nlsd, context->sa_server,
                                            &context->sa_server_len);

            if (context->accept_socket == INVALID_SOCKET) {

                rv = apr_get_netos_error();
                if (   rv == APR_FROM_OS_ERROR(WSAECONNRESET)
                    || rv == APR_FROM_OS_ERROR(WSAEINPROGRESS)
                    || rv == APR_FROM_OS_ERROR(WSAEWOULDBLOCK) ) {
                    ap_log_error(APLOG_MARK, APLOG_DEBUG,
                                 rv, ap_server_conf, APLOGNO(00343)
                                 "accept() failed, retrying.");
                    continue;
                }

                /* A more serious error than 'retry', log it */
                ap_log_error(APLOG_MARK, APLOG_WARNING,
                             rv, ap_server_conf, APLOGNO(00344)
                             "accept() failed.");

                if (   rv == APR_FROM_OS_ERROR(WSAEMFILE)
                    || rv == APR_FROM_OS_ERROR(WSAENOBUFS) ) {
                    /* Hopefully a temporary condition in the provider? */
                    Sleep(100);
                    ++err_count;
                    if (err_count > MAX_ACCEPTEX_ERR_COUNT) {
                        ap_log_error(APLOG_MARK, APLOG_ERR, rv, ap_server_conf, APLOGNO(00345)
                                     "Child: Encountered too many accept() "
                                     "resource faults, aborting.");
                        /* We have an irrecoverable error, tell the child to die */
                        SetEvent(exit_event);
                        break;
                    }
                    continue;
                }

                /* We have an irrecoverable error, tell the child to die */
                SetEvent(exit_event);
                break;
            }
            /* Per MSDN, cancel the inherited association of this socket
             * to the WSAEventSelect API, and restore the state corresponding
             * to apr_os_sock_make's default assumptions (really, a flaw within
             * os_sock_make and os_sock_put that it does not query).
             */
            WSAEventSelect(context->accept_socket, 0, 0);
            err_count = 0;

            context->sa_server_len = sizeof(context->buff) / 2;
            if (getsockname(context->accept_socket, context->sa_server,
                            &context->sa_server_len) == SOCKET_ERROR) {
                ap_log_error(APLOG_MARK, APLOG_WARNING, apr_get_netos_error(), ap_server_conf, APLOGNO(00346)
                             "getsockname failed");
                continue;
            }
            if ((getpeername(context->accept_socket, context->sa_client,
                             &context->sa_client_len)) == SOCKET_ERROR) {
                ap_log_error(APLOG_MARK, APLOG_WARNING, apr_get_netos_error(), ap_server_conf, APLOGNO(00347)
                             "getpeername failed");
                memset(&context->sa_client, '\0', sizeof(context->sa_client));
            }
        }

        sockinfo.os_sock  = &context->accept_socket;
        sockinfo.local    = context->sa_server;
        sockinfo.remote   = context->sa_client;
        sockinfo.family   = context->sa_server->sa_family;
        sockinfo.type     = SOCK_STREAM;
        sockinfo.protocol = IPPROTO_TCP;
        /* Restore the state corresponding to apr_os_sock_make's default
         * assumption of timeout -1 (really, a flaw of os_sock_make and
         * os_sock_put that it does not query to determine ->timeout).
         * XXX: Upon a fix to APR, these three statements should disappear.
         */
        ioctlsocket(context->accept_socket, FIONBIO, &zero);
        setsockopt(context->accept_socket, SOL_SOCKET, SO_RCVTIMEO,
                   (char *) &zero, sizeof(zero));
        setsockopt(context->accept_socket, SOL_SOCKET, SO_SNDTIMEO,
                   (char *) &zero, sizeof(zero));
        apr_os_sock_make(&context->sock, &sockinfo, context->ptrans);

        /* When a connection is received, send an io completion notification
         * to the ThreadDispatchIOCP.
         */
        PostQueuedCompletionStatus(ThreadDispatchIOCP, BytesRead,
                                   IOCP_CONNECTION_ACCEPTED,
                                   &context->overlapped);
        context = NULL;
    }
    if (accf == ACCEPT_FILTER_NONE)
        CloseHandle(events[2]);

    ap_log_error(APLOG_MARK, APLOG_DEBUG, APR_SUCCESS, ap_server_conf, APLOGNO(00348)
                 "Child: Accept thread exiting.");
    return 0;
}


static winnt_conn_ctx_t *winnt_get_connection(winnt_conn_ctx_t *context)
{
    int rc;
    DWORD BytesRead;
    LPOVERLAPPED pol;
#ifdef _WIN64
    ULONG_PTR CompKey;
#else
    DWORD CompKey;
#endif

    mpm_recycle_completion_context(context);

    while (1) {
        if (workers_may_exit) {
            return NULL;
        }
        rc = GetQueuedCompletionStatus(ThreadDispatchIOCP, &BytesRead,
                                       &CompKey, &pol, INFINITE);
        if (!rc) {
            rc = apr_get_os_error();
            ap_log_error(APLOG_MARK, APLOG_DEBUG, rc, ap_server_conf, APLOGNO(00349)
                         "Child: GetQueuedCompletionStatus returned %d",
                         rc);
            continue;
        }

        switch (CompKey) {
        case IOCP_CONNECTION_ACCEPTED:
            context = CONTAINING_RECORD(pol, winnt_conn_ctx_t, overlapped);
            break;
        case IOCP_SHUTDOWN:
            return NULL;
        default:
            return NULL;
        }
        break;
    }

    return context;
}

/*
 * worker_main()
 * Main entry point for the worker threads. Worker threads block in
 * win*_get_connection() awaiting a connection to service.
 */
static DWORD __stdcall worker_main(void *thread_num_val)
{
    apr_thread_t *thd;
    apr_os_thread_t osthd;
    static int requests_this_child = 0;
    winnt_conn_ctx_t *context = NULL;
    int thread_num = (int)thread_num_val;
    ap_sb_handle_t *sbh;
    conn_rec *c;
    apr_int32_t disconnected;

    osthd = apr_os_thread_current();

    while (1) {

        ap_update_child_status_from_indexes(0, thread_num, SERVER_READY, NULL);

        /* Grab a connection off the network */
        context = winnt_get_connection(context);

        if (!context) {
            /* Time for the thread to exit */
            break;
        }

        /* Have we hit MaxConnectionsPerChild connections? */
        if (ap_max_requests_per_child) {
            requests_this_child++;
            if (requests_this_child > ap_max_requests_per_child) {
                SetEvent(max_requests_per_child_event);
            }
        }

        ap_create_sb_handle(&sbh, context->ptrans, 0, thread_num);
        c = ap_run_create_connection(context->ptrans, ap_server_conf,
                                     context->sock, thread_num, sbh,
                                     context->ba);

        if (!c) {
            /* ap_run_create_connection closes the socket on failure */
            context->accept_socket = INVALID_SOCKET;
            continue;
        }

        thd = NULL;
        apr_os_thread_put(&thd, &osthd, context->ptrans);
        c->current_thread = thd;

        ap_process_connection(c, context->sock);

        ap_lingering_close(c);

        apr_socket_opt_get(context->sock, APR_SO_DISCONNECTED, &disconnected);
        if (!disconnected) {
            context->accept_socket = INVALID_SOCKET;
        }
    }

    ap_update_child_status_from_indexes(0, thread_num, SERVER_DEAD, NULL);

    return 0;
}


/*
 * child_main()
 * Entry point for the main control thread for the child process.
 * This thread creates the accept thread, worker threads and
 * monitors the child process for maintenance and shutdown
 * events.
 */
static void create_listener_thread(void)
{
    unsigned tid;
    int num_listeners = 0;
    /* Start an accept thread per listener
     * XXX: Why would we have a NULL sd in our listeners?
     */
    ap_listen_rec *lr;

    /* Number of completion_contexts allowed in the system is
     * (ap_threads_per_child + num_listeners). We need the additional
     * completion contexts to prevent server hangs when ThreadsPerChild
     * is configured to something less than or equal to the number
     * of listeners. This is not a usual case, but people have
     * encountered it.
     */
    for (lr = ap_listeners; lr ; lr = lr->next) {
        num_listeners++;
    }
    max_num_completion_contexts = ap_threads_per_child + num_listeners;

    /* Now start a thread per listener */
    for (lr = ap_listeners; lr; lr = lr->next) {
        if (lr->sd != NULL) {
            /* A smaller stack is sufficient.
             * To convert to CreateThread, the returned handle cannot be
             * ignored, it must be closed/joined.
             */
            _beginthreadex(NULL, 65536, winnt_accept,
                           (void *) lr, stack_res_flag, &tid);
        }
    }
}


void child_main(apr_pool_t *pconf, DWORD parent_pid)
{
    apr_status_t status;
    apr_hash_t *ht;
    ap_listen_rec *lr;
    HANDLE child_events[3];
    HANDLE *child_handles;
    int listener_started = 0;
    int threads_created = 0;
    int time_remains;
    int cld;
    DWORD tid;
    int rv;
    int i;
    int num_events;

    /* Get a sub context for global allocations in this child, so that
     * we can have cleanups occur when the child exits.
     */
    apr_pool_create(&pchild, pconf);
    apr_pool_tag(pchild, "pchild");

    ap_run_child_init(pchild, ap_server_conf);
    ht = apr_hash_make(pchild);

    listener_shutdown_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!listener_shutdown_event) {
        ap_log_error(APLOG_MARK, APLOG_CRIT, apr_get_os_error(), ap_server_conf, APLOGNO(10035)
                     "Child: Failed to create a listener_shutdown event.");
        exit(APEXIT_CHILDINIT);
    }

    /* Initialize the child_events */
    max_requests_per_child_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!max_requests_per_child_event) {
        ap_log_error(APLOG_MARK, APLOG_CRIT, apr_get_os_error(), ap_server_conf, APLOGNO(00350)
                     "Child: Failed to create a max_requests event.");
        exit(APEXIT_CHILDINIT);
    }
    child_events[0] = exit_event;
    child_events[1] = max_requests_per_child_event;

    if (parent_pid != my_pid) {
        child_events[2] = OpenProcess(SYNCHRONIZE, FALSE, parent_pid);
        if (child_events[2] == NULL) {
            num_events = 2;
            ap_log_error(APLOG_MARK, APLOG_ERR, apr_get_os_error(), ap_server_conf, APLOGNO(02643)
                         "Child: Failed to open handle to parent process %ld; "
                         "will not react to abrupt parent termination", parent_pid);
        }
        else {
            num_events = 3;
        }
    }
    else {
        /* presumably -DONE_PROCESS */
        child_events[2] = NULL;
        num_events = 2;
    }

    /*
     * Wait until we have permission to start accepting connections.
     * start_mutex is used to ensure that only one child ever
     * goes into the listen/accept loop at once.
     */
    status = apr_proc_mutex_lock(start_mutex);
    if (status != APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_ERR, status, ap_server_conf, APLOGNO(00351)
                     "Child: Failed to acquire the start_mutex. "
                     "Process will exit.");
        exit(APEXIT_CHILDINIT);
    }
    ap_log_error(APLOG_MARK, APLOG_DEBUG, APR_SUCCESS, ap_server_conf, APLOGNO(00352)
                 "Child: Acquired the start mutex.");

    /*
     * Create the worker thread dispatch IOCompletionPort
     */
    ThreadDispatchIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE,
                                                NULL, 0, 0);
    apr_thread_mutex_create(&ctxpool_lock, APR_THREAD_MUTEX_DEFAULT, pchild);
    ctxpool_wait_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!ctxpool_wait_event) {
        ap_log_error(APLOG_MARK, APLOG_CRIT, apr_get_os_error(),
                     ap_server_conf, APLOGNO(00353)
                     "Child: Failed to create a ctxpool_wait event.");
        exit(APEXIT_CHILDINIT);
    }

    /*
     * Create the pool of worker threads
     */
    ap_log_error(APLOG_MARK, APLOG_NOTICE, APR_SUCCESS, ap_server_conf, APLOGNO(00354)
                 "Child: Starting %d worker threads.", ap_threads_per_child);
    child_handles = (HANDLE) apr_pcalloc(pchild, ap_threads_per_child
                                                  * sizeof(HANDLE));
    apr_thread_mutex_create(&child_lock, APR_THREAD_MUTEX_DEFAULT, pchild);

    while (1) {
        int from_previous_generation = 0, starting_up = 0;

        for (i = 0; i < ap_threads_per_child; i++) {
            int *score_idx;
            int status = ap_scoreboard_image->servers[0][i].status;
            if (status != SERVER_GRACEFUL && status != SERVER_DEAD) {
                if (ap_scoreboard_image->servers[0][i].generation != my_generation) {
                    ++from_previous_generation;
                }
                else if (status == SERVER_STARTING) {
                    ++starting_up;
                }
                continue;
            }
            ap_update_child_status_from_indexes(0, i, SERVER_STARTING, NULL);

            child_handles[i] = CreateThread(NULL, ap_thread_stacksize,
                                            worker_main, (void *) i,
                                            stack_res_flag, &tid);
            if (child_handles[i] == 0) {
                ap_log_error(APLOG_MARK, APLOG_CRIT, apr_get_os_error(),
                             ap_server_conf, APLOGNO(00355)
                             "Child: CreateThread failed. Unable to "
                             "create all worker threads. Created %d of the %d "
                             "threads requested with the ThreadsPerChild "
                             "configuration directive.",
                             threads_created, ap_threads_per_child);
                ap_signal_parent(SIGNAL_PARENT_SHUTDOWN);
                goto shutdown;
            }
            ap_scoreboard_image->servers[0][i].pid = my_pid;
            ap_scoreboard_image->servers[0][i].generation = my_generation;
            threads_created++;
            /* Save the score board index in ht keyed to the thread handle.
             * We need this when cleaning up threads down below...
             */
            apr_thread_mutex_lock(child_lock);
            score_idx = apr_pcalloc(pchild, sizeof(int));
            *score_idx = i;
            apr_hash_set(ht, &child_handles[i], sizeof(HANDLE), score_idx);
            apr_thread_mutex_unlock(child_lock);
        }
        /* Start the listener only when workers are available */
        if (!listener_started && threads_created) {
            create_listener_thread();
            listener_started = 1;
            winnt_mpm_state = AP_MPMQ_RUNNING;
        }
        if (threads_created == ap_threads_per_child) {
            break;
        }
        /* Check to see if the child has been told to exit */
        if (WaitForSingleObject(exit_event, 0) != WAIT_TIMEOUT) {
            break;
        }
        /* wait for previous generation to clean up an entry in the scoreboard
         */
        ap_log_error(APLOG_MARK, APLOG_TRACE2, 0, ap_server_conf,
                     "Child: %d threads starting up, %d remain from a prior generation",
                     starting_up, from_previous_generation);
        apr_sleep(apr_time_from_sec(1));
    }

    /* Wait for one of these events:
     * exit_event:
     *    The exit_event is signaled by the parent process to notify
     *    the child that it is time to exit.
     *
     * max_requests_per_child_event:
     *    This event is signaled by the worker threads to indicate that
     *    the process has handled MaxConnectionsPerChild connections.
     *
     * parent process exiting
     *
     * TIMEOUT:
     *    To do periodic maintenance on the server (check for thread exits,
     *    number of completion contexts, etc.)
     *
     * XXX: thread exits *aren't* being checked.
     *
     * XXX: other_child - we need the process handles to the other children
     *      in order to map them to apr_proc_other_child_read (which is not
     *      named well, it's more like a_p_o_c_died.)
     *
     * XXX: however - if we get a_p_o_c handle inheritance working, and
     *      the parent process creates other children and passes the pipes
     *      to our worker processes, then we have no business doing such
     *      things in the child_main loop, but should happen in master_main.
     */
    while (1) {
#if !APR_HAS_OTHER_CHILD
        rv = WaitForMultipleObjects(num_events, (HANDLE *)child_events, FALSE, INFINITE);
        cld = rv - WAIT_OBJECT_0;
#else
        /* THIS IS THE EXPECTED BUILD VARIATION -- APR_HAS_OTHER_CHILD */
        rv = WaitForMultipleObjects(num_events, (HANDLE *)child_events, FALSE, 1000);
        cld = rv - WAIT_OBJECT_0;
        if (rv == WAIT_TIMEOUT) {
            apr_proc_other_child_refresh_all(APR_OC_REASON_RUNNING);
        }
        else
#endif
            if (rv == WAIT_FAILED) {
            /* Something serious is wrong */
            ap_log_error(APLOG_MARK, APLOG_CRIT, apr_get_os_error(),
                         ap_server_conf, APLOGNO(00356)
                         "Child: WAIT_FAILED -- shutting down server");
            /* check handle validity to identify a possible culprit */
            for (i = 0; i < num_events; i++) {
                DWORD out_flags;

                if (0 == GetHandleInformation(child_events[i], &out_flags)) {
                    ap_log_error(APLOG_MARK, APLOG_CRIT, apr_get_os_error(),
                                 ap_server_conf, APLOGNO(02644)
                                 "Child: Event handle #%d (%pp) is invalid",
                                 i, child_events[i]);
                }
            }
            break;
        }
        else if (cld == 0) {
            /* Exit event was signaled */
            ap_log_error(APLOG_MARK, APLOG_DEBUG, APR_SUCCESS, ap_server_conf, APLOGNO(00357)
                         "Child: Exit event signaled. Child process is "
                         "ending.");
            break;
        }
        else if (cld == 2) {
            /* The parent is dead.  Shutdown the child process. */
            ap_log_error(APLOG_MARK, APLOG_CRIT, 0, ap_server_conf, APLOGNO(02538)
                         "Child: Parent process exited abruptly. Child process "
                         "is ending");
            break;
        }
        else {
            /* MaxConnectionsPerChild event set by the worker threads.
             * Signal the parent to restart
             */
            ap_log_error(APLOG_MARK, APLOG_NOTICE, APR_SUCCESS, ap_server_conf, APLOGNO(00358)
                         "Child: Process exiting because it reached "
                         "MaxConnectionsPerChild. Signaling the parent to "
                         "restart a new child process.");
            ap_signal_parent(SIGNAL_PARENT_RESTART);
            break;
        }
    }

    /*
     * Time to shutdown the child process
     */

 shutdown:

    winnt_mpm_state = AP_MPMQ_STOPPING;

    /* Close the listening sockets. Note, we must close the listeners
     * before closing any accept sockets pending in AcceptEx to prevent
     * memory leaks in the kernel.
     */
    for (lr = ap_listeners; lr ; lr = lr->next) {
        apr_socket_close(lr->sd);
    }

    /* Shutdown listener threads and pending AcceptEx sockets
     * but allow the worker threads to continue consuming the
     * already accepted connections.
     */
    SetEvent(listener_shutdown_event);

    Sleep(1000);

    /* Tell the worker threads to exit */
    workers_may_exit = 1;

    /* Release the start_mutex to let the new process (in the restart
     * scenario) a chance to begin accepting and servicing requests
     */
    rv = apr_proc_mutex_unlock(start_mutex);
    if (rv == APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_DEBUG, rv, ap_server_conf, APLOGNO(00359)
                     "Child: Released the start mutex");
    }
    else {
        ap_log_error(APLOG_MARK, APLOG_ERR, rv, ap_server_conf, APLOGNO(00360)
                     "Child: Failure releasing the start mutex");
    }

    /* Shutdown the worker threads by posting a number of IOCP_SHUTDOWN
     * completion packets equal to the amount of created threads
     */
    for (i = 0; i < threads_created; i++) {
        PostQueuedCompletionStatus(ThreadDispatchIOCP, 0, IOCP_SHUTDOWN, NULL);
    }

    /* Empty the pool of completion contexts */
    apr_thread_mutex_lock(ctxpool_lock);
    while (ctxpool_head) {
        CloseHandle(ctxpool_head->overlapped.hEvent);
        closesocket(ctxpool_head->accept_socket);
        ctxpool_head = ctxpool_head->next;
    }
    apr_thread_mutex_unlock(ctxpool_lock);

    /* Give busy threads a chance to service their connections
     * (no more than the global server timeout period which
     * we track in msec remaining).
     */
    time_remains = (int)(ap_server_conf->timeout / APR_TIME_C(1000));

    while (threads_created)
    {
        HANDLE handle = child_handles[threads_created - 1];
        DWORD dwRet;

        if (time_remains < 0)
            break;
        /* Every 30 seconds give an update */
        if ((time_remains % 30000) == 0) {
            ap_log_error(APLOG_MARK, APLOG_NOTICE, APR_SUCCESS,
                         ap_server_conf, APLOGNO(00362)
                         "Child: Waiting %d more seconds "
                         "for %d worker threads to finish.",
                         time_remains / 1000, threads_created);
        }

        dwRet = WaitForSingleObject(handle, 100);
        time_remains -= 100;
        if (dwRet == WAIT_TIMEOUT) {
            /* Keep waiting */
        }
        else if (dwRet == WAIT_OBJECT_0) {
            CloseHandle(handle);
            threads_created--;
        }
        else {
            break;
        }
    }

    if (threads_created) {
        ap_log_error(APLOG_MARK, APLOG_NOTICE, APR_SUCCESS, ap_server_conf, APLOGNO(00363)
                     "Child: Waiting for %d threads timed out, terminating process.",
                     threads_created);
        for (i = 0; i < threads_created; i++) {
            /* Reset the scoreboard entries for the threads. */
            int *idx = apr_hash_get(ht, &child_handles[i], sizeof(HANDLE));
            if (idx) {
                ap_update_child_status_from_indexes(0, *idx, SERVER_DEAD, NULL);
            }
        }
        /* We can't wait for any longer, but still have some threads remaining.
         *
         * The only thing we can do is terminate the process and let the OS
         * perform the required cleanup. Terminate with exit code 0, as we do
         * not want the parent process to restart the child. Note that we use
         * TerminateProcess() instead of ExitProcess(), as the latter function
         * causes all DLLs to be unloaded, and it can potentially deadlock if
         * a DLL unload handler tries to acquire the same lock that is being
         * held by one of the remaining worker threads.
         *
         * See https://msdn.microsoft.com/en-us/library/windows/desktop/ms682658
         */
        TerminateProcess(GetCurrentProcess(), 0);
    }
    ap_log_error(APLOG_MARK, APLOG_NOTICE, APR_SUCCESS, ap_server_conf, APLOGNO(00364)
                 "Child: All worker threads have exited.");

    apr_thread_mutex_destroy(child_lock);
    apr_thread_mutex_destroy(ctxpool_lock);
    CloseHandle(ctxpool_wait_event);
    CloseHandle(ThreadDispatchIOCP);

    apr_pool_destroy(pchild);
    CloseHandle(exit_event);
    if (child_events[2] != NULL) {
        CloseHandle(child_events[2]);
    }
}

#endif /* def WIN32 */
