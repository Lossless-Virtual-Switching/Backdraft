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

#include "mod_serf.h"

#include "httpd.h"
#include "http_core.h"
#include "http_config.h"
#include "http_protocol.h"
#include "http_request.h"
#include "http_log.h"

#include "serf.h"
#include "apr_uri.h"
#include "apr_strings.h"
#include "apr_version.h"
#include "ap_mpm.h"

module AP_MODULE_DECLARE_DATA serf_module;
static int mpm_supprts_serf = 0;

typedef struct {
    int on;
    int preservehost;
    apr_uri_t url;
} serf_config_t;

typedef struct {
  const char *name;
  const char *provider;
  apr_table_t *params;
} serf_cluster_t;

typedef struct {
  /* name -> serf_cluster_t* */
  apr_hash_t *clusters;
} serf_server_config_t;

typedef struct {
    int rstatus;
    int want_ssl;
    int done_headers;
    int keep_reading;
    request_rec *r;
    apr_pool_t *serf_pool;
    apr_bucket_brigade *tmpbb;
    serf_config_t *conf;
    serf_ssl_context_t *ssl_ctx;
    serf_bucket_alloc_t *bkt_alloc;
    serf_bucket_t *body_bkt;
} s_baton_t;

#if !APR_VERSION_AT_LEAST(1,4,0)
#define apr_time_from_msec(x) (x * 1000)
#endif

/**
 * This works right now because all timers are invoked in the single listener
 * thread in the Event MPM -- the same thread that serf callbacks are made
 * from, so we don't technically need a mutex yet, but with the Simple MPM,
 * invocations are made from worker threads, and we need to figure out locking
 */
static void timed_cleanup_callback(void *baton)
{
    s_baton_t *ctx = baton;

    /* Causes all serf connections to unregister from the event mpm */
    if (ctx->rstatus) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, ctx->rstatus, ctx->r, APLOGNO(01119)
                      "serf: request returned: %d", ctx->rstatus);
        ctx->r->status = HTTP_OK;
        apr_pool_destroy(ctx->serf_pool);
        ap_die(ctx->rstatus, ctx->r);
    }
    else {
        apr_bucket *e;
        apr_brigade_cleanup(ctx->tmpbb);
        e = apr_bucket_flush_create(ctx->r->connection->bucket_alloc);
        APR_BRIGADE_INSERT_TAIL(ctx->tmpbb, e);
        e = apr_bucket_eos_create(ctx->r->connection->bucket_alloc);
        APR_BRIGADE_INSERT_TAIL(ctx->tmpbb, e);

        /* TODO: return code? bleh */
        ap_pass_brigade(ctx->r->output_filters, ctx->tmpbb);

        apr_pool_destroy(ctx->serf_pool);

        ap_finalize_request_protocol(ctx->r);
        ap_process_request_after_handler(ctx->r);
        return;
    }
}

static void closed_connection(serf_connection_t *conn,
                              void *closed_baton,
                              apr_status_t why,
                              apr_pool_t *pool)
{
    s_baton_t *ctx = closed_baton;

    if (why) {
        /* justin says that error handling isn't done yet. hah. */
        /* XXXXXX: review */
        ap_log_rerror(APLOG_MARK, APLOG_ERR, why, ctx->r, APLOGNO(01120) "Closed Connection Error");
        ctx->rstatus = HTTP_INTERNAL_SERVER_ERROR;
    }

    if (mpm_supprts_serf) {
        ap_mpm_register_timed_callback(apr_time_from_msec(1),
                                       timed_cleanup_callback, ctx);
    }
    ctx->keep_reading = 0;
}

static serf_bucket_t* conn_setup(apr_socket_t *sock,
                                 void *setup_baton,
                                 apr_pool_t *pool)
{
    serf_bucket_t *c;
    s_baton_t *ctx = setup_baton;

    c = serf_bucket_socket_create(sock, ctx->bkt_alloc);
    if (ctx->want_ssl) {
        c = serf_bucket_ssl_decrypt_create(c, ctx->ssl_ctx, ctx->bkt_alloc);
    }

    return c;
}

static int copy_headers_in(void *vbaton, const char *key, const char *value)
{
    serf_bucket_t *hdrs_bkt = (serf_bucket_t *)vbaton;

    /* XXXXX: List of headers not to copy to serf. serf's serf_bucket_headers_setn,
     * doesn't actually overwrite a header if we set it once, so we need to ignore anything
     * we might want to toggle or combine.
     */
    switch (key[0]) {
    case 'a':
    case 'A':
        if (ap_cstr_casecmp("Accept-Encoding", key) == 0) {
            return 0;
        }
        break;
    case 'c':
    case 'C':
        if (ap_cstr_casecmp("Connection", key) == 0) {
            return 0;
        }
        break;
    case 'h':
    case 'H':
        if (ap_cstr_casecmp("Host", key) == 0) {
            return 0;
        }
        break;
    case 'k':
    case 'K':
        if (ap_cstr_casecmp("Keep-Alive", key) == 0) {
            return 0;
        }
        break;
    case 't':
    case 'T':
        if (ap_cstr_casecmp("TE", key) == 0) {
            return 0;
        }
        if (ap_cstr_casecmp("Trailer", key) == 0) {
            return 0;
        }
        break;
    case 'u':
    case 'U':
        if (ap_cstr_casecmp("Upgrade", key) == 0) {
            return 0;
        }
        break;
    default:
        break;
    }

    serf_bucket_headers_setn(hdrs_bkt, key, value);
    return 0;
}

static int copy_headers_out(void *vbaton, const char *key, const char *value)
{
    s_baton_t *ctx = vbaton;
    int done = 0;

    /* XXXXX: Special Treatment required for MANY other headers. fixme.*/
    switch (key[0]) {
    case 'c':
    case 'C':
        if (ap_cstr_casecmp("Content-Type", key) == 0) {
            ap_set_content_type(ctx->r, value);
            done = 1;
            break;
        }
        else if (ap_cstr_casecmp("Connection", key) == 0) {
            done = 1;
            break;
        }
        else if (ap_cstr_casecmp("Content-Encoding", key) == 0) {
            done = 1;
            break;
        }
        else if (ap_cstr_casecmp("Content-Length", key) == 0) {
            done = 1;
            break;
        }
        break;
    case 't':
    case 'T':
        if (ap_cstr_casecmp("Transfer-Encoding", key) == 0) {
            done = 1;
            break;
        }
        break;
    default:
            break;
    }

    if (!done) {
        apr_table_addn(ctx->r->headers_out, key, value);
    }

    return 0;
}

static serf_bucket_t* accept_response(serf_request_t *request,
                                      serf_bucket_t *stream,
                                      void *acceptor_baton,
                                      apr_pool_t *pool)
{
    serf_bucket_t *c;
    serf_bucket_alloc_t *bkt_alloc;

    /* get the per-request bucket allocator */
    bkt_alloc = serf_request_get_alloc(request);

    /* Create a barrier so the response doesn't eat us! */
    c = serf_bucket_barrier_create(stream, bkt_alloc);

    return serf_bucket_response_create(c, bkt_alloc);
}

static apr_status_t handle_response(serf_request_t *request,
                                    serf_bucket_t *response,
                                    void *vbaton,
                                    apr_pool_t *pool)
{
    apr_status_t rv;
    s_baton_t *ctx = vbaton;
    const char *data;
    apr_size_t len;
    serf_status_line sl;

    if (response == NULL) {
        ctx->rstatus = HTTP_INTERNAL_SERVER_ERROR;
        return APR_EGENERAL;
    }

    /* XXXXXXX: Create better error message. */
    rv = serf_bucket_response_status(response, &sl);
    if (rv) {
        if (APR_STATUS_IS_EAGAIN(rv)) {
            return APR_SUCCESS;
        }

        ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, ctx->r, APLOGNO(01121) "serf_bucket_response_status...");

        ctx->rstatus = HTTP_INTERNAL_SERVER_ERROR;

        if (mpm_supprts_serf) {
            ap_mpm_register_timed_callback(apr_time_from_msec(1),
                                           timed_cleanup_callback, ctx);
        }

        return rv;
    }

    /**
     * XXXXX: If I understood serf buckets better, it might be possible to not
     * copy all of the data here, and better stream it to the client.
     **/

    do {
        apr_brigade_cleanup(ctx->tmpbb);
        rv = serf_bucket_read(response, AP_IOBUFSIZE, &data, &len);

        if (SERF_BUCKET_READ_ERROR(rv)) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, ctx->r, APLOGNO(01122) "serf_bucket_read(response)");
            return rv;
        }

        if (!ctx->done_headers) {
            serf_bucket_t *hdrs;
            serf_status_line line;

            /* TODO: improve */
            serf_bucket_response_status(response, &line);
            ctx->r->status = line.code;

            hdrs = serf_bucket_response_get_headers(response);
            serf_bucket_headers_do(hdrs, copy_headers_out, ctx);
            ctx->done_headers = 1;
        }


        if (len > 0) {
            /* TODO: make APR bucket <-> serf bucket stuff more magical. */
            apr_brigade_write(ctx->tmpbb, NULL, NULL, data, len);
        }

        if (APR_STATUS_IS_EOF(rv)) {
            ctx->keep_reading = 0;

            ctx->rstatus = ap_pass_brigade(ctx->r->output_filters, ctx->tmpbb);

            if (mpm_supprts_serf) {
                ap_mpm_register_timed_callback(apr_time_from_msec(1),
                                               timed_cleanup_callback, ctx);
            }
            return APR_EOF;
        }

        ctx->rstatus = ap_pass_brigade(ctx->r->output_filters, ctx->tmpbb);

        /* XXXX: Should we send a flush now? */
        if (APR_STATUS_IS_EAGAIN(rv)) {
            return APR_SUCCESS;
        }

    } while (1);
}


static apr_status_t setup_request(serf_request_t *request,
                                  void *vbaton,
                                  serf_bucket_t **req_bkt,
                                  serf_response_acceptor_t *acceptor,
                                  void **acceptor_baton,
                                  serf_response_handler_t *handler,
                                  void **handler_baton,
                                  apr_pool_t *pool)
{
    s_baton_t *ctx = vbaton;
    serf_bucket_t *hdrs_bkt;

    *req_bkt = serf_bucket_request_create(ctx->r->method, ctx->r->unparsed_uri,
                                          ctx->body_bkt,
                                          serf_request_get_alloc(request));

    hdrs_bkt = serf_bucket_request_get_headers(*req_bkt);

    apr_table_do(copy_headers_in, hdrs_bkt, ctx->r->headers_in, NULL);

    if (ctx->conf->preservehost) {
        serf_bucket_headers_setn(hdrs_bkt, "Host",
                                 apr_table_get(ctx->r->headers_in, "Host"));
    }
    else {
        serf_bucket_headers_setn(hdrs_bkt, "Host", ctx->conf->url.hostname);
    }

    serf_bucket_headers_setn(hdrs_bkt, "Accept-Encoding", "gzip");

    if (ctx->want_ssl) {
        if (ctx->ssl_ctx == NULL) {
            *req_bkt = serf_bucket_ssl_encrypt_create(*req_bkt, NULL,
                                           ctx->bkt_alloc);
            ctx->ssl_ctx = serf_bucket_ssl_encrypt_context_get(*req_bkt);
        }
        else {
            *req_bkt = serf_bucket_ssl_encrypt_create(*req_bkt, ctx->ssl_ctx,
                                                      ctx->bkt_alloc);
        }
    }

    *acceptor = accept_response;
    *acceptor_baton = ctx;
    *handler = handle_response;
    *handler_baton = ctx;

    return APR_SUCCESS;
}

/* TODO: rewrite drive_serf to make it async */
static int drive_serf(request_rec *r, serf_config_t *conf)
{
    apr_status_t rv = 0;
    apr_pool_t *pool;
    apr_sockaddr_t *address;
    s_baton_t *baton = apr_palloc(r->pool, sizeof(s_baton_t));
    /* XXXXX: make persistent/per-process or something.*/
    serf_context_t *serfme;
    serf_connection_t *conn;
    serf_server_config_t *ctx =
        (serf_server_config_t *)ap_get_module_config(r->server->module_config,
                                                     &serf_module);

    /* Allocate everything out of a subpool, with a shorter lifetime than
     * the main request, so that we can cleanup safely and remove our events
     * from the main serf context in the async mpm mode.
     */
    apr_pool_create(&pool, r->pool);
    apr_pool_tag(pool, "mod_serf_drive");
    if (strcmp(conf->url.scheme, "cluster") == 0) {
        int rc;
        ap_serf_cluster_provider_t *cp;
        serf_cluster_t *cluster;
        apr_array_header_t *servers = NULL;
        apr_uint32_t pick = 0;
        ap_serf_server_t *choice;

        /* TODO: could this be optimized in post-config to pre-setup the
         * pointers to the right cluster inside the conf structure?
         */
        cluster = apr_hash_get(ctx->clusters,
                               conf->url.hostname,
                               APR_HASH_KEY_STRING);
        if (!cluster) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r, APLOGNO(01123)
                          "SerfCluster: unable to find cluster %s", conf->url.hostname);
            return HTTP_INTERNAL_SERVER_ERROR;
        }

        cp = ap_lookup_provider(AP_SERF_CLUSTER_PROVIDER, cluster->provider, "0");

        if (cp == NULL) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r, APLOGNO(01124)
                          "SerfCluster: unable to find provider %s", cluster->provider);
            return HTTP_INTERNAL_SERVER_ERROR;
        }

        if (cp->list_servers == NULL) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r, APLOGNO(01125)
                          "SerfCluster: %s is missing list servers provider.", cluster->provider);
            return HTTP_INTERNAL_SERVER_ERROR;
        }

        rc = cp->list_servers(cp->baton,
                              r,
                              cluster->params,
                              &servers);

        if (rc != OK) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, rc, r, APLOGNO(01126)
                          "SerfCluster: %s list servers returned failure", cluster->provider);
            return HTTP_INTERNAL_SERVER_ERROR;
        }

        if (servers == NULL || apr_is_empty_array(servers)) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, rc, r, APLOGNO(01127)
                          "SerfCluster: %s failed to provide a list of servers", cluster->provider);
            return HTTP_INTERNAL_SERVER_ERROR;
        }

        /* TODO: restructure try all servers in the array !! */
        pick = ap_random_pick(0, servers->nelts-1);
        choice = APR_ARRAY_IDX(servers, pick, ap_serf_server_t *);

        rv = apr_sockaddr_info_get(&address, choice->ip,
                                   APR_UNSPEC, choice->port, 0,
                                   pool);
    }
    else {
        /* XXXXX: cache dns? */
        rv = apr_sockaddr_info_get(&address, conf->url.hostname,
                                   APR_UNSPEC, conf->url.port, 0,
                                   pool);
    }

    if (rv != APR_SUCCESS) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r, APLOGNO(01128) "Unable to resolve: %s", conf->url.hostname);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    if (mpm_supprts_serf) {
        serfme = ap_lookup_provider("mpm_serf", "instance", "0");
        if (!serfme) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r, APLOGNO(01129) "mpm lied to us about supporting serf.");
            return HTTP_INTERNAL_SERVER_ERROR;
        }
    }
    else {
        serfme = serf_context_create(pool);
    }

    baton->r = r;
    baton->conf = conf;
    baton->serf_pool = pool;
    baton->bkt_alloc = serf_bucket_allocator_create(pool, NULL, NULL);
    baton->body_bkt = NULL;
    baton->ssl_ctx = NULL;
    baton->rstatus = OK;

    baton->tmpbb = apr_brigade_create(r->pool, r->connection->bucket_alloc);
    baton->done_headers = 0;
    baton->keep_reading = 1;

    if (ap_cstr_casecmp(conf->url.scheme, "https") == 0) {
        baton->want_ssl = 1;
    }
    else {
        baton->want_ssl = 0;
    }

    rv = ap_setup_client_block(baton->r, REQUEST_CHUNKED_DECHUNK);
    if (rv) {
        return rv;
    }

    /* TODO: create custom serf bucket, which does async request body reads */
    if (ap_should_client_block(r)) {
        apr_size_t len;
        apr_off_t flen = 0;
        char buf[AP_IOBUFSIZE];
        apr_file_t *fp;

        rv = apr_file_mktemp(&fp, "mod_serf_buffer.XXXXXX", 0, pool);
        if (rv != APR_SUCCESS) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r, APLOGNO(01130) "Unable to create temp request body buffer file.");
            return HTTP_INTERNAL_SERVER_ERROR;
        }

        do {
            len = sizeof(buf);
            /* FIXME: ap_get_client_block() returns long, not apr_status_t */
            rv = ap_get_client_block(baton->r, buf, len);
            if (rv > 0) {
                rv = apr_file_write_full(fp, buf, rv, NULL);
                if (rv) {
                    ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r, APLOGNO(01131) "failed to read request body");
                    return HTTP_INTERNAL_SERVER_ERROR;
                }
            }
        } while(rv > 0);

        if (rv < 0) {
            ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r, APLOGNO(01132) "failed to read request body");
            return HTTP_INTERNAL_SERVER_ERROR;
        }

        apr_file_seek(fp, APR_SET, &flen);
        baton->body_bkt = serf_bucket_file_create(fp, baton->bkt_alloc);
    }

    conn = serf_connection_create(serfme, address,
                                  conn_setup, baton,
                                  closed_connection, baton,
                                  pool);

    /* XXX: Is it correct that we don't use the returned serf_request_t? */
    serf_connection_request_create(conn, setup_request, baton);

    if (mpm_supprts_serf) {
        return SUSPENDED;
    }
    else {
        do {
            rv = serf_context_run(serfme, SERF_DURATION_FOREVER, pool);

            /* XXXX: Handle timeouts */
            if (APR_STATUS_IS_TIMEUP(rv)) {
                continue;
            }

            if (rv != APR_SUCCESS) {
                ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r, APLOGNO(01133) "serf_context_run() for %pI", address);
                return HTTP_INTERNAL_SERVER_ERROR;
            }

            serf_debug__closed_conn(baton->bkt_alloc);
        } while (baton->keep_reading);

        return baton->rstatus;
    }
}

static int serf_handler(request_rec *r)
{
    serf_config_t *conf = ap_get_module_config(r->per_dir_config,
                                               &serf_module);

    if (conf->on == 0) {
        return DECLINED;
    }

    return drive_serf(r, conf);
}

static int is_true(const char *w)
{
    if (strcasecmp(w, "on") == 0 || strcmp(w, "1") == 0 ||
        strcasecmp(w, "true") == 0)
    {
        return 1;
    }

    return 0;
}
static const char *add_pass(cmd_parms *cmd, void *vconf,
                            int argc, char *const argv[])
{
    int i;
    apr_status_t rv;
    serf_config_t *conf = (serf_config_t *) vconf;

    if (argc < 1) {
        return "SerfPass must have at least a URI.";
    }

    rv = apr_uri_parse(cmd->pool, argv[0], &conf->url);

    if (rv != APR_SUCCESS) {
        return "Unable to parse SerfPass url.";
    }

    if (!conf->url.scheme) {
        return "Need scheme part in url.";
    }

    /* XXXX: These are bugs in apr_uri_parse. Fixme. */
    if (!conf->url.port) {
        conf->url.port = apr_uri_port_of_scheme(conf->url.scheme);
    }

    if (!conf->url.path) {
        conf->url.path = "/";
    }

    for (i = 1; i < argc; i++) {
        const char *p = argv[i];
        const char *x = ap_strchr_c(p, '=');

        if (x) {
            if (strncmp(p, "preservehost", x-p) == 0) {
                conf->preservehost = is_true(x+1);
            }
        }
    }

    conf->on = 1;

    return NULL;
}

/* SerfCluster <name> <provider> <key=value_params_to_provider> ... */

static const char *add_cluster(cmd_parms *cmd, void *d,
                               int argc, char *const argv[])
{
    const char *rv;
    ap_serf_cluster_provider_t *backend;
    int i;
    serf_cluster_t *cluster = NULL;
    serf_server_config_t *ctx =
        (serf_server_config_t *)ap_get_module_config(cmd->server->module_config,
                                                     &serf_module);

    const char *err = ap_check_cmd_context(cmd, GLOBAL_ONLY);

    if (err != NULL) {
        return err;
    }

    if (argc < 2) {
        return "SerfCluster must have at least a name and provider.";
    }

    cluster = apr_palloc(cmd->pool, sizeof(serf_cluster_t));
    cluster->name = apr_pstrdup(cmd->pool, argv[0]);
    cluster->provider = apr_pstrdup(cmd->pool, argv[1]);
    cluster->params = apr_table_make(cmd->pool, 6);

    backend = ap_lookup_provider(AP_SERF_CLUSTER_PROVIDER, cluster->provider, "0");

    if (backend == NULL) {
        return apr_psprintf(cmd->pool, "SerfCluster: unable to find "
                            "provider '%s'", cluster->provider);
    }

    for (i = 2; i < argc; i++) {
        const char *p = argv[i];
        const char *x = ap_strchr_c(p, '=');

        if (x && strlen(p) > 1) {
            apr_table_addn(cluster->params,
                           apr_pstrmemdup(cmd->pool, p, x-p),
                           x+1);
        }
        else {
            apr_table_addn(cluster->params,
                           apr_pstrdup(cmd->pool, p),
                           "");
        }
    }

    if (backend->check_config == NULL) {
        return apr_psprintf(cmd->pool, "SerfCluster: Provider '%s' failed to "
                             "provider a configuration checker",
                            cluster->provider);
    }

    rv = backend->check_config(backend->baton, cmd, cluster->params);

    if (rv) {
        return rv;
    }

    apr_hash_set(ctx->clusters, cluster->name, APR_HASH_KEY_STRING, cluster);

    return NULL;
}

static void *create_dir_config(apr_pool_t *p, char *dummy)
{
    serf_config_t *new = (serf_config_t *) apr_pcalloc(p, sizeof(serf_config_t));
    new->on = 0;
    new->preservehost = 1;
    return new;
}

static void *create_server_config(apr_pool_t *p, server_rec *s)
{
    serf_server_config_t *ctx =
        (serf_server_config_t *) apr_pcalloc(p, sizeof(serf_server_config_t));

    ctx->clusters = apr_hash_make(p);

    return ctx;
}

static void * merge_server_config(apr_pool_t *p, void *basev, void *overridesv)
{
    serf_server_config_t *ctx = apr_pcalloc(p, sizeof(serf_server_config_t));
    serf_server_config_t *base = (serf_server_config_t *) basev;
    serf_server_config_t *overrides = (serf_server_config_t *) overridesv;

    ctx->clusters = apr_hash_overlay(p, base->clusters, overrides->clusters);
    return ctx;
}

static const command_rec serf_cmds[] =
{
    AP_INIT_TAKE_ARGV("SerfCluster", add_cluster, NULL, RSRC_CONF,
                      "Configure a cluster backend"),
    AP_INIT_TAKE_ARGV("SerfPass", add_pass, NULL, OR_INDEXES,
                      "URL to reverse proxy to"),
    {NULL}
};

typedef struct hb_table_baton_t {
    apr_pool_t *p;
    const char *msg;
} hb_table_baton_t;

static int hb_table_check(void *rec, const char *key, const char *value)
{
    hb_table_baton_t *b = (hb_table_baton_t*)rec;
    if (strcmp(key, "path") != 0) {
        b->msg = apr_psprintf(b->p,
                              "SerfCluster Heartbeat Invalid parameter '%s'",
                              key);
        return 1;
    }

    return 0;
}

static const char* hb_config_check(void *baton,
                                   cmd_parms *cmd,
                                   apr_table_t *params)
{
    hb_table_baton_t b;

    if (apr_is_empty_table(params)) {
        return "SerfCluster Heartbeat requires a path to the heartbeat information.";
    }

    b.p = cmd->pool;
    b.msg = NULL;

    apr_table_do(hb_table_check, &b, params, NULL);

    if (b.msg) {
        return b.msg;
    }

    return NULL;
}

typedef struct hb_server_t {
    const char *ip;
    int busy;
    int ready;
    int seen;
    unsigned int port;
} hb_server_t;

static void
argstr_to_table(apr_pool_t *p, char *str, apr_table_t *parms)
{
    char *key;
    char *value;
    char *strtok_state;

    key = apr_strtok(str, "&", &strtok_state);
    while (key) {
        value = strchr(key, '=');
        if (value) {
            *value = '\0';      /* Split the string in two */
            value++;            /* Skip passed the = */
        }
        else {
            value = "1";
        }
        ap_unescape_url(key);
        ap_unescape_url(value);
        apr_table_set(parms, key, value);
        key = apr_strtok(NULL, "&", &strtok_state);
    }
}

static apr_status_t read_heartbeats(const char *path,
                                    apr_array_header_t *servers,
                                    apr_pool_t *pool)
{
    apr_finfo_t fi;
    apr_status_t rv;
    apr_file_t *fp;

    if (!path) {
        return APR_SUCCESS;
    }

    rv = apr_file_open(&fp, path, APR_READ|APR_BINARY|APR_BUFFERED,
                       APR_OS_DEFAULT, pool);

    if (rv) {
        return rv;
    }

    rv = apr_file_info_get(&fi, APR_FINFO_SIZE, fp);

    if (rv) {
        return rv;
    }

    {
        char *t;
        int lineno = 0;
        apr_table_t *hbt = apr_table_make(pool, 10);
        char buf[4096];

        while (apr_file_gets(buf, sizeof(buf), fp) == APR_SUCCESS) {
            hb_server_t *server;
            const char *ip;
            lineno++;

            /* comment */
            if (buf[0] == '#') {
                continue;
            }


            /* line format: <IP> <query_string>\n */
            t = strchr(buf, ' ');
            if (!t) {
                continue;
            }

            ip = apr_pstrmemdup(pool, buf, t - buf);
            t++;
            server = apr_pcalloc(pool, sizeof(hb_server_t));
            server->ip = ip;
            server->port = 80;
            server->seen = -1;
            apr_table_clear(hbt);

            argstr_to_table(pool, apr_pstrdup(pool, t), hbt);

            if (apr_table_get(hbt, "busy")) {
                server->busy = atoi(apr_table_get(hbt, "busy"));
            }

            if (apr_table_get(hbt, "ready")) {
                server->ready = atoi(apr_table_get(hbt, "ready"));
            }

            if (apr_table_get(hbt, "lastseen")) {
                server->seen = atoi(apr_table_get(hbt, "lastseen"));
            }

            if (apr_table_get(hbt, "port")) {
                server->port = atoi(apr_table_get(hbt, "port"));
            }

            if (server->busy == 0 && server->ready != 0) {
                /* Server has zero threads active, but lots of them ready,
                 * it likely just started up, so lets /4 the number ready,
                 * to prevent us from completely flooding it with all new
                 * requests.
                 */
                server->ready = server->ready / 4;
            }

            APR_ARRAY_PUSH(servers, hb_server_t *) = server;
        }
    }

    return APR_SUCCESS;
}

static int hb_server_sort(const void *a_, const void *b_)
{
    hb_server_t *a = (hb_server_t*)a_;
    hb_server_t *b = (hb_server_t*)b_;
    if (a->ready == b->ready) {
        return 0;
    }
    else if (a->ready > b->ready) {
        return -1;
    }
    else {
        return 1;
    }
}

static int hb_list_servers(void *baton,
                           request_rec *r,
                           apr_table_t *params,
                           apr_array_header_t **out_servers)
{
    int i;
    hb_server_t *hbs;
    apr_status_t rv;
    apr_pool_t *tpool;
    apr_array_header_t *tmpservers;
    apr_array_header_t *servers;
    const char *path = apr_table_get(params, "path");

    apr_pool_create(&tpool, r->pool);
    apr_pool_tag(tpool, "mod_serf_hb");

    path = ap_server_root_relative(tpool, path);

    tmpservers = apr_array_make(tpool, 32, sizeof(hb_server_t *));
    rv = read_heartbeats(path, tmpservers, tpool);

    if (rv) {
        ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r, APLOGNO(01134)
                      "SerfCluster: Heartbeat unable to read '%s'", path);
        apr_pool_destroy(tpool);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    qsort(tmpservers->elts, tmpservers->nelts, sizeof(hb_server_t *),
          hb_server_sort);

    servers = apr_array_make(r->pool, tmpservers->nelts, sizeof(ap_serf_server_t *));
    for (i = 0;
         i < tmpservers->nelts;
         i++)
    {
        ap_serf_server_t *x;

        hbs = APR_ARRAY_IDX(tmpservers, i, hb_server_t *);
        if (hbs->ready > 0) {
            x = apr_palloc(r->pool, sizeof(ap_serf_server_t));
            x->ip = apr_pstrdup(r->pool, hbs->ip);
            x->port = hbs->port;
            APR_ARRAY_PUSH(servers, ap_serf_server_t *) = x;
        }
    }

    *out_servers = servers;
    apr_pool_destroy(tpool);
    return OK;
}

static const ap_serf_cluster_provider_t builtin_heartbeat =
{
    "heartbeat",
    NULL,
    &hb_config_check,
    &hb_list_servers,
    NULL,
    NULL
};

static int static_table_check(void *rec, const char *key, const char *value)
{
    hb_table_baton_t *b = (hb_table_baton_t*)rec;
    if (strcmp(key, "hosts") != 0 &&
        strcmp(key, "order") != 0) {
        b->msg = apr_psprintf(b->p,
                              "SerfCluster Static Invalid parameter '%s'",
                              key);
        return 1;
    }

    return 0;
}

static const char* static_config_check(void *baton,
                                   cmd_parms *cmd,
                                   apr_table_t *params)
{
    hb_table_baton_t b;

    if (apr_is_empty_table(params)) {
        return "SerfCluster Static requires at least a host list.";
    }

    b.p = cmd->pool;
    b.msg = NULL;

    apr_table_do(static_table_check, &b, params, NULL);

    if (b.msg) {
        return b.msg;
    }

    if (apr_table_get(params, "hosts") == NULL) {
        return "SerfCluster Static requires at least a hosts parameter";
    }
    return NULL;
}

static int static_list_servers(void *baton,
                               request_rec *r,
                               apr_table_t *params,
                               apr_array_header_t **out_servers)
{
    apr_status_t rv;
    char *ip;
    char *strtok_state;
    apr_array_header_t *servers;
    const char *hosts = apr_table_get(params, "hosts");
    const char *order = apr_table_get(params, "order");

    servers = apr_array_make(r->pool, 10, sizeof(ap_serf_server_t *));

    ip = apr_strtok(apr_pstrdup(r->pool, hosts), ",", &strtok_state);
    while (ip) {
        char *host_str;
        char *scope_id;
        apr_port_t port = 0;

        rv = apr_parse_addr_port(&host_str, &scope_id, &port, ip, r->pool);
        if (!rv) {
            ap_serf_server_t *s = apr_palloc(r->pool, sizeof(ap_serf_server_t));
            s->ip = host_str;
            s->port = port ? port : 80;
            APR_ARRAY_PUSH(servers, ap_serf_server_t *) = s;
        }
        ip = apr_strtok(NULL, ",", &strtok_state);
    }

    if (strcmp(order, "random") == 0) {
        /* TODO: support order=random */
    }

    *out_servers = servers;

    return OK;
}

static const ap_serf_cluster_provider_t builtin_static =
{
    "static",
    NULL,
    &static_config_check,
    &static_list_servers,
    NULL,
    NULL
};

static int serf_post_config(apr_pool_t *p, apr_pool_t *plog, apr_pool_t *ptemp, server_rec *s)
{
    apr_status_t rv;
    rv = ap_mpm_query(AP_MPMQ_HAS_SERF, &mpm_supprts_serf);

    if (rv != APR_SUCCESS) {
        mpm_supprts_serf = 0;
    }

    return OK;
}

static void register_hooks(apr_pool_t *p)
{
    ap_register_provider(p, AP_SERF_CLUSTER_PROVIDER,
                         "heartbeat", "0", &builtin_heartbeat);

    ap_register_provider(p, AP_SERF_CLUSTER_PROVIDER,
                         "static", "0", &builtin_static);

    ap_hook_post_config(serf_post_config, NULL, NULL, APR_HOOK_MIDDLE);

    ap_hook_handler(serf_handler, NULL, NULL, APR_HOOK_FIRST);
}

AP_DECLARE_MODULE(serf) =
{
    STANDARD20_MODULE_STUFF,
    create_dir_config,
    NULL,
    create_server_config,
    merge_server_config,
    serf_cmds,
    register_hooks
};
