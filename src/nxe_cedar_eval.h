/*
 * Copyright (c) Tatsuya Kamijo
 * Copyright (c) Bengo4.com, Inc.
 *
 * nxe_cedar_eval.h - Cedar policy set evaluator
 *
 * Forbid-priority evaluation and context manipulation public API.
 */

#ifndef NXE_CEDAR_EVAL_H
#define NXE_CEDAR_EVAL_H

#include "nxe_cedar_types.h"
#include "nxe_cedar_expr.h"


/*
 * Opaque handle used to populate a record-valued attribute one field
 * at a time. Created by nxe_cedar_eval_ctx_add_*_attr_record() for a
 * top-level record, or by nxe_cedar_record_add_record() for a nested
 * record. The implementation lives in nxe_cedar_eval.c.
 */
typedef struct nxe_cedar_record_s nxe_cedar_record_t;


nxe_cedar_decision_t nxe_cedar_eval(nxe_cedar_policy_set_t *policy_set,
    nxe_cedar_eval_ctx_t *ctx, ngx_log_t *log);

nxe_cedar_eval_ctx_t *nxe_cedar_eval_ctx_create(ngx_pool_t *pool);

void nxe_cedar_eval_ctx_set_principal(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *type, ngx_str_t *id);
ngx_int_t nxe_cedar_eval_ctx_add_principal_attr(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name, ngx_str_t *value);
ngx_int_t nxe_cedar_eval_ctx_add_principal_attr_long(
    nxe_cedar_eval_ctx_t *ctx, ngx_str_t *name, int64_t value);
ngx_int_t nxe_cedar_eval_ctx_add_principal_attr_bool(
    nxe_cedar_eval_ctx_t *ctx, ngx_str_t *name, ngx_flag_t value);
ngx_int_t nxe_cedar_eval_ctx_add_principal_attr_ip(
    nxe_cedar_eval_ctx_t *ctx, ngx_str_t *name, ngx_str_t *value);

void nxe_cedar_eval_ctx_set_action(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *type, ngx_str_t *id);
ngx_int_t nxe_cedar_eval_ctx_add_action_attr(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name, ngx_str_t *value);
ngx_int_t nxe_cedar_eval_ctx_add_action_attr_long(
    nxe_cedar_eval_ctx_t *ctx, ngx_str_t *name, int64_t value);
ngx_int_t nxe_cedar_eval_ctx_add_action_attr_bool(
    nxe_cedar_eval_ctx_t *ctx, ngx_str_t *name, ngx_flag_t value);
ngx_int_t nxe_cedar_eval_ctx_add_action_attr_ip(
    nxe_cedar_eval_ctx_t *ctx, ngx_str_t *name, ngx_str_t *value);

void nxe_cedar_eval_ctx_set_resource(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *type, ngx_str_t *id);
ngx_int_t nxe_cedar_eval_ctx_add_resource_attr(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name, ngx_str_t *value);
ngx_int_t nxe_cedar_eval_ctx_add_resource_attr_long(
    nxe_cedar_eval_ctx_t *ctx, ngx_str_t *name, int64_t value);
ngx_int_t nxe_cedar_eval_ctx_add_resource_attr_bool(
    nxe_cedar_eval_ctx_t *ctx, ngx_str_t *name, ngx_flag_t value);
ngx_int_t nxe_cedar_eval_ctx_add_resource_attr_ip(
    nxe_cedar_eval_ctx_t *ctx, ngx_str_t *name, ngx_str_t *value);

ngx_int_t nxe_cedar_eval_ctx_add_context_attr(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name, ngx_str_t *value);
ngx_int_t nxe_cedar_eval_ctx_add_context_attr_long(
    nxe_cedar_eval_ctx_t *ctx, ngx_str_t *name, int64_t value);
ngx_int_t nxe_cedar_eval_ctx_add_context_attr_bool(
    nxe_cedar_eval_ctx_t *ctx, ngx_str_t *name, ngx_flag_t value);
ngx_int_t nxe_cedar_eval_ctx_add_context_attr_ip(
    nxe_cedar_eval_ctx_t *ctx, ngx_str_t *name, ngx_str_t *value);

/*
 * Record-valued attribute constructors.
 *
 * Each nxe_cedar_eval_ctx_add_*_attr_record() reserves a new
 * record-valued attribute on the corresponding entity / context and
 * returns a handle for populating its fields. Callers add fields via
 * nxe_cedar_record_add_{str,long,bool,ip,record}().
 *
 * nxe_cedar_record_add_record() returns NULL when the resulting record
 * would exceed NXE_CEDAR_MAX_RECORD_DEPTH. The record nesting limit is
 * aligned with the parser's member-chain limit; scalar fields added
 * directly to a record at exactly NXE_CEDAR_MAX_RECORD_DEPTH are
 * writable but require one more member step and are not reachable from
 * policy text.
 *
 * Returns NULL on allocation failure as well.
 */
nxe_cedar_record_t *nxe_cedar_eval_ctx_add_principal_attr_record(
    nxe_cedar_eval_ctx_t *ctx, ngx_str_t *name);
nxe_cedar_record_t *nxe_cedar_eval_ctx_add_action_attr_record(
    nxe_cedar_eval_ctx_t *ctx, ngx_str_t *name);
nxe_cedar_record_t *nxe_cedar_eval_ctx_add_resource_attr_record(
    nxe_cedar_eval_ctx_t *ctx, ngx_str_t *name);
nxe_cedar_record_t *nxe_cedar_eval_ctx_add_context_attr_record(
    nxe_cedar_eval_ctx_t *ctx, ngx_str_t *name);

ngx_int_t nxe_cedar_record_add_str(nxe_cedar_record_t *rec,
    ngx_str_t *name, ngx_str_t *value);
ngx_int_t nxe_cedar_record_add_long(nxe_cedar_record_t *rec,
    ngx_str_t *name, int64_t value);
ngx_int_t nxe_cedar_record_add_bool(nxe_cedar_record_t *rec,
    ngx_str_t *name, ngx_flag_t value);
ngx_int_t nxe_cedar_record_add_ip(nxe_cedar_record_t *rec,
    ngx_str_t *name, ngx_str_t *value);
nxe_cedar_record_t *nxe_cedar_record_add_record(nxe_cedar_record_t *rec,
    ngx_str_t *name);


#endif /* NXE_CEDAR_EVAL_H */
