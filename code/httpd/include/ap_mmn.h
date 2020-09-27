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

/**
 * @file  ap_mmn.h
 * @brief Module Magic Number
 *
 * @defgroup APACHE_CORE_MMN Module Magic Number
 * @ingroup  APACHE_CORE

 * #MODULE_MAGIC_NUMBER_MAJOR\n
 * Major API changes that could cause compatibility problems for older modules
 * such as structure size changes.  No binary compatibility is possible across
 * a change in the major version.
 *
 * #MODULE_MAGIC_NUMBER_MINOR\n
 * Minor API changes that do not cause binary compatibility problems.
 * Should be reset to 0 when upgrading MODULE_MAGIC_NUMBER_MAJOR.
 *
 * See the #AP_MODULE_MAGIC_AT_LEAST macro below for an example.
 * @{
 */

#ifndef APACHE_AP_MMN_H
#define APACHE_AP_MMN_H

/*
 * 20010224   (2.0.13-dev) MODULE_MAGIC_COOKIE reset to "AP20"
 * 20010523   (2.0.19-dev) bump for scoreboard structure reordering
 * 20010627   (2.0.19-dev) more API changes than I can count
 * 20010726   (2.0.22-dev) more big API changes
 * 20010808   (2.0.23-dev) dir d_is_absolute bit introduced, bucket changes, etc
 * 20010825   (2.0.25-dev) removed d_is_absolute, introduced map_to_storage hook
 * 20011002   (2.0.26-dev) removed 1.3-deprecated request_rec.content_language
 * 20011127   (2.0.29-dev) bump for postconfig hook change, and removal of
 *                         socket from connection record
 * 20011212   (2.0.30-dev) bump for new used_path_info member of request_rec
 * 20011218   (2.0.30-dev) bump for new sbh member of conn_rec, different
 *                         declarations for scoreboard, new parameter to
 *                         create_connection hook
 * 20020102   (2.0.30-dev) bump for changed type of limit_req_body in
 *                         core_dir_config
 * 20020109   (2.0.31-dev) bump for changed shm and scoreboard declarations
 * 20020111   (2.0.31-dev) bump for ETag fields added at end of cor_dir_config
 * 20020114   (2.0.31-dev) mod_dav changed how it asks its provider to fulfill
 *                         a GET request
 * 20020118   (2.0.31-dev) Input filtering split of blocking and mode
 * 20020127   (2.0.31-dev) bump for pre_mpm hook change
 * 20020128   (2.0.31-dev) bump for pre_config hook change
 * 20020218   (2.0.33-dev) bump for AddOutputFilterByType directive
 * 20020220   (2.0.33-dev) bump for scoreboard.h structure change
 * 20020302   (2.0.33-dev) bump for protocol_filter additions.
 * 20020306   (2.0.34-dev) bump for filter type renames.
 * 20020318   (2.0.34-dev) mod_dav's API for REPORT generation changed
 * 20020319   (2.0.34-dev) M_INVALID changed, plus new M_* methods for RFC 3253
 * 20020327   (2.0.35-dev) Add parameter to quick_handler hook
 * 20020329   (2.0.35-dev) bump for addition of freelists to bucket API
 * 20020329.1 (2.0.36)     minor bump for new arg to opt fn ap_cgi_build_command
 * 20020506   (2.0.37-dev) Removed r->boundary in request_rec.
 * 20020529   (2.0.37-dev) Standardized the names of some apr_pool_*_set funcs
 * 20020602   (2.0.37-dev) Bucket API change (metadata buckets)
 * 20020612   (2.0.38-dev) Changed server_rec->[keep_alive_]timeout to apr time
 * 20020625   (2.0.40-dev) Changed conn_rec->keepalive to an enumeration
 * 20020628   (2.0.40-dev) Added filter_init to filter registration functions
 * 20020903   (2.0.41-dev) APR's error constants changed
 * 20020903.1 (2.1.0-dev)  allow_encoded_slashes added to core_dir_config
 * 20020903.2 (2.0.46-dev) add ap_escape_logitem
 * 20030213.1 (2.1.0-dev)  changed log_writer optional fn's to return previous
 *                         handler
 * 20030821   (2.1.0-dev)  bumped mod_include's entire API
 * 20030821.1 (2.1.0-dev)  added XHTML doctypes
 * 20030821.2 (2.1.0-dev)  added ap_escape_errorlog_item
 * 20030821.3 (2.1.0-dev)  added ap_get_server_revision / ap_version_t
 * 20040425   (2.1.0-dev)  removed ap_add_named_module API
 *                         changed ap_add_module, ap_add_loaded_module,
 *                         ap_setup_prelinked_modules,
 *                         ap_process_resource_config
 * 20040425.1 (2.1.0-dev)  Added ap_module_symbol_t and
 *                         ap_prelinked_module_symbols
 * 20050101.0 (2.1.2-dev)  Axed misnamed http_method for http_scheme
 *                         (which it was!)
 * 20050127.0 (2.1.3-dev)  renamed regex_t->ap_regex_t,
 *                         regmatch_t->ap_regmatch_t, REG_*->AP_REG_*,
 *                         removed reg* in place of ap_reg*; added ap_regex.h
 * 20050217.0 (2.1.3-dev)  Axed find_child_by_pid, mpm_*_completion_context
 *                         (winnt mpm) symbols from the public sector, and
 *                         decorated real_exit_code with ap_ in the win32/os.h.
 * 20050305.0 (2.1.4-dev)  added pid and generation fields to worker_score
 * 20050305.1 (2.1.5-dev)  added ap_vhost_iterate_given_conn.
 * 20050305.2 (2.1.5-dev)  added AP_INIT_TAKE_ARGV.
 * 20050305.3 (2.1.5-dev)  added Protocol Framework.
 * 20050701.0 (2.1.7-dev)  Bump MODULE_MAGIC_COOKIE to "AP21"!
 * 20050701.1 (2.1.7-dev)  trace_enable member added to core server_config
 * 20050708.0 (2.1.7-dev)  Bump MODULE_MAGIC_COOKIE to "AP22"!
 * 20050708.1 (2.1.7-dev)  add proxy request_status hook (minor)
 * 20050919.0 (2.1.8-dev)  mod_ssl ssl_ext_list optional function added
 * 20051005.0 (2.1.8-dev)  NET_TIME filter eliminated
 * 20051005.0 (2.3.0-dev)  Bump MODULE_MAGIC_COOKIE to "AP24"!
 * 20051115.0 (2.3.0-dev)  Added use_canonical_phys_port to core_dir_config
 * 20060110.0 (2.3.0-dev)  Conversion of Authz to be provider based
 *                         addition of <SatisfyAll><SatisfyOne>
 *                         removal of Satisfy, Allow, Deny, Order
 * 20060110.1 (2.3.0-dev)  minex and minex_set members added to
 *                         cache_server_conf (minor)
 * 20060110.2 (2.3.0-dev)  flush_packets and flush_wait members added to
 *                         proxy_server (minor)
 * 20060110.3 (2.3.0-dev)  added inreslist member to proxy_conn_rec (minor)
 * 20060110.4 (2.3.0-dev)  Added server_scheme member to server_rec (minor)
 * 20060905.0 (2.3.0-dev)  Replaced ap_get_server_version() with
 *                         ap_get_server_banner() and ap_get_server_description()
 * 20060905.1 (2.3.0-dev)  Enable retry=0 for the worker (minor)
 * 20060905.2 (2.3.0-dev)  Added ap_all_available_mutexes_string,
 *                         ap_available_mutexes_string and
 *                         ap_parse_mutex()
 * 20060905.3 (2.3.0-dev)  Added conn_rec::clogging_input_filters.
 * 20060905.4 (2.3.0-dev)  Added proxy_balancer::sticky_path.
 * 20060905.5 (2.3.0-dev)  Added ap_mpm_safe_kill()
 * 20070823.0 (2.3.0-dev)  Removed ap_all_available_mutexes_string,
 *                         ap_available_mutexes_string for macros
 * 20070823.1 (2.3.0-dev)  add ap_send_interim_response()
 * 20071023.0 (2.3.0-dev)  add ap_get_scoreboard(sbh) split from the less
 *                         conventional ap_get_scoreboard(proc, thread)
 * 20071023.1 (2.3.0-dev)  Add flags field to struct proxy_alias
 * 20071023.2 (2.3.0-dev)  Add ap_mod_status_reqtail
 * 20071023.3 (2.3.0-dev)  Declare ap_time_process_request() as part of the
 *                         public scoreboard API.
 * 20071108.1 (2.3.0-dev)  Add the optional kept_body brigade to request_rec
 * 20071108.2 (2.3.0-dev)  Add st and keep fields to struct util_ldap_connection_t
 * 20071108.3 (2.3.0-dev)  Add API guarantee for adding connection filters
 *                         with non-NULL request_rec pointer (ap_add_*_filter*)
 * 20071108.4 (2.3.0-dev)  Add ap_proxy_ssl_connection_cleanup
 * 20071108.5 (2.3.0-dev)  Add *scpool to proxy_conn_rec structure
 * 20071108.6 (2.3.0-dev)  Add *r and need_flush to proxy_conn_rec structure
 * 20071108.7 (2.3.0-dev)  Add *ftp_directory_charset to proxy_dir_conf
 * 20071108.8 (2.3.0-dev)  Add optional function ap_logio_add_bytes_in() to mog_logio
 * 20071108.9 (2.3.0-dev)  Add chroot support to unixd_config
 * 20071108.10(2.3.0-dev)  Introduce new ap_expr API
 * 20071108.11(2.3.0-dev)  Revise/Expand new ap_expr API
 * 20071108.12(2.3.0-dev)  Remove ap_expr_clone from the API (same day it was added)
 * 20080403.0 (2.3.0-dev)  Add condition field to core dir config
 * 20080403.1 (2.3.0-dev)  Add authn/z hook and provider registration wrappers.
 * 20080403.2 (2.3.0-dev)  Add ap_escape_path_segment_buffer() and ap_unescape_all().
 * 20080407.0 (2.3.0-dev)  Remove ap_graceful_stop_signalled.
 * 20080407.1              Deprecate ap_cache_cacheable_hdrs_out and add two
 *                         generalized ap_cache_cacheable_headers_(in|out).
 * 20080528.0 (2.3.0-dev)  Switch order of ftp_directory_charset and
 *                         interpolate_env in proxy_dir_conf.
 *                         Rationale: see r661069.
 * 20080528.1 (2.3.0-dev)  add has_realm_hash() to authn_provider struct
 * 20080722.0 (2.3.0-dev)  remove has_realm_hash() from authn_provider struct
 * 20080722.1 (2.3.0-dev)  Add conn_timeout and conn_timeout_set to
 *                         proxy_worker struct.
 * 20080722.2 (2.3.0-dev)  Add scolonsep to proxy_balancer
 * 20080829.0 (2.3.0-dev)  Add cookie attributes when removing cookies
 * 20080830.0 (2.3.0-dev)  Cookies can be set on headers_out and err_headers_out
 * 20080920.0 (2.3.0-dev)  Add ap_mpm_register_timed_callback.
 * 20080920.1 (2.3.0-dev)  Export mod_rewrite.h in the public API.
 * 20080920.2 (2.3.0-dev)  Added ap_timeout_parameter_parse to util.c / httpd.h
 * 20081101.0 (2.3.0-dev)  Remove unused AUTHZ_GROUP_NOTE define.
 * 20081102.0 (2.3.0-dev)  Remove authz_provider_list, authz_request_state,
 *                         and AUTHZ_ACCESS_PASSED_NOTE.
 * 20081104.0 (2.3.0-dev)  Remove r and need_flush fields from proxy_conn_rec
 *                         as they are no longer used and add
 *                         ap_proxy_buckets_lifetime_transform to mod_proxy.h
 * 20081129.0 (2.3.0-dev)  Move AP_FILTER_ERROR and AP_NOBODY_READ|WROTE
 *                         from util_filter.h to httpd.h and change their
 *                         numeric values so they do not overlap with other
 *                         potential status codes
 * 20081201.0 (2.3.0-dev)  Rename several APIs to include ap_ prefix.
 * 20081201.1 (2.3.0-dev)  Added ap_args_to_table and ap_body_to_table.
 * 20081212.0 (2.3.0-dev)  Remove sb_type from process_score in scoreboard.h.
 * 20081231.0 (2.3.0-dev)  Switch ap_escape_html API: add ap_escape_html2,
 *                         and make ap_escape_html a macro for it.
 * 20090130.0 (2.3.2-dev)  Add ap_ prefix to unixd_setup_child().
 * 20090131.0 (2.3.2-dev)  Remove ap_default_type(), disable DefaultType
 * 20090208.0 (2.3.2-dev)  Add conn_rec::current_thread.
 * 20090208.1 (2.3.3-dev)  Add ap_retained_data_create()/ap_retained_data_get()
 * 20090401.0 (2.3.3-dev)  Remove ap_threads_per_child, ap_max_daemons_limit,
 *                         ap_my_generation, etc.  ap_mpm_query() can't be called
 *                         until after the register-hooks phase.
 * 20090401.1 (2.3.3-dev)  Protected log.c internals, http_log.h changes
 * 20090401.2 (2.3.3-dev)  Added tmp_flush_bb to core_output_filter_ctx_t
 * 20090401.3 (2.3.3-dev)  Added DAV options provider to mod_dav.h
 * 20090925.0 (2.3.3-dev)  Added server_rec::context and added *server_rec
 *                         param to ap_wait_or_timeout()
 * 20090925.1 (2.3.3-dev)  Add optional function ap_logio_get_last_bytes() to
 *                         mod_logio
 * 20091011.0 (2.3.3-dev)  Move preserve_host{,_set} from proxy_server_conf to
 *                         proxy_dir_conf
 * 20091011.1 (2.3.3-dev)  add debug_level to util_ldap_state_t
 * 20091031.0 (2.3.3-dev)  remove public LDAP referral-related macros
 * 20091119.0 (2.3.4-dev)  dav_error interface uses apr_status_t parm, not errno
 * 20091119.1 (2.3.4-dev)  ap_mutex_register(), ap_{proc,global}_mutex_create()
 * 20091229.0 (2.3.5-dev)  Move allowed_connect_ports from proxy_server_conf
 *                         to mod_proxy_connect
 * 20091230.0 (2.3.5-dev)  Move ftp_directory_charset from proxy_dir_conf
 *                         to proxy_ftp_dir_conf(mod_proxy_ftp)
 * 20091230.1 (2.3.5-dev)  add util_ldap_state_t.opTimeout
 * 20091230.2 (2.3.5-dev)  add ap_get_server_name_for_url()
 * 20091230.3 (2.3.6-dev)  add ap_parse_log_level()
 * 20091230.4 (2.3.6-dev)  export ap_process_request_after_handler() for mod_serf
 * 20100208.0 (2.3.6-dev)  ap_socache_provider_t API changes to store and iterate
 * 20100208.1 (2.3.6-dev)  Added forward member to proxy_conn_rec
 * 20100208.2 (2.3.6-dev)  Added ap_log_command_line().
 * 20100223.0 (2.3.6-dev)  LDAP client_certs per-server moved to per-dir
 * 20100223.1 (2.3.6-dev)  Added ap_process_fnmatch_configs().
 * 20100504.0 (2.3.6-dev)  Added name arg to ap_{proc,global}_mutex_create().
 * 20100604.0 (2.3.6-dev)  Remove unused core_dir_config::loglevel
 * 20100606.0 (2.3.6-dev)  Make ap_log_*error macro wrappers around
 *                         ap_log_*error_ to save argument preparation and
 *                         function call overhead.
 *                         Introduce per-module loglevels, including new APIs
 *                         APLOG_USE_MODULE() and AP_DECLARE_MODULE().
 * 20100606.1 (2.3.6-dev)  Added extended timestamp formatting via
 *                         ap_recent_ctime_ex().
 * 20100609.0 (2.3.6-dev)  Dropped ap_body_to_table due to missing constraints.
 * 20100609.1 (2.3.7-dev)  Introduce ap_log_cserror()
 * 20100609.2 (2.3.7-dev)  Add deferred write pool to core_output_filter_ctx
 * 20100625.0 (2.3.7-dev)  Add 'userctx' to socache iterator callback prototype
 * 20100630.0 (2.3.7-dev)  make module_levels vector of char instead of int
 * 20100701.0 (2.3.7-dev)  re-order struct members to improve alignment
 * 20100701.1 (2.3.7-dev)  add note_auth_failure hook
 * 20100701.2 (2.3.7-dev)  add ap_proxy_*_wid() functions
 * 20100714.0 (2.3.7-dev)  add access_checker_ex hook, add AUTHZ_DENIED_NO_USER
 *                         to authz_status, call authz providers twice to allow
 *                         authz without authenticated user
 * 20100719.0 (2.3.7-dev)  Add symbol name parameter to ap_add_module and
 *                         ap_add_loaded_module. Add ap_find_module_short_name
 * 20100723.0 (2.3.7-dev)  Remove ct_output_filters from core rec
 * 20100723.1 (2.3.7-dev)  Added ap_proxy_hashfunc() and hash elements to
 *                         proxy worker structs
 * 20100723.2 (2.3.7-dev)  Add ap_request_has_body()
 * 20100723.3 (2.3.8-dev)  Add ap_check_mpm()
 * 20100905.0 (2.3.9-dev)  Add log_id to conn and req recs. Add error log
 *                         format handlers. Support AP_CTIME_OPTION_COMPACT in
 *                         ap_recent_ctime_ex().
 * 20100905.1 (2.3.9-dev)  Add ap_cache_check_allowed()
 * 20100912.0 (2.3.9-dev)  Add an additional "out" brigade parameter to the
 *                         mod_cache store_body() provider function.
 * 20100916.0 (2.3.9-dev)  Add commit_entity() to the mod_cache provider
 *                         interface.
 * 20100918.0 (2.3.9-dev)  Move the request_rec within mod_include to be
 *                         exposed within include_ctx_t.
 * 20100919.0 (2.3.9-dev)  Authz providers: Add parsed_require_line parameter
 *                         to check_authorization() function. Add
 *                         parse_require_line() function.
 * 20100919.1 (2.3.9-dev)  Introduce ap_rxplus util/API
 * 20100921.0 (2.3.9-dev)  Add an apr_bucket_brigade to the create_entity
 *                         provider interface for mod_cache.h.
 * 20100922.0 (2.3.9-dev)  Move cache_* functions from mod_cache.h to a
 *                         private header file.
 * 20100923.0 (2.3.9-dev)  Remove MOD_CACHE_REQUEST_REC, remove deprecated
 *                         ap_cache_cacheable_hdrs_out, trim cache_object_t,
 *                         make ap_cache_accept_headers, ap_cache_accept_headers
 *                         ap_cache_try_lock, ap_cache_check_freshness,
 *                         cache_server_conf, cache_enable, cache_disable,
 *                         cache_request_rec and cache_provider_list private.
 * 20100923.1 (2.3.9-dev)  Add cache_status hook.
 * 20100923.2 (2.3.9-dev)  Add generate_log_id hook.
 *                         Make root parameter of ap_expr_eval() const.
 * 20100923.3 (2.3.9-dev)  Add "last" member to ap_directive_t
 * 20101012.0 (2.3.9-dev)  Add header to cache_status hook.
 * 20101016.0 (2.3.9-dev)  Remove ap_cache_check_allowed().
 * 20101017.0 (2.3.9-dev)  Make ap_cache_control() public, add cache_control_t
 *                         to mod_disk_cache format.
 * 20101106.0 (2.3.9-dev)  Replace the ap_expr parser derived from
 *                         mod_include's parser with one derived from
 *                         mod_ssl's parser. Clean up ap_expr's public
 *                         interface.
 * 20101106.1 (2.3.9-dev)  Add ap_pool_cleanup_set_null() generic cleanup
 * 20101106.2 (2.3.9-dev)  Add suexec_disabled_reason field to ap_unixd_config
 * 20101113.0 (2.3.9-dev)  Add source address to mod_proxy.h
 * 20101113.1 (2.3.9-dev)  Add ap_set_flag_slot_char()
 * 20101113.2 (2.3.9-dev)  Add ap_expr_exec_re()
 * 20101204.0 (2.3.10-dev) Add _t to ap_expr's typedef names
 * 20101223.0 (2.3.11-dev) Remove cleaned from proxy_conn_rec.
 * 20101223.1 (2.3.11-dev) Rework mod_proxy, et.al. Remove proxy_worker_stat
 *                         and replace w/ proxy_worker_shared; remove worker
 *                         info from scoreboard and use slotmem; Allow
 *                         dynamic growth of balancer members; Remove
 *                         BalancerNonce in favor of 'nonce' parameter.
 * 20110117.0 (2.3.11-dev) Merge <If> sections in separate step (ap_if_walk).
 *                         Add core_dir_config->sec_if. Add ap_add_if_conf().
 *                         Add pool argument to ap_add_file_conf().
 * 20110117.1 (2.3.11-dev) Add ap_pstr2_alnum() and ap_str2_alnum()
 * 20110203.0 (2.3.11-dev) Raise DYNAMIC_MODULE_LIMIT to 256
 * 20110203.1 (2.3.11-dev) Add ap_state_query()
 * 20110203.2 (2.3.11-dev) Add ap_run_pre_read_request() hook and
 *                         ap_parse_form_data() util
 * 20110312.0 (2.3.12-dev) remove uldap_connection_cleanup and add
                           util_ldap_state_t.connectionPoolTTL,
                           util_ldap_connection_t.freed, and
                           util_ldap_connection_t.rebind_pool.
 * 20110312.1 (2.3.12-dev) Add core_dir_config.decode_encoded_slashes.
 * 20110328.0 (2.3.12-dev) change type and name of connectionPoolTTL in util_ldap_state_t
                           connectionPoolTTL (connection_pool_ttl, int->apr_interval_t)
 * 20110329.0 (2.3.12-dev) Change single-bit signed fields to unsigned in
 *                         proxy and cache interfaces.
 *                         Change ap_configfile_t/ap_cfg_getline()/
 *                         ap_cfg_getc() API, add ap_pcfg_strerror()
 *                         Axe mpm_note_child_killed hook, change
 *                         ap_reclaim_child_process and ap_recover_child_process
 *                         interfaces.
 * 20110329.1 (2.3.12-dev) Add ap_reserve_module_slots()/ap_reserve_module_slots_directive()
 *                         change AP_CORE_DECLARE to AP_DECLARE: ap_create_request_config()
 *                         change AP_DECLARE to AP_CORE_DECLARE: ap_register_log_hooks()
 * 20110329.2 (2.3.12-dev) Add child_status and end_generation hooks.
 * 20110329.3 (2.3.12-dev) Add format field to ap_errorlog_info.
 * 20110329.4 (2.3.13-dev) bgrowth and max_balancers to proxy_server_conf.
 * 20110329.5 (2.3.13-dev) Add ap_regexec_len()
 * 20110329.6 (2.3.13-dev) Add AP_EXPR_FLAGS_RESTRICTED, ap_expr_eval_ctx_t->data,
 *                         ap_expr_exec_ctx()
 * 20110604.0 (2.3.13-dev) Make ap_rputs() inline
 * 20110605.0 (2.3.13-dev) add core_dir_config->condition_ifelse, change return
 *                         type of ap_add_if_conf().
 *                         Add members of core_request_config: document_root,
 *                         context_document_root, context_prefix.
 *                         Add ap_context_*(), ap_set_context_info(), ap_set_document_root()
 * 20110605.1 (2.3.13-dev) add ap_(get|set)_core_module_config()
 * 20110605.2 (2.3.13-dev) add ap_get_conn_socket()
 * 20110619.0 (2.3.13-dev) add async connection infos to process_score in scoreboard,
 *                         add ap_start_lingering_close(),
 *                         add conn_state_e:CONN_STATE_LINGER_NORMAL and CONN_STATE_LINGER_SHORT
 * 20110619.1 (2.3.13-dev) add ap_str_toupper()
 * 20110702.0 (2.3.14-dev) make ap_expr_parse_cmd() macro wrapper for new
 *                         ap_expr_parse_cmd_mi() function, add ap_expr_str_*() functions,
 *                         rename AP_EXPR_FLAGS_* -> AP_EXPR_FLAG_*
 * 20110702.1 (2.3.14-dev) Add ap_scan_script_header_err*_ex functions
 * 20110723.0 (2.3.14-dev) Revert addition of ap_ldap*
 * 20110724.0 (2.3.14-dev) Add override_list as parameter to ap_parse_htaccess
 *                         Add member override_list to cmd_parms_struct,
 *                         core_dir_config and htaccess_result
 * 20110724.1 (2.3.15-dev) add NOT_IN_HTACCESS
 * 20110724.2 (2.3.15-dev) retries and retry_delay in util_ldap_state_t
 * 20110724.3 (2.3.15-dev) add util_varbuf.h / ap_varbuf API
 * 20110724.4 (2.3.15-dev) add max_ranges to core_dir_config
 * 20110724.5 (2.3.15-dev) add ap_set_accept_ranges()
 * 20110724.6 (2.3.15-dev) add max_overlaps and max_reversals to core_dir_config
 * 20110724.7 (2.3.15-dev) add ap_random_insecure_bytes(), ap_random_pick()
 * 20110724.8 (2.3.15-dev) add ap_abort_on_oom(), ap_malloc(), ap_calloc(),
 *                         ap_realloc()
 * 20110724.9 (2.3.15-dev) add ap_varbuf_pdup() and ap_varbuf_regsub()
 * 20110724.10(2.3.15-dev) Export ap_max_mem_free
 * 20111009.0 (2.3.15-dev) Remove ap_proxy_removestr(),
 *                         add ap_unixd_config.group_name
 * 20111014.0 (2.3.15-dev) Remove cookie_path_str and cookie_domain_str from
 *                         proxy_dir_conf
 * 20111025.0 (2.3.15-dev) Add return value and maxlen to ap_varbuf_regsub(),
 *                         add ap_pregsub_ex()
 * 20111025.1 (2.3.15-dev) Add ap_escape_urlencoded(), ap_escape_urlencoded_buffer()
 *                         and ap_unescape_urlencoded().
 * 20111025.2 (2.3.15-dev) Add ap_lua_ssl_val to mod_lua
 * 20111118.0 (2.5.0-dev)  Add conn_rec to error_log hook
 * 20111118.1 (2.5.0-dev)  Add reclvl to ap_expr_eval_ctx_t
 * 20111120.0 (2.5.0-dev)  Remove parts of conn_state_t that are private to the MPM
 * 20111121.0 (2.5.0-dev)  Pass ap_errorlog_info struct to error_log hook,
 *                         add pool to ap_errorlog_info.
 * 20111125.0 (2.5.0-dev)  c->remote_ip becomes c->peer_ip and r->client_ip,
 *                         c->remote_addr becomes c->peer_addr and r->client_addr
 * 20111201.0 (2.5.0-dev)  Add invalidate_entity() to the cache provider.
 * 20111202.0 (2.5.0-dev)  Use apr_status_t across mod_session API.
 * 20111202.1 (2.5.0-dev)  add APLOGNO()
 * 20111203.0 (2.5.0-dev)  Optional ap_proxy_retry_worker(), remove
 *                         ap_proxy_string_read(), ap_cache_liststr(),
 *                         ap_proxy_buckets_lifetime_transform(),
 *                         ap_proxy_date_canon(), ap_proxy_is_ipaddr(),
 *                         ap_proxy_is_domainname(), ap_proxy_is_hostname(),
 *                         ap_proxy_is_word(), ap_proxy_hex2sec(),
 *                         ap_proxy_sec2hex(), ap_proxy_make_fake_req(),
 *                         ap_proxy_strmatch_path, ap_proxy_strmatch_domain,
 *                         ap_proxy_table_unmerge(), proxy_lb_workers.
 * 20111203.1 (2.5.0-dev)  Add ap_list_provider_groups()
 * 20120109.0 (2.5.0-dev)  Changes sizeof(overrides_t) in core config.
 * 20120111.0 (2.5.0-dev)  Remove sb_type from global_score.
 * 20120123.0 (2.5.0-dev)  Make core_output_filter_ctx_t and core_ctx_t
 *                         private, add ap_create_core_ctx(),
 *                         ap_core_ctx_get_bb(), move core_net rec definition
 *                         to http_core.h
 * 20120201.0 (2.5.0-dev)  Bump MODULE_MAGIC_COOKIE to "AP25"!
 * 20120204.0 (2.5.0-dev)  Remove ap_create_core_ctx(), ap_core_ctx_get_bb();
 *                         add insert_network_bucket hook, AP_DECLINED
 * 20120211.0 (2.5.0-dev)  Change re_nsub in ap_regex_t from apr_size_t to int.
 * 20120211.1 (2.5.0-dev)  Add ap_palloc_debug, ap_pcalloc_debug
 * 20120211.2 (2.5.0-dev)  Add ap_runtime_dir_relative
 * 20120211.3 (2.5.0-dev)  Add forcerecovery to proxy_balancer_shared struct
 * 20120211.4 (2.5.0-dev)  Add missing HTTP status codes registered with IANA.
 * 20120724.0 (2.5.0-dev)  Add hostname argument to ap_proxy_checkproxyblock.
 * 20120724.1 (2.5.0-dev)  Add post_perdir_config hook.
 * 20120724.2 (2.5.0-dev)  Add fgrab slotmem function to struct
 * 20120724.3 (2.5.0-dev)  Add bal_persist, inherit to proxy_server_conf
 * 20120724.4 (2.5.0-dev)  Add dirwalk_stat hook.
 * 20120724.5 (2.5.0-dev)  Add ap_get_sload() and ap_get_loadavg().
 * 20120724.6 (2.5.0-dev)  Add sticky_separator to proxy_balancer_shared struct.
 * 20120724.7 (2.5.0-dev)  Add min_http_version/max_http_version to
 *                         core_server_config
 * 20120724.8 (2.5.0-dev)  Add conn_log_level to core_server_config
 * 20120724.9 (2.5.0-dev)  Add ap_bin2hex()
 * 20121222.0 (2.5.0-dev)  Remove min_http_version/max_http_version from
 *                         core_server_config again, add http09_enable
 * 20121222.1 (2.5.0-dev)  Add http_conformance to core_server_config,
 *                         add ap_has_cntrl()
 * 20121222.2 (2.5.0-dev)  Add ap_password_validate()
 * 20121222.3 (2.5.0-dev)  Add ppinherit to proxy_server_conf
 * 20121222.4 (2.5.0-dev)  Add uds_path to proxy_conn_rec
 * 20121222.5 (2.5.0-dev)  Add "r" and "must_rebind" to util_ldap_connection_t
 * 20121222.6 (2.5.0-dev)  Add ap_proxy_create_hdrbrgd() and
 *                         ap_proxy_pass_brigade()
 * 20121222.7 (2.5.0-dev)  Add ap_remove_input|output_filter_byhandle()
 * 20121222.8 (2.5.0-dev)  Add dav_join_error()
 * 20121222.9 (2.5.0-dev)  Add conn_sense_e
 * 20121222.10 (2.5.0-dev) Add cache_control_t.invalidated
 * 20121222.11 (2.5.0-dev) Add ap_find_etag_weak(), ap_find_etag_strong()
 * 20121222.12 (2.5.0-dev) Add ap_condition_e, ap_condition_if_match(),
 *                         ap_condition_if_unmodified_since(),
 *                         ap_condition_if_none_match(),
 *                         ap_condition_if_modified_since(),
 *                         ap_condition_if_range()
 * 20121222.13 (2.5.0-dev) Add ap_proxy_clear_connection()
 * 20121222.14 (2.5.0-dev) Add ap_map_http_request_error()
 * 20121222.15 (2.5.0-dev) Add allow/decode_encoded_slashes_set to core_dir_config
 * 20121222.16 (2.5.0-dev) AP_DEFAULT_HANDLER_NAME/AP_IS_DEFAULT_HANDLER_NAME
 * 20130702.0 (2.5.0-dev)  Remove pre_htaccess hook, add open_htaccess hook.
 * 20130702.1 (2.5.0-dev)  Restore AUTH_HANDLED to mod_auth.h
 * 20130702.2 (2.5.0-dev)  Add ap_log_data(), ap_log_rdata(), etc.
 * 20130702.3 (2.5.0-dev)  Add util_fcgi.h, FastCGI protocol support
 * 20130903.0 (2.5.0-dev)  Changes sizeof(worker_score) in scoreboard 
 * 20130924.0 (2.5.0-dev)  Add ap_errorlog_provider
 * 20130924.1 (2.5.0-dev)  Add ap_proxy_connection_reusable()
 * 20131112.0 (2.5.0-dev)  Add parse_errorlog_arg to ap_errorlog_provider
 * 20131112.1 (2.5.0-dev)  Add suspend_connection and resume_connection hooks
 * 20131112.2 (2.5.0-dev)  Add ap_regname
 * 20131230.0 (2.5.0-dev)  cl_head_zero & expect_strict added to server_config
 * 20131230.1 (2.5.0-dev)  REWRITE_REDIRECT_HANDLER_NAME in mod_rewrite.h
 * 20131230.2 (2.5.0-dev)  Prefix REWRITE_REDIRECT_HANDLER_NAME in mod_rewrite.h
 * 20140207.0 (2.5.0-dev)  Support for slaved connections in core.c
 * 20140207.1 (2.5.0-dev)  Add SSL reusable SNI to mod_proxy.h's proxy_conn_rec
 * 20140207.2 (2.5.0-dev)  Add proxy detach_backend hook
 * 20140207.3 (2.5.0-dev)  Add mod_ssl_openssl.h and OpenSSL-specific hooks
 * 20140207.4 (2.5.0-dev)  add ap_mpm_register_socket_callback_timeout
 * 20140207.5 (2.5.0-dev)  Add ap_mpm_resume_suspended(), AP_MPMQ_CAN_SUSPEND to
 *                         ap_mpm_query(), and suspended_baton to conn_rec
 * 20140207.6 (2.5.0-dev)  Added ap_log_mpm_common().
 * 20140207.7 (2.5.0-dev)  Added ap_force_set_tz().
 * 20140207.8 (2.5.0-dev)  Added ap_shutdown_conn().
 * 20140611.0 (2.5.0-dev)  Add ap_mpm_socket_callback_fn_t.
                           Changes 3rd argument's type of
                           ap_mpm_register_socket_callback and 
                           ap_mpm_register_socket_callback_timeout.
 * 20140611.1 (2.5.0-dev)  Add ap_proxy_connect_uds().
 * 20140627.0 (2.5.0-dev)  Revert 20140611.0 change.
 * 20140627.1 (2.5.0-dev)  add last_backend_conn to util_ldap_connection_t
 * 20140627.2 (2.5.0-dev)  Added is_name_matchable to proxy_worker_shared.
                           Added ap_proxy_define_match_worker().
 * 20140627.3 (2.5.0-dev)  Add ap_copy_scoreboard_worker()
 * 20140627.4 (2.5.0-dev)  Added ap_parse_token_list_strict() to httpd.h.
 * 20140627.5 (2.5.0-dev)  Add r->trailers_{in,out}
 * 20140627.6 (2.5.0-dev)  Added ap_pcre_version_string(), AP_REG_PCRE_COMPILED
 *                         and AP_REG_PCRE_LOADED to ap_regex.h.
 * 20140627.7 (2.5.0-dev)  Add listener bucket in scoreboard.h's process_score.
 * 20140627.8 (2.5.0-dev)  Add ap_set_listencbratio(), ap_close_listeners_ex(),
 *                         ap_duplicate_listeners(), ap_num_listen_buckets and
 *                         ap_have_so_reuseport to ap_listen.h.
 * 20140627.9 (2.5.0-dev)  Add cgi_pass_auth and AP_CGI_PASS_AUTH_* to 
 *                         core_dir_config
 * 20140627.10 (2.5.0-dev) Add ap_proxy_de_socketfy to mod_proxy.h
 * 20150121.0 (2.5.0-dev)  Revert field addition from core_dir_config; r1653666
 * 20150121.1 (2.5.0-dev)  Add cmd_parms_struct.parent to http_config.h
 * 20150121.2 (2.5.0-dev)  Add response_code_exprs to http_core.h
 * 20150222.0 (2.5.0-dev)  ssl pre_handshake hook now indicates proxy|client
 * 20150222.1 (2.5.0-dev)  Add keep_alive_timeout_set to server_rec
 * 20150222.2 (2.5.0-dev)  Add response code 418 as per RFC2324/RFC7168
 * 20150222.3 (2.5.0-dev)  Add ap_some_authn_required, ap_force_authn hook.
 *                         Deprecate broken ap_some_auth_required.
 * 20150222.4 (2.5.0-dev)  Add protocols and protocols_honor_order to
 *                         core_server_config. Add hooks protocol_propose
 *                         protocol_switch and protocol_get. Add
 *                         ap_select_protocol(), ap_switch_protocol(),
 *                         ap_get_protocol(). Add HTTP_MISDIRECTED_REQUEST.
 * 20150222.5 (2.5.0-dev)  Add ap_request_core_filter(),
 *                         ap_filter_setaside_brigade(),
 *                         ap_filter_reinstate_brigade() and
 *                         ap_filter_should_yield(). Add empty and filters to
 *                         conn_rec.
 * 20150222.6 (2.5.0-dev)  Add async_filter to conn_rec.
 * 20150222.7 (2.5.0-dev)  Add ap_casecmpstr[n]();
 * 20150222.8 (2.5.0-dev)  Add ap_getword_conf2[_nc]();
 * 20150222.9 (2.5.0-dev)  Add expr_handler to core_dir_config.
 * 20150222.10 (2.5.0-dev) Add new ap_update_child_status...() methods,
 *                         add protocol to worker_score in scoreboard.h,
 *                         Add pre_close connection hook and 
 *                         ap_prep_lingering_close().
 * 20150222.11 (2.5.0-dev) Split useragent_host from the conn_rec into
 *                         the request_rec, with ap_get_useragent_host()
 * 20150222.12 (2.5.0-dev) Add complete_connection hook,
 *                         ap_filter_complete_connection().
 * 20150222.13 (2.5.0-dev) Add ap_create_request().
 * 20160312.0 (2.5.0-dev)  Rename complete_connection to output_pending,
 *                         add ap_filter_input_pending(),
 *                         ap_filter_prepare_brigade(), ap_filter_direction_e
 * 20160312.1 (2.5.0-dev)  Add cgi_var_rules to core_dir_config.
 * 20160315.0 (2.5.0-dev)  Rename ap_mpm_register_socket_callback,
 *                         ap_mpm_register_socket_callback_timeout and
 *                         ap_mpm_unregister_socket_callback to
 *                         ap_mpm_register_poll_callback,
 *                         ap_mpm_register_poll_callback_timeout and
 *                         ap_mpm_unregister_poll_callback. Add
 *                         AP_MPMQ_CAN_POLL.
 * 20160315.1 (2.5.0-dev)  Add AP_IMPLEMENT_OPTIONAL_HOOK_RUN_FIRST.
 * 20160315.2 (2.5.0-dev)  Add optional proxy_{hook,run}_section_post_config(),
 *                         ap_proxy_connection_create_ex() and section_config
 *                         to struct proxy_{worker,balancer} in mod_proxy.h,
 *                         and optional ssl_engine_set() to mod_ssl.h.
 * 20160315.3 (2.5.0-dev)  Add childtags to dav_error.
 * 20160608.1 (2.5.0-dev)  Rename ap_casecmpstr[n]() to ap_cstr_casecmp[n]()
 * 20160608.2 (2.5.0-dev)  Add dav_get_provider_name.
 * 20160608.3 (2.5.0-dev)  Add dav_begin_multistatus, dav_send_one_response,
 *                         dav_finish_multistatus, dav_send_multistatus,
 *                         dav_handle_err, dav_failed_proppatch,
 *                         dav_success_proppatch.
 * 20160608.4 (2.5.0-dev)  Add dav_acl_provider, dav_acl_provider_register
 *                         dav_get_acl_providers.
 * 20160608.5 (2.5.0-dev)  Add ap_proxy_check_backend(), and tmp_bb to
 *                         struct proxy_conn_rec.
 * 20160608.6 (2.5.0-dev)  Add ap_scan_http_field_content, ap_scan_http_token
 *                         and ap_get_http_token
 * 20160608.7 (2.5.0-dev)  Add ap_check_pipeline().
 * 20160608.8 (2.5.0-dev)  Rename ap_proxy_check_backend() to
 *                         ap_proxy_check_connection().
 * 20160608.9 (2.5.0-dev)  Renamed AP_HTTP_CONFORMANCE_LIBERAL to 
 *                         AP_HTTP_CONFORMANCE_UNSAFE, and
 *                         eliminated AP_HTTP_CONFORMANCE_LOGONLY
 * 20160617.1 (2.5.0-dev)  Added http_whitespace and http_methods to
 *                         core_server_config
 * 20160629.1 (2.5.0-dev)  Dropped http_whitespace from core_server_config
 * 20160629.2 (2.5.0-dev)  Replaced fold w/multiple flags for ap_[r]getline()
 * 20161018.1 (2.5.0-dev)  Dropped ap_has_cntrls(), ap_scan_http_uri_safe(),
 *                         ap_get_http_token() and http_stricturi conf member.
 *                         Added ap_scan_vchar_obstext()
 * 20161018.2 (2.5.0-dev)  add ap_set_conn_count()
 * 20161018.3 (2.5.0-dev)  add ap_exists_directive()
 * 20161018.4 (2.5.0-dev)  Add taint to request_rec and ap_request_tainted()
 * 20161018.5 (2.5.0-dev)  Add ap_get_basic_auth_components() and deprecate
 *                         ap_get_basic_auth_pw()
 * 20161018.6 (2.5.0-dev)  Add ap_update_sb_handle()
 * 20161018.7 (2.5.0-dev)  Add flags field to module_struct and function
 *                         ap_get_module_flags()
 * 20171014.1 (2.5.0-dev)  Add NOT_IN_DIR_CONTEXT replacing NOT_IN_DIR_LOC_FILE
 *                         semantics
 * 20171014.2 (2.5.1-dev)  Add "use_specific_errors" to listen_rec and 
 *                         ap_accept_error_is_nonfatal()
 * 20171014.3 (2.5.1-dev)  AP_DECLARE ap_parse_vhost_addrs() as public 
 * 20171014.4 (2.5.1-dev)  Add failontimeout_set, growth_set and lbmethod_set
 *                         to proxy_balancer struct
 * 20171014.5 (2.5.1-dev)  Add hostname_ex to proxy_worker_shared
 * 20171014.6 (2.5.1-dev)  Add AP_REG_DOLLAR_ENDONLY, ap_regcomp_get_default_cflags
 *                         ap_regcomp_set_default_cflags and
 *                         ap_regcomp_default_cflag_by_name
 * 20171014.7 (2.5.1-dev)  Add AP_CORE_DEFAULT macro
 * 20171014.8 (2.5.1-dev)  Add CONN_STATE_NUM to enum conn_state_e
 * 20171014.9 (2.5.1-dev)  Add response_field_size to proxy_worker_shared 
 * 20180417.1 (2.5.1-dev)  Toggle ap_filter_input|output_pending API to _NONSTD
 * 20180417.2 (2.5.1-dev)  Add AP_GETLINE_NOSPC_EOL flag to http_protocol.h
 * 20180417.3 (2.5.1-dev)  Add ap_fgetline() and AP_GETLINE_NONBLOCK flag
 * 20180422.1 (2.5.1-dev)  Axe ap_rgetline_core()
 * 20180606.1 (2.5.1-dev)  Move ap_{make,set}_etag() from module http to core
 * 20180711.1 (2.5.1-dev)  Add type ap_filter_ring_t, replace field 'filters'
 *                         by the ap_filter_ring_t 'pending_filters' in struct
 *                         conn_rec, and add ring entry 'pending' in struct
 *                         ap_filter_t
 * 20180711.2 (2.5.1-dev)  Add ap_reuse_brigade_from_pool()
 * 20180716.1 (2.5.1-dev)  Axe conn_rec->empty brigade
 * 20180716.2 (2.5.1-dev)  Add read_buf_size member to core_dir_config,
 *                         flush_max_threshold and flush_max_pipelined to
 *                         core_server_config, and ap_get_read_buf_size().
 * 20180720.1 (2.5.1-dev)  Axe data_in_{in,out}put_filter from conn_rec.
 * 20180720.2 (2.5.1-dev)  Add optional function declaration for
 *                         ap_proxy_balancer_get_best_worker to mod_proxy.h.
 * 20180720.3 (2.5.1-dev)  Add client64 to worker_share
 * 20180720.4 (2.5.1-dev)  Add new duration field to worker_score struct in
 *                         scoreboard.h
 * 20180720.5 (2.5.1-dev)  Add new ap_update_global_status() method and
 *                         times field in the global_score structure in
 *                         scoreboard.h.
 * 20180720.6 (2.5.1-dev)  add strict_host_check to core_server_conf
 * 20180720.7 (2.5.1-dev)  worker_share struct re-organized
 * 20180902.1 (2.5.1-dev)  Split conn_rec pending_filters in two rings,
 *                         pending_input_filters and pending_output_filters
 * 20180903.1 (2.5.1-dev)  Replace conn_rec pending_{in,out}put_filters by
 *                         filter_conn_ctx, remove argument pool from
 *                         ap_filter_prepare_brigade()
 * 20180903.2 (2.5.1-dev)  Add ap_filter_recycle()
 * 20180905.1 (2.5.1-dev)  Axe ap_reuse_brigade_from_pool(), replaced by
 *                         ap_acquire_brigade()/ap_release_brigade(), and
 *                         in ap_filter_t replace pending/bb/deferred_pool
 *                         fields by struct ap_filter_private *priv
 * 20180906.1 (2.5.1-dev)  Don't export ap_filter_recycle() anymore
 * 20180906.2 (2.5.1-dev)  Add ap_state_dir_relative()
 * 20180906.3 (2.5.1-dev)  Add ap_dir_nofnmatch() and ap_dir_fnmatch().
 * 20190312.1 (2.5.1-dev)  Axe bucket number from struct process_score
 * 20190312.2 (2.5.1-dev)  Add ap_no2slash_ex() and merge_slashes to 
 *                         core_server_conf.
 * 20190312.3 (2.5.1-dev)  Add forward_100_continue{,_set} to proxy_dir_conf
 * 20190312.4 (2.5.1-dev)  Add add dns_pool to proxy_conn_pool and define
 *                         AP_VOLATILIZE_T.
 * 20190312.5 (2.5.1-dev)  Add proxy_tunnel_rec, ap_proxy_tunnel_create()
 *                         and ap_proxy_tunnel_run() to proxy_util.
 * 20190312.6 (2.5.1-dev)  Add proxy check_trans hook
 * 20190312.7 (2.5.1-dev)  AP_REG_DEFAULT macro in ap_regex.h
 * 20190312.8 (2.5.1-dev)  ap_is_chunked() in httpd.h
 * 20190312.9 (2.5.1-dev)  AP_REG_NO_DEFAULT macro in ap_regex.h
 * 20200217.0 (2.5.1-dev)  Add and use ap_method_mask_t type
 * 20200331.0 (2.5.1-dev)  Remove ap_request_core_filter() and
 *                         ap_request_core_filter_handle.
 * 20200331.1 (2.5.1-dev)  Add flushed:1 to request_rec
 * 20200331.2 (2.5.1-dev)  Add ap_proxy_should_override to mod_proxy.h
 * 20200331.3 (2.5.1-dev)  Add ap_parse_request_line() and
 *                         ap_check_request_header()
 * 20200420.0 (2.5.1-dev)  Add flags to listen_rec in place of use_specific_errors
 * 20200420.1 (2.5.1-dev)  Add ap_filter_adopt_brigade()
 * 20200420.2 (2.5.1-dev)  Add ap_proxy_worker_can_upgrade()
 * 20200420.3 (2.5.1-dev)  Add ap_parse_strict_length()
 * 20200420.4 (2.5.1-dev)  Add ap_normalize_path()
 * 20200420.5 (2.5.1-dev)  Add pre_translate_name hook
 * 20200420.6 (2.5.1-dev)  Add map_encoded_one and map_encoded_all bits to
 *                         proxy_server_conf
 * 20200420.7 (2.5.1-dev)  Add struct etag_rec, ap_make_etag_ex(),
 *                         ap_set_etag_fd(). Add typedef ap_request_bnotes_t,
 *                         macros AP_REQUEST_GET_BNOTE, AP_REQUEST_SET_BNOTE,
 *                         AP_REQUEST_STRONG_ETAG, AP_REQUEST_IS_STRONG_ETAG.
 *                         Add bnotes to request_rec.
 * 20200420.8 (2.5.1-dev)  Add dav_get_provider(), dav_open_lockdb() and
 *                         dav_close_lockdb() mod_dav.h.
 * 20200420.9 (2.5.1-dev)  Add hooks deliver_report and gather_reports to
 *                         mod_dav.h.
 * 20200420.10 (2.5.1-dev) Add method_precondition hook to mod_dav.h.
 * 20200701.0 (2.5.1-dev)  Axe ap_mpm_unregister_poll_callback() and
 *                         mpm_unregister_poll_callback hook.
 * 20200702.1 (2.5.1-dev)  Add pool arg to mpm_register_poll_callback and
 *                         mpm_register_poll_callback_timeout hooks
 * 20200702.2 (2.5.1-dev)  Add dav_get_resource().
 * 20200703.0 (2.5.1-dev)  Remove ap_md5digest(), ap_md5contextTo64(),
 *                         ContentDigest directive.
 * 20200705.0 (2.5.1-dev)  Update method_precondition hook.
 * 20200705.1 (2.5.1-dev)  Add dav_validate_root_ns(), dav_find_child_ns(),
 *                         dav_find_next_ns(), dav_find_attr_ns() and
 *                         dav_find_attr().
 * 20200705.2 (2.5.1-dev)  Add dav_liveprop_elem structure and
 *                         DAV_PROP_ELEMENT key.
 */

#define MODULE_MAGIC_COOKIE 0x41503235UL /* "AP25" */

#ifndef MODULE_MAGIC_NUMBER_MAJOR
#define MODULE_MAGIC_NUMBER_MAJOR 20200705
#endif
#define MODULE_MAGIC_NUMBER_MINOR 2             /* 0...n */

/**
 * Determine if the server's current MODULE_MAGIC_NUMBER is at least a
 * specified value.
 *
 * Useful for testing for features.
 * For example, suppose you wish to use the apr_table_overlap
 *    function.  You can do this:
 *
 * \code
 * #if AP_MODULE_MAGIC_AT_LEAST(19980812,2)
 *     ... use apr_table_overlap()
 * #else
 *     ... alternative code which doesn't use apr_table_overlap()
 * #endif
 * \endcode
 *
 * @param   major   The major module magic number.
 * @param   minor   The minor module magic number.
 */
#define AP_MODULE_MAGIC_AT_LEAST(major,minor)           \
    ((major) < MODULE_MAGIC_NUMBER_MAJOR                \
     || ((major) == MODULE_MAGIC_NUMBER_MAJOR           \
         && (minor) <= MODULE_MAGIC_NUMBER_MINOR))

/** @deprecated Present for backwards compatibility. */
#define MODULE_MAGIC_NUMBER MODULE_MAGIC_NUMBER_MAJOR
/** @deprecated Use AP_MODULE_MAGIC_AT_LEAST. */
#define MODULE_MAGIC_AT_LEAST old_broken_macro_we_hope_you_are_not_using

#endif /* !APACHE_AP_MMN_H */
/** @} */
