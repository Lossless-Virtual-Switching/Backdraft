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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <apr_lib.h>
#include <apr_buckets.h>
#include <apr_hash.h>
#include <apr_time.h>
#include <apr_date.h>
#include <apr_strings.h>
#include <apr_thread_mutex.h>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ocsp.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>

#if defined(LIBRESSL_VERSION_NUMBER)
/* Missing from LibreSSL */
#define MD_USE_OPENSSL_PRE_1_1_API (LIBRESSL_VERSION_NUMBER < 0x2070000f)
#else /* defined(LIBRESSL_VERSION_NUMBER) */
#define MD_USE_OPENSSL_PRE_1_1_API (OPENSSL_VERSION_NUMBER < 0x10100000L)
#endif

#include "md.h"
#include "md_crypt.h"
#include "md_json.h"
#include "md_log.h"
#include "md_http.h"
#include "md_json.h"
#include "md_result.h"
#include "md_status.h"
#include "md_store.h"
#include "md_util.h"
#include "md_ocsp.h"

#define MD_OCSP_ID_LENGTH   SHA_DIGEST_LENGTH
   
struct md_ocsp_reg_t {
    apr_pool_t *p;
    md_store_t *store;
    const char *user_agent;
    const char *proxy_url;
    apr_hash_t *hash;
    apr_thread_mutex_t *mutex;
    md_timeslice_t renew_window;
    md_job_notify_cb *notify;
    void *notify_ctx;
};

typedef struct md_ocsp_status_t md_ocsp_status_t; 
struct md_ocsp_status_t {
    md_data_t id;
    const char *hexid;
    const char *hex_sha256;
    OCSP_CERTID *certid;
    const char *responder_url;
    
    apr_time_t next_run;      /* when the responder shall be asked again */
    int errors;               /* consecutive failed attempts */

    md_ocsp_cert_stat_t resp_stat;
    md_data_t resp_der;
    md_timeperiod_t resp_valid;
    
    md_data_t req_der;
    OCSP_REQUEST *ocsp_req;
    md_ocsp_reg_t *reg;

    const char *md_name;
    const char *file_name;
    
    apr_time_t resp_mtime;
    apr_time_t resp_last_check;
};

const char *md_ocsp_cert_stat_name(md_ocsp_cert_stat_t stat)
{
    switch (stat) {
        case MD_OCSP_CERT_ST_GOOD: return "good";
        case MD_OCSP_CERT_ST_REVOKED: return "revoked";
        default: return "unknown";
    }
}

md_ocsp_cert_stat_t md_ocsp_cert_stat_value(const char *name)
{
    if (name && !strcmp("good", name)) return MD_OCSP_CERT_ST_GOOD;
    if (name && !strcmp("revoked", name)) return MD_OCSP_CERT_ST_REVOKED;
    return MD_OCSP_CERT_ST_UNKNOWN;
}

static apr_status_t init_cert_id(md_data_t *data, const md_cert_t *cert)
{
    X509 *x = md_cert_get_X509(cert);
    unsigned int ulen = 0;
    
    assert(data->len == SHA_DIGEST_LENGTH);
    if (X509_digest(x, EVP_sha1(), (unsigned char*)data->data, &ulen) != 1) {
        return APR_EGENERAL;
    }
    data->len = ulen;
    return APR_SUCCESS;
}

static void ostat_req_cleanup(md_ocsp_status_t *ostat)
{
    if (ostat->ocsp_req) {
        OCSP_REQUEST_free(ostat->ocsp_req);
        ostat->ocsp_req = NULL;
    }
    if (ostat->req_der.data) {
        OPENSSL_free((void*)ostat->req_der.data);
        ostat->req_der.data = NULL;
        ostat->req_der.len = 0;
    }
}

static int ostat_cleanup(void *ctx, const void *key, apr_ssize_t klen, const void *val)
{
    md_ocsp_reg_t *reg = ctx;
    md_ocsp_status_t *ostat = (md_ocsp_status_t *)val;
    
    (void)reg;
    (void)key;
    (void)klen;
    ostat_req_cleanup(ostat);
    if (ostat->certid) {
        OCSP_CERTID_free(ostat->certid);
        ostat->certid = NULL;
    }
    if (ostat->resp_der.data) {
        OPENSSL_free((void*)ostat->resp_der.data);
        ostat->resp_der.data = NULL;
        ostat->resp_der.len = 0;
    }
    return 1;
}

static int ostat_should_renew(md_ocsp_status_t *ostat) 
{
    md_timeperiod_t renewal;
    
    renewal = md_timeperiod_slice_before_end(&ostat->resp_valid, &ostat->reg->renew_window);
    return md_timeperiod_has_started(&renewal, apr_time_now());
}  

static apr_status_t ostat_set(md_ocsp_status_t *ostat, md_ocsp_cert_stat_t stat,
                              md_data_t *der, md_timeperiod_t *valid, apr_time_t mtime)
{
    apr_status_t rv = APR_SUCCESS;
    char *s = (char*)der->data;
    
    if (der->len) {
        s = OPENSSL_malloc(der->len);
        if (!s) {
            rv = APR_ENOMEM;
            goto leave;
        }
        memcpy((char*)s, der->data, der->len);
    }
 
    if (ostat->resp_der.data) {
        OPENSSL_free((void*)ostat->resp_der.data);
        ostat->resp_der.data = NULL;
        ostat->resp_der.len = 0;
    }
    
    ostat->resp_stat = stat;
    ostat->resp_der.data = s;
    ostat->resp_der.len = der->len;
    ostat->resp_valid = *valid;
    ostat->resp_mtime = mtime;
    
    ostat->errors = 0;
    ostat->next_run = md_timeperiod_slice_before_end(
        &ostat->resp_valid, &ostat->reg->renew_window).start;
    
leave:
    return rv;
}

static apr_status_t ostat_from_json(md_ocsp_cert_stat_t *pstat, 
                                    md_data_t *resp_der, md_timeperiod_t *resp_valid, 
                                    md_json_t *json, apr_pool_t *p)
{
    const char *s;
    md_timeperiod_t valid;
    apr_status_t rv = APR_ENOENT;
    
    memset(resp_der, 0, sizeof(*resp_der));
    memset(resp_valid, 0, sizeof(*resp_valid));
    s = md_json_dups(p, json, MD_KEY_VALID, MD_KEY_FROM, NULL);
    if (s && *s) valid.start = apr_date_parse_rfc(s);
    s = md_json_dups(p, json, MD_KEY_VALID, MD_KEY_UNTIL, NULL);
    if (s && *s) valid.end = apr_date_parse_rfc(s);
    s = md_json_dups(p, json, MD_KEY_RESPONSE, NULL);
    if (!s || !*s) goto leave;
    md_util_base64url_decode(resp_der, s, p);
    *pstat = md_ocsp_cert_stat_value(md_json_gets(json, MD_KEY_STATUS, NULL));
    *resp_valid = valid;
    rv = APR_SUCCESS;
leave:
    return rv;
}

static void ostat_to_json(md_json_t *json, md_ocsp_cert_stat_t stat,
                          const md_data_t *resp_der, const md_timeperiod_t *resp_valid, 
                          apr_pool_t *p)
{
    const char *s = NULL;

    if (resp_der->len > 0) {
        md_json_sets(md_util_base64url_encode(resp_der, p), json, MD_KEY_RESPONSE, NULL);
        s = md_ocsp_cert_stat_name(stat);
        if (s) md_json_sets(s, json, MD_KEY_STATUS, NULL);
        md_json_set_timeperiod(resp_valid, json, MD_KEY_VALID, NULL);
    }
}

static apr_status_t ocsp_status_refresh(md_ocsp_status_t *ostat, apr_pool_t *ptemp)
{
    md_store_t *store = ostat->reg->store;
    md_json_t *jprops;
    apr_time_t mtime;
    apr_status_t rv = APR_EAGAIN;
    md_data_t resp_der;
    md_timeperiod_t resp_valid;
    md_ocsp_cert_stat_t resp_stat;
    /* Check if the store holds a newer response than the one we have */
    mtime = md_store_get_modified(store, MD_SG_OCSP, ostat->md_name, ostat->file_name, ptemp);
    if (mtime <= ostat->resp_mtime) goto leave;
    rv = md_store_load_json(store, MD_SG_OCSP, ostat->md_name, ostat->file_name, &jprops, ptemp);
    if (APR_SUCCESS != rv) goto leave;
    rv = ostat_from_json(&resp_stat, &resp_der, &resp_valid, jprops, ptemp);
    if (APR_SUCCESS != rv) goto leave;
    rv = ostat_set(ostat, resp_stat, &resp_der, &resp_valid, mtime);
    if (APR_SUCCESS != rv) goto leave;
leave:
    return rv;
}


static apr_status_t ocsp_status_save(md_ocsp_cert_stat_t stat, const md_data_t *resp_der, 
                                     const md_timeperiod_t *resp_valid,
                                     md_ocsp_status_t *ostat, apr_pool_t *ptemp)
{
    md_store_t *store = ostat->reg->store;
    md_json_t *jprops;
    apr_time_t mtime;
    apr_status_t rv;
    
    jprops = md_json_create(ptemp);
    ostat_to_json(jprops, stat, resp_der, resp_valid, ptemp);
    rv = md_store_save_json(store, ptemp, MD_SG_OCSP, ostat->md_name, ostat->file_name, jprops, 0);
    if (APR_SUCCESS != rv) goto leave;
    mtime = md_store_get_modified(store, MD_SG_OCSP, ostat->md_name, ostat->file_name, ptemp);
    if (mtime) ostat->resp_mtime = mtime;
leave:
    return rv;
}

static apr_status_t ocsp_reg_cleanup(void *data)
{
    md_ocsp_reg_t *reg = data;
    
    /* free all OpenSSL structures that we hold */
    apr_hash_do(ostat_cleanup, reg, reg->hash);
    return APR_SUCCESS;
}

apr_status_t md_ocsp_reg_make(md_ocsp_reg_t **preg, apr_pool_t *p, md_store_t *store, 
                              const md_timeslice_t *renew_window,
                              const char *user_agent, const char *proxy_url)
{
    md_ocsp_reg_t *reg;
    apr_status_t rv = APR_SUCCESS;
    
    reg = apr_palloc(p, sizeof(*reg));
    if (!reg) {
        rv = APR_ENOMEM;
        goto leave;
    }
    reg->p = p;
    reg->store = store;
    reg->user_agent = user_agent;
    reg->proxy_url = proxy_url;
    reg->hash = apr_hash_make(p);
    reg->renew_window = *renew_window;
    
    rv = apr_thread_mutex_create(&reg->mutex, APR_THREAD_MUTEX_NESTED, p);
    if (APR_SUCCESS != rv) goto leave;

    apr_pool_cleanup_register(p, reg, ocsp_reg_cleanup, apr_pool_cleanup_null);
leave:
    *preg = (APR_SUCCESS == rv)? reg : NULL;
    return rv;
}

apr_status_t md_ocsp_prime(md_ocsp_reg_t *reg, md_cert_t *cert, md_cert_t *issuer, const md_t *md)
{
    char iddata[MD_OCSP_ID_LENGTH];
    md_ocsp_status_t *ostat;
    STACK_OF(OPENSSL_STRING) *ssk = NULL;
    const char *name, *s;
    md_data_t id;
    apr_status_t rv;
    
    /* Called during post_config. no mutex protection needed */
    name = md? md->name : MD_OTHER;
    id.data = iddata; id.len = sizeof(iddata);
    
    md_log_perror(MD_LOG_MARK, MD_LOG_DEBUG, 0, reg->p, 
                  "md[%s]: priming OCSP status", name);
    rv = init_cert_id(&id, cert);
    if (APR_SUCCESS != rv) goto leave;
    
    ostat = apr_hash_get(reg->hash, id.data, (apr_ssize_t)id.len);
    if (ostat) goto leave; /* already seen it, cert is used in >1 server_rec */
    
    ostat = apr_pcalloc(reg->p, sizeof(*ostat));
    md_data_assign_pcopy(&ostat->id, &id, reg->p);
    ostat->reg = reg;
    ostat->md_name = name;
    md_data_to_hex(&ostat->hexid, 0, reg->p, &ostat->id);
    ostat->file_name = apr_psprintf(reg->p, "ocsp-%s.json", ostat->hexid);
    rv = md_cert_to_sha256_fingerprint(&ostat->hex_sha256, cert, reg->p); 
    if (APR_SUCCESS != rv) goto leave;

    md_log_perror(MD_LOG_MARK, MD_LOG_TRACE2, 0, reg->p, 
                  "md[%s]: getting ocsp responder from cert", name);
    ssk = X509_get1_ocsp(md_cert_get_X509(cert));
    if (!ssk) {
        rv = APR_ENOENT;
        md_log_perror(MD_LOG_MARK, MD_LOG_ERR, rv, reg->p, 
                      "md[%s]: certificate with serial %s has not OCSP responder URL", 
                      name, md_cert_get_serial_number(cert, reg->p));
        goto leave;
    }
    s = sk_OPENSSL_STRING_value(ssk, 0);
    md_log_perror(MD_LOG_MARK, MD_LOG_TRACE2, 0, reg->p, 
                  "md[%s]: ocsp responder found '%s'", name, s);
    ostat->responder_url = apr_pstrdup(reg->p, s);
    X509_email_free(ssk);

    ostat->certid = OCSP_cert_to_id(NULL, md_cert_get_X509(cert), md_cert_get_X509(issuer));
    if (!ostat->certid) {
        rv = APR_EGENERAL;
        md_log_perror(MD_LOG_MARK, MD_LOG_ERR, rv, reg->p, 
                      "md[%s]: unable to create OCSP certid for certificate with serial %s", 
                      name, md_cert_get_serial_number(cert, reg->p));
        goto leave;
    }
    
    /* See, if we have something in store */
    ocsp_status_refresh(ostat, reg->p);
    md_log_perror(MD_LOG_MARK, MD_LOG_DEBUG, rv, reg->p, 
                  "md[%s]: adding ocsp info (responder=%s)", 
                  name, ostat->responder_url);
    apr_hash_set(reg->hash, ostat->id.data, (apr_ssize_t)ostat->id.len, ostat);
    rv = APR_SUCCESS;
leave:
    return rv;
}

apr_status_t md_ocsp_get_status(unsigned char **pder, int *pderlen,
                                md_ocsp_reg_t *reg, const md_cert_t *cert,
                                apr_pool_t *p, const md_t *md)
{
    char iddata[MD_OCSP_ID_LENGTH];
    md_ocsp_status_t *ostat;
    const char *name;
    apr_status_t rv;
    int locked = 0;
    md_data_t id;
    
    (void)p;
    (void)md;
    id.data = iddata; id.len = sizeof(iddata);
    *pder = NULL;
    *pderlen = 0;
    name = md? md->name : MD_OTHER;
    md_log_perror(MD_LOG_MARK, MD_LOG_TRACE2, 0, reg->p, 
                  "md[%s]: OCSP, get_status", name);
    rv = init_cert_id(&id, cert);
    if (APR_SUCCESS != rv) goto leave;
    
    ostat = apr_hash_get(reg->hash, id.data, (apr_ssize_t)id.len);
    if (!ostat) {
        rv = APR_ENOENT;
        goto leave;
    }
    
    /* While the ostat instance itself always exists, the response data it holds
     * may vary over time and we need locked access to make a copy. */
    apr_thread_mutex_lock(reg->mutex);
    locked = 1;
    
    if (ostat->resp_der.len <= 0) {
        /* No response known, check store for new response. */
        ocsp_status_refresh(ostat, p);
        if (ostat->resp_der.len <= 0) {
            md_log_perror(MD_LOG_MARK, MD_LOG_TRACE2, 0, reg->p, 
                          "md[%s]: OCSP, no response available", name);
            goto leave;
        }
    }
    /* We have a response */
    if (ostat_should_renew(ostat)) {
        /* But it is up for renewal. A watchdog should be busy with
         * retrieving a new one. In case of outages, this might take
         * a while, however. Pace the frequency of checks with the
         * urgency of a new response based on the remaining time. */
        long secs = (long)apr_time_sec(md_timeperiod_remaining(&ostat->resp_valid, apr_time_now()));
        apr_time_t waiting_time; 
        
        /* every hour, every minute, every second */
        waiting_time = ((secs >= MD_SECS_PER_DAY)?
                        apr_time_from_sec(60 * 60) : ((secs >= 60)? 
                        apr_time_from_sec(60) : apr_time_from_sec(1)));
        if ((apr_time_now() - ostat->resp_last_check) >= waiting_time) {
            ostat->resp_last_check = apr_time_now();
            ocsp_status_refresh(ostat, p);
        }
    }
    
    *pder = OPENSSL_malloc(ostat->resp_der.len);
    if (*pder == NULL) {
        rv = APR_ENOMEM;
        goto leave;
    }
    memcpy(*pder, ostat->resp_der.data, ostat->resp_der.len);
    *pderlen = (int)ostat->resp_der.len;
    md_log_perror(MD_LOG_MARK, MD_LOG_TRACE2, 0, reg->p, 
                  "md[%s]: OCSP, returning %ld bytes of response", 
                  name, (long)ostat->resp_der.len);
leave:
    if (locked) apr_thread_mutex_unlock(reg->mutex);
    return rv;
}

static void ocsp_get_meta(md_ocsp_cert_stat_t *pstat, md_timeperiod_t *pvalid, 
                          md_ocsp_reg_t *reg, md_ocsp_status_t *ostat, apr_pool_t *p)
{
    apr_thread_mutex_lock(reg->mutex);
    if (ostat->resp_der.len <= 0) {
        /* No resonse known, check the store if out watchdog retrieved one 
         * in the meantime. */
        ocsp_status_refresh(ostat, p);
    }
    *pvalid = ostat->resp_valid;
    *pstat = ostat->resp_stat;
    apr_thread_mutex_unlock(reg->mutex);
}

apr_status_t md_ocsp_get_meta(md_ocsp_cert_stat_t *pstat, md_timeperiod_t *pvalid,
                              md_ocsp_reg_t *reg, const md_cert_t *cert,
                              apr_pool_t *p, const md_t *md)
{
    char iddata[MD_OCSP_ID_LENGTH];
    md_ocsp_status_t *ostat;
    const char *name;
    apr_status_t rv;
    md_timeperiod_t valid;
    md_ocsp_cert_stat_t stat;
    md_data_t id;
    
    (void)p;
    (void)md;
    id.data = iddata; id.len = sizeof(iddata);
    name = md? md->name : MD_OTHER;
    memset(&valid, 0, sizeof(valid));
    stat = MD_OCSP_CERT_ST_UNKNOWN;
    md_log_perror(MD_LOG_MARK, MD_LOG_TRACE2, 0, reg->p, 
                  "md[%s]: OCSP, get_status", name);
    
    rv = init_cert_id(&id, cert);
    if (APR_SUCCESS != rv) goto leave;
    
    ostat = apr_hash_get(reg->hash, id.data, (apr_ssize_t)id.len);
    if (!ostat) {
        rv = APR_ENOENT;
        goto leave;
    }
    ocsp_get_meta(&stat, &valid, reg, ostat, p);
leave:
    *pstat = stat;
    *pvalid = valid;  
    return rv;
}

apr_size_t md_ocsp_count(md_ocsp_reg_t *reg)
{
    return apr_hash_count(reg->hash);
}

static const char *certid_as_hex(const OCSP_CERTID *certid, apr_pool_t *p)
{
    md_data_t der;
    const char *hex;
    
    memset(&der, 0, sizeof(der));
    der.len = (apr_size_t)i2d_OCSP_CERTID((OCSP_CERTID*)certid, (unsigned char**)&der.data);
    md_data_to_hex(&hex, 0, p, &der);
    OPENSSL_free((void*)der.data);
    return hex;
}

static const char *certid_summary(const OCSP_CERTID *certid, apr_pool_t *p)
{
    const char *serial, *issuer, *key, *s;
    ASN1_INTEGER *aserial;
    ASN1_OCTET_STRING *aname_hash, *akey_hash;
    ASN1_OBJECT *amd_nid;
    BIGNUM *bn; 
    md_data_t data;
    
    serial = issuer = key = "???";
    OCSP_id_get0_info(&aname_hash, &amd_nid, &akey_hash, &aserial, (OCSP_CERTID*)certid);
    if (aname_hash) {
        data.len = (apr_size_t)aname_hash->length;
        data.data = (const char*)aname_hash->data;
        md_data_to_hex(&issuer, 0, p, &data);
    }
    if (akey_hash) {
        data.len = (apr_size_t)akey_hash->length;
        data.data = (const char*)akey_hash->data;
        md_data_to_hex(&key, 0, p, &data);
    }
    if (aserial) {
        bn = ASN1_INTEGER_to_BN(aserial, NULL);
        s = BN_bn2hex(bn);
        serial = apr_pstrdup(p, s);
        OPENSSL_free((void*)bn);
        OPENSSL_free((void*)s);
    }
    return apr_psprintf(p, "certid[der=%s, issuer=%s, key=%s, serial=%s]",
                        certid_as_hex(certid, p), issuer, key, serial);
}

static const char *certstatus_string(int status)
{
    switch (status) {
        case V_OCSP_CERTSTATUS_GOOD: return "good";
        case V_OCSP_CERTSTATUS_REVOKED: return "revoked";
        case V_OCSP_CERTSTATUS_UNKNOWN: return "unknown";
        default: return "???";
    }

}

static const char *single_resp_summary(OCSP_SINGLERESP* resp, apr_pool_t *p)
{
    const OCSP_CERTID *certid;
    int status, reason = 0;
    ASN1_GENERALIZEDTIME *bup = NULL, *bnextup = NULL;
    md_timeperiod_t valid;
    
#if MD_USE_OPENSSL_PRE_1_1_API
    certid = resp->certId;
#else
    certid = OCSP_SINGLERESP_get0_id(resp);
#endif
    status = OCSP_single_get0_status(resp, &reason, NULL, &bup, &bnextup);
    valid.start = bup? md_asn1_generalized_time_get(bup) : apr_time_now();
    valid.end = md_asn1_generalized_time_get(bnextup);

    return apr_psprintf(p, "ocsp-single-resp[%s, status=%s, reason=%d, valid=%s]",
                        certid_summary(certid, p),
                        certstatus_string(status), reason,
                        md_timeperiod_print(p, &valid));
}

typedef struct {
    apr_pool_t *p;
    md_ocsp_status_t *ostat;
    md_result_t *result;
    md_job_t *job;
} md_ocsp_update_t;

static apr_status_t ostat_on_resp(const md_http_response_t *resp, void *baton)
{
    md_ocsp_update_t *update = baton;
    md_ocsp_status_t *ostat = update->ostat;
    md_http_request_t *req = resp->req;
    OCSP_RESPONSE *ocsp_resp = NULL;
    OCSP_BASICRESP *basic_resp = NULL;
    OCSP_SINGLERESP *single_resp;
    apr_status_t rv = APR_SUCCESS;
    int n, breason = 0, bstatus;
    ASN1_GENERALIZEDTIME *bup = NULL, *bnextup = NULL;
    md_data_t der, new_der;
    md_timeperiod_t valid;
    md_ocsp_cert_stat_t nstat;
    
    der.data = new_der.data = NULL;
    der.len  = new_der.len = 0;

    md_result_activity_printf(update->result, "status of certid %s, reading response", 
                              ostat->hexid);
    if (APR_SUCCESS != (rv = apr_brigade_pflatten(resp->body, (char**)&der.data, 
                                                  &der.len, req->pool))) {
        goto leave;
    }
    if (NULL == (ocsp_resp = d2i_OCSP_RESPONSE(NULL, (const unsigned char**)&der.data, 
                                               (long)der.len))) {
        rv = APR_EINVAL;
        md_result_set(update->result, rv, "response body does not parse as OCSP response");
        md_result_log(update->result, MD_LOG_DEBUG);
        goto leave;
    }
    /* got a response! but what does it say? */
    n = OCSP_response_status(ocsp_resp);
    if (OCSP_RESPONSE_STATUS_SUCCESSFUL != n) {
        rv = APR_EINVAL;
        md_result_printf(update->result, rv, "OCSP response status is, unsuccessfully, %d", n);
        md_result_log(update->result, MD_LOG_DEBUG);
        goto leave;
    }
    basic_resp = OCSP_response_get1_basic(ocsp_resp);
    if (!basic_resp) {
        rv = APR_EINVAL;
        md_result_set(update->result, rv, "OCSP response has no basicresponse");
        md_result_log(update->result, MD_LOG_DEBUG);
        goto leave;
    }
    /* The notion of nonce enabled freshness in OCSP responses, e.g. that the response
     * contains the signed nonce we sent to the responder, does not scale well. Responders
     * like to return cached response bytes and therefore do not add a nonce to it.
     * So, in reality, we can only detect a mismatch when present and otherwise have
     * to accept it. */
    switch ((n = OCSP_check_nonce(ostat->ocsp_req, basic_resp))) {
        case 1:
            md_log_perror(MD_LOG_MARK, MD_LOG_DEBUG, 0, req->pool, 
                          "req[%d]: OCSP respoonse nonce does match", req->id);
            break;
        case 0:
            rv = APR_EINVAL;
            md_result_printf(update->result, rv, "OCSP nonce mismatch in response", n);
            md_result_log(update->result, MD_LOG_WARNING);
            goto leave;
            
        case -1:
            md_log_perror(MD_LOG_MARK, MD_LOG_TRACE1, 0, req->pool, 
                          "req[%d]: OCSP respoonse did not return the nonce", req->id);
            break;
        default:
            break;
    }
    
    if (!OCSP_resp_find_status(basic_resp, ostat->certid, &bstatus,
                               &breason, NULL, &bup, &bnextup)) {
        const char *prefix, *slist = "", *sep = "";
        int i;
        
        rv = APR_EINVAL;
        prefix = apr_psprintf(req->pool, "OCSP response, no matching status reported for  %s",
                              certid_summary(ostat->certid, req->pool));
        for (i = 0; i < OCSP_resp_count(basic_resp); ++i) {
            single_resp = OCSP_resp_get0(basic_resp, i);
            slist = apr_psprintf(req->pool, "%s%s%s", slist, sep, 
                                 single_resp_summary(single_resp, req->pool));
            sep = ", ";
        }
        md_result_printf(update->result, rv, "%s, status list [%s]", prefix, slist);
        md_result_log(update->result, MD_LOG_DEBUG);
        goto leave;
    }
    if (V_OCSP_CERTSTATUS_UNKNOWN == bstatus) {
        rv = APR_ENOENT;
        md_result_set(update->result, rv, "OCSP basicresponse says cert is unknown");
        md_result_log(update->result, MD_LOG_DEBUG);
        goto leave;
    }
    if (!bnextup) {
        rv = APR_EINVAL;
        md_result_set(update->result, rv, "OCSP basicresponse reports not valid dates");
        md_result_log(update->result, MD_LOG_DEBUG);
        goto leave;
    }
    
    /* Coming here, we have a response for our certid and it is either GOOD
     * or REVOKED. Both cases we want to remember and use in stapling. */
    n = i2d_OCSP_RESPONSE(ocsp_resp, (unsigned char**)&new_der.data);
    if (n <= 0) {
        rv = APR_EGENERAL;
        md_result_set(update->result, rv, "error DER encoding OCSP response");
        md_result_log(update->result, MD_LOG_WARNING);
        goto leave;
    }
    nstat = (bstatus == V_OCSP_CERTSTATUS_GOOD)? MD_OCSP_CERT_ST_GOOD : MD_OCSP_CERT_ST_REVOKED;
    new_der.len = (apr_size_t)n;
    valid.start = bup? md_asn1_generalized_time_get(bup) : apr_time_now();
    valid.end = md_asn1_generalized_time_get(bnextup);
    
    /* First, update the instance with a copy */
    apr_thread_mutex_lock(ostat->reg->mutex);
    ostat_set(ostat, nstat, &new_der, &valid, apr_time_now());
    apr_thread_mutex_unlock(ostat->reg->mutex);
    
    /* Next, save the original response */
    rv = ocsp_status_save(nstat, &new_der, &valid, ostat, req->pool); 
    if (APR_SUCCESS != rv) {
        md_result_set(update->result, rv, "error saving OCSP status");
        md_result_log(update->result, MD_LOG_ERR);
        goto leave;
    }
    
    md_result_printf(update->result, rv, "certificate status is %s, status valid %s", 
                     (nstat == MD_OCSP_CERT_ST_GOOD)? "GOOD" : "REVOKED",
                     md_timeperiod_print(req->pool, &ostat->resp_valid));
    md_result_log(update->result, MD_LOG_DEBUG);

leave:
    if (new_der.data) OPENSSL_free((void*)new_der.data);
    if (basic_resp) OCSP_BASICRESP_free(basic_resp);
    if (ocsp_resp) OCSP_RESPONSE_free(ocsp_resp);
    return rv;
}

static apr_status_t ostat_on_req_status(const md_http_request_t *req, apr_status_t status, 
                                        void *baton)
{
    md_ocsp_update_t *update = baton;
    md_ocsp_status_t *ostat = update->ostat;

    (void)req;
    md_job_end_run(update->job, update->result);
    if (APR_SUCCESS != status) {
        ++ostat->errors;
        ostat->next_run = apr_time_now() + md_job_delay_on_errors(ostat->errors); 
        md_result_printf(update->result, status, "OCSP status update failed (%d. time)",  
                         ostat->errors);
        md_result_log(update->result, MD_LOG_DEBUG);
        md_job_log_append(update->job, "ocsp-error", 
                          update->result->problem, update->result->detail);
        md_job_holler(update->job, "ocsp-errored");
        goto leave;
    }
    md_job_notify(update->job, "ocsp-renewed", update->result);

leave:
    md_job_save(update->job, update->result, update->p);
    ostat_req_cleanup(ostat);
    return APR_SUCCESS;
}

typedef struct {
    md_ocsp_reg_t *reg;
    apr_array_header_t *todos;
    apr_pool_t *ptemp;
    apr_time_t time;
    int max_parallel;
} md_ocsp_todo_ctx_t;

static apr_status_t next_todo(md_http_request_t **preq, void *baton, 
                              md_http_t *http, int in_flight)
{
    md_ocsp_todo_ctx_t *ctx = baton;
    md_ocsp_update_t *update, **pupdate;    
    md_ocsp_status_t *ostat;
    OCSP_CERTID *certid = NULL;
    md_http_request_t *req = NULL;
    apr_status_t rv = APR_ENOENT;
    apr_table_t *headers;
    int len;
    
    if (in_flight < ctx->max_parallel) {
        pupdate = apr_array_pop(ctx->todos);
        if (pupdate) {
            update = *pupdate;
            ostat = update->ostat;
            
            update->job = md_ocsp_job_make(ctx->reg, ostat->md_name, update->p);
            md_job_load(update->job);
            md_job_start_run(update->job, update->result, ctx->reg->store);
             
            if (!ostat->ocsp_req) {
                ostat->ocsp_req = OCSP_REQUEST_new();
                if (!ostat->ocsp_req) goto leave;
                certid = OCSP_CERTID_dup(ostat->certid);
                if (!certid) goto leave;
                if (!OCSP_request_add0_id(ostat->ocsp_req, certid)) goto leave;
                OCSP_request_add1_nonce(ostat->ocsp_req, 0, -1);
                certid = NULL;
            }
            if (0 == ostat->req_der.len) {
                len = i2d_OCSP_REQUEST(ostat->ocsp_req, (unsigned char**)&ostat->req_der.data);
                if (len < 0) goto leave;
                ostat->req_der.len = (apr_size_t)len;
            }
            md_result_activity_printf(update->result, "status of certid %s, "
                                      "contacting %s", ostat->hexid, ostat->responder_url);
            headers = apr_table_make(ctx->ptemp, 5);
            apr_table_set(headers, "Expect", "");
            rv = md_http_POSTd_create(&req, http, ostat->responder_url, headers, 
                                      "application/ocsp-request", &ostat->req_der);
            if (APR_SUCCESS != rv) goto leave;
            md_http_set_on_status_cb(req, ostat_on_req_status, update);
            md_http_set_on_response_cb(req, ostat_on_resp, update);
            rv = APR_SUCCESS;
        }
    }
leave:
    *preq = (APR_SUCCESS == rv)? req : NULL;
    if (certid) OCSP_CERTID_free(certid);
    return rv;
}

static int select_updates(void *baton, const void *key, apr_ssize_t klen, const void *val)
{
    md_ocsp_todo_ctx_t *ctx = baton;
    md_ocsp_status_t *ostat = (md_ocsp_status_t *)val;
    md_ocsp_update_t *update;
    
    (void)key;
    (void)klen;
    if (ostat->next_run <= ctx->time) {
        update = apr_pcalloc(ctx->ptemp, sizeof(*update));
        update->p = ctx->ptemp;
        update->ostat = ostat;
        update->result = md_result_md_make(update->p, ostat->md_name);
        update->job = NULL;
        APR_ARRAY_PUSH(ctx->todos, md_ocsp_update_t*) = update;
    }
    return 1;
}

static int select_next_run(void *baton, const void *key, apr_ssize_t klen, const void *val)
{
    md_ocsp_todo_ctx_t *ctx = baton;
    md_ocsp_status_t *ostat = (md_ocsp_status_t *)val;
    
    (void)key;
    (void)klen;
    if (ostat->next_run < ctx->time && ostat->next_run > apr_time_now()) {
        ctx->time = ostat->next_run;
    }
    return 1;
}

void md_ocsp_renew(md_ocsp_reg_t *reg, apr_pool_t *p, apr_pool_t *ptemp, apr_time_t *pnext_run)
{
    md_ocsp_todo_ctx_t ctx;
    md_http_t *http;
    apr_status_t rv = APR_SUCCESS;
    
    (void)p;
    (void)pnext_run;
    
    ctx.reg = reg;
    ctx.ptemp = ptemp;
    ctx.todos = apr_array_make(ptemp, (int)md_ocsp_count(reg), sizeof(md_ocsp_status_t*));
    ctx.max_parallel = 6; /* the magic number in HTTP */
    
    /* Create a list of update tasks that are needed now or in the next minute */
    ctx.time = apr_time_now() + apr_time_from_sec(60);;
    apr_hash_do(select_updates, &ctx, reg->hash);
    md_log_perror(MD_LOG_MARK, MD_LOG_DEBUG, 0, p, 
                  "OCSP status updates due: %d",  ctx.todos->nelts);
    if (!ctx.todos->nelts) goto leave;
    
    rv = md_http_create(&http, ptemp, reg->user_agent, reg->proxy_url);
    if (APR_SUCCESS != rv) goto leave;
    
    rv = md_http_multi_perform(http, next_todo, &ctx);

leave:
    /* When do we need to run next? *pnext_run contains the planned schedule from
     * the watchdog. We can make that earlier if we need it. */
    ctx.time = *pnext_run;
    apr_hash_do(select_next_run, &ctx, reg->hash);

    /* sanity check and return */
    if (ctx.time < apr_time_now()) ctx.time = apr_time_now() + apr_time_from_sec(1);
    *pnext_run = ctx.time;

    if (APR_SUCCESS != rv) {
        md_log_perror(MD_LOG_MARK, MD_LOG_DEBUG, rv, p, "ocsp_renew done");
    }
    return;
}

apr_status_t md_ocsp_remove_responses_older_than(md_ocsp_reg_t *reg, apr_pool_t *p, 
                                                 apr_time_t timestamp)
{
    return md_store_remove_not_modified_since(reg->store, p, timestamp, 
                                              MD_SG_OCSP, "*", "ocsp*.json");
}

typedef struct {
    apr_pool_t *p;
    md_ocsp_reg_t *reg;
    int good;
    int revoked;
    int unknown;
} ocsp_summary_ctx_t;

static int add_to_summary(void *baton, const void *key, apr_ssize_t klen, const void *val)
{
    ocsp_summary_ctx_t *ctx = baton;
    md_ocsp_status_t *ostat = (md_ocsp_status_t *)val;
    md_ocsp_cert_stat_t stat;
    md_timeperiod_t valid;
    
    (void)key;
    (void)klen;
    ocsp_get_meta(&stat, &valid, ctx->reg, ostat, ctx->p);
    switch (stat) {
        case MD_OCSP_CERT_ST_GOOD: ++ctx->good; break;
        case MD_OCSP_CERT_ST_REVOKED: ++ctx->revoked; break;
        case MD_OCSP_CERT_ST_UNKNOWN: ++ctx->unknown; break;
    }
    return 1;
}

void  md_ocsp_get_summary(md_json_t **pjson, md_ocsp_reg_t *reg, apr_pool_t *p)
{
    md_json_t *json;
    ocsp_summary_ctx_t ctx;
    
    memset(&ctx, 0, sizeof(ctx));
    ctx.p = p;
    ctx.reg = reg;
    apr_hash_do(add_to_summary, &ctx, reg->hash);

    json = md_json_create(p);
    md_json_setl(ctx.good+ctx.revoked+ctx.unknown, json, MD_KEY_TOTAL, NULL);
    md_json_setl(ctx.good, json, MD_KEY_GOOD, NULL);
    md_json_setl(ctx.revoked, json, MD_KEY_REVOKED, NULL);
    md_json_setl(ctx.unknown, json, MD_KEY_UNKNOWN, NULL);
    *pjson = json;
}

static apr_status_t job_loadj(md_json_t **pjson, const char *name, 
                              md_ocsp_reg_t *reg, apr_pool_t *p)
{
    return md_store_load_json(reg->store, MD_SG_OCSP, name, MD_FN_JOB, pjson, p);
}

typedef struct {
    apr_pool_t *p;
    md_ocsp_reg_t *reg;
    apr_array_header_t *ostats;
} ocsp_status_ctx_t;

static md_json_t *mk_jstat(md_ocsp_status_t *ostat, md_ocsp_reg_t *reg, apr_pool_t *p)
{
    md_ocsp_cert_stat_t stat;
    md_timeperiod_t valid, renewal;
    md_json_t *json, *jobj;
    apr_status_t rv;
    
    json = md_json_create(p);
    md_json_sets(ostat->md_name, json, MD_KEY_DOMAIN, NULL);
    md_json_sets(ostat->hexid, json, MD_KEY_ID, NULL);
    ocsp_get_meta(&stat, &valid, reg, ostat, p);
    md_json_sets(md_ocsp_cert_stat_name(stat), json, MD_KEY_STATUS, NULL);
    md_json_sets(ostat->hex_sha256, json, MD_KEY_CERT, MD_KEY_SHA256_FINGERPRINT, NULL);
    md_json_sets(ostat->responder_url, json, MD_KEY_URL, NULL);
    md_json_set_timeperiod(&valid, json, MD_KEY_VALID, NULL);
    renewal = md_timeperiod_slice_before_end(&valid, &reg->renew_window);
    md_json_set_time(renewal.start, json, MD_KEY_RENEW_AT, NULL);
    if ((MD_OCSP_CERT_ST_UNKNOWN == stat) || renewal.start < apr_time_now()) {
        /* We have no answer yet, or it should be in renew now. Add job information */
        rv = job_loadj(&jobj, ostat->md_name, reg, p);
        if (APR_SUCCESS == rv) {
            md_json_setj(jobj, json, MD_KEY_RENEWAL, NULL);
        }
    }
    return json;
}

static int add_ostat(void *baton, const void *key, apr_ssize_t klen, const void *val)
{
    ocsp_status_ctx_t *ctx = baton;
    const md_ocsp_status_t *ostat = val;
    
    (void)key;
    (void)klen;
    APR_ARRAY_PUSH(ctx->ostats, const md_ocsp_status_t*) = ostat;
    return 1;
}

static int md_ostat_cmp(const void *v1, const void *v2)
{
    int n;
    n = strcmp((*(md_ocsp_status_t**)v1)->md_name, (*(md_ocsp_status_t**)v2)->md_name);
    if (!n) {
        n = strcmp((*(md_ocsp_status_t**)v1)->hexid, (*(md_ocsp_status_t**)v2)->hexid);
    }
    return n;
}

void md_ocsp_get_status_all(md_json_t **pjson, md_ocsp_reg_t *reg, apr_pool_t *p)
{
    md_json_t *json;
    ocsp_status_ctx_t ctx;
    md_ocsp_status_t *ostat;
    int i;
    
    memset(&ctx, 0, sizeof(ctx));
    ctx.p = p;
    ctx.reg = reg;
    ctx.ostats = apr_array_make(p, (int)apr_hash_count(reg->hash), sizeof(md_ocsp_status_t*));
    json = md_json_create(p);
    
    apr_hash_do(add_ostat, &ctx, reg->hash);
    qsort(ctx.ostats->elts, (size_t)ctx.ostats->nelts, sizeof(md_json_t*), md_ostat_cmp);
    
    for (i = 0; i < ctx.ostats->nelts; ++i) {
        ostat = APR_ARRAY_IDX(ctx.ostats, i, md_ocsp_status_t*);
        md_json_addj(mk_jstat(ostat, reg, p), json, MD_KEY_OCSPS, NULL);
    }
    *pjson = json;
}

void md_ocsp_set_notify_cb(md_ocsp_reg_t *ocsp, md_job_notify_cb *cb, void *baton)
{
    ocsp->notify = cb;
    ocsp->notify_ctx = baton;
}

md_job_t *md_ocsp_job_make(md_ocsp_reg_t *ocsp, const char *mdomain, apr_pool_t *p)
{
    md_job_t *job;
    
    job = md_job_make(p, ocsp->store, MD_SG_OCSP, mdomain);
    md_job_set_notify_cb(job, ocsp->notify, ocsp->notify_ctx);
    return job;
}
