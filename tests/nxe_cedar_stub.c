/*
 * Copyright (c) Tatsuya Kamijo
 * Copyright (c) Bengo4.com, Inc.
 *
 * nxe_cedar_stub.c - nxe-cedar public API stub implementation
 *
 * Temporary implementation until the real C sources are ready.
 * Provides minimal behavior for test harness verification.
 * This file becomes unnecessary once the real sources (nxe_cedar_*.c) are in place.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include "nxe_cedar_eval.h"


/* --- parse API --- */

nxe_cedar_policy_set_t *
nxe_cedar_parse(ngx_pool_t *pool, ngx_log_t *log, const ngx_str_t *text)
{
    nxe_cedar_policy_set_t *ps;

    (void) text;

    ps = ngx_pcalloc(pool, sizeof(nxe_cedar_policy_set_t));
    if (ps == NULL) {
        ngx_log_error(NGX_LOG_ERR, log, 0,
                      "nxe_cedar_parse stub: alloc failed");
        return NULL;
    }

    ps->policies = ngx_array_create(pool, 1, sizeof(nxe_cedar_policy_t));
    if (ps->policies == NULL) {
        return NULL;
    }

    /* stub: returns empty policy set (eval always returns DENY) */
    return ps;
}


/* --- evaluation context API --- */

nxe_cedar_eval_ctx_t *
nxe_cedar_eval_ctx_create(ngx_pool_t *pool)
{
    nxe_cedar_eval_ctx_t *ctx;

    ctx = ngx_pcalloc(pool, sizeof(nxe_cedar_eval_ctx_t));
    if (ctx == NULL) {
        return NULL;
    }

    ctx->pool = pool;

    ctx->principal_attrs = ngx_array_create(pool, 4,
                                            sizeof(nxe_cedar_attr_t));
    ctx->resource_attrs = ngx_array_create(pool, 4,
                                           sizeof(nxe_cedar_attr_t));
    ctx->context_attrs = ngx_array_create(pool, 4,
                                          sizeof(nxe_cedar_attr_t));

    if (ctx->principal_attrs == NULL
        || ctx->resource_attrs == NULL
        || ctx->context_attrs == NULL)
    {
        return NULL;
    }

    return ctx;
}


void
nxe_cedar_eval_ctx_set_principal(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *type, ngx_str_t *id)
{
    ctx->principal_type = *type;
    ctx->principal_id = *id;
}


ngx_int_t
nxe_cedar_eval_ctx_add_principal_attr(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name, ngx_str_t *value)
{
    nxe_cedar_attr_t *attr;

    attr = ngx_array_push(ctx->principal_attrs);
    if (attr == NULL) {
        return NGX_ERROR;
    }

    attr->name = *name;
    attr->value_type = NXE_CEDAR_VALUE_STRING;
    attr->value.str_val = *value;

    return NGX_OK;
}


void
nxe_cedar_eval_ctx_set_action(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *type, ngx_str_t *id)
{
    ctx->action_type = *type;
    ctx->action_id = *id;
}


void
nxe_cedar_eval_ctx_set_resource(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *type, ngx_str_t *id)
{
    ctx->resource_type = *type;
    ctx->resource_id = *id;
}


ngx_int_t
nxe_cedar_eval_ctx_add_resource_attr(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name, ngx_str_t *value)
{
    nxe_cedar_attr_t *attr;

    attr = ngx_array_push(ctx->resource_attrs);
    if (attr == NULL) {
        return NGX_ERROR;
    }

    attr->name = *name;
    attr->value_type = NXE_CEDAR_VALUE_STRING;
    attr->value.str_val = *value;

    return NGX_OK;
}


ngx_int_t
nxe_cedar_eval_ctx_add_context_attr(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name, ngx_str_t *value)
{
    nxe_cedar_attr_t *attr;

    attr = ngx_array_push(ctx->context_attrs);
    if (attr == NULL) {
        return NGX_ERROR;
    }

    attr->name = *name;
    attr->value_type = NXE_CEDAR_VALUE_STRING;
    attr->value.str_val = *value;

    return NGX_OK;
}


ngx_int_t
nxe_cedar_eval_ctx_add_context_attr_long(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name, ngx_int_t value)
{
    nxe_cedar_attr_t *attr;

    attr = ngx_array_push(ctx->context_attrs);
    if (attr == NULL) {
        return NGX_ERROR;
    }

    attr->name = *name;
    attr->value_type = NXE_CEDAR_VALUE_LONG;
    attr->value.long_val = value;

    return NGX_OK;
}


/* --- evaluation API --- */

nxe_cedar_decision_t
nxe_cedar_eval(nxe_cedar_policy_set_t *policy_set,
    nxe_cedar_eval_ctx_t *ctx, ngx_log_t *log)
{
    (void) policy_set;
    (void) ctx;
    (void) log;

    /* stub: always returns DENY */
    return NXE_CEDAR_DECISION_DENY;
}
