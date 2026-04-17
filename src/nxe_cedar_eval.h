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


#endif /* NXE_CEDAR_EVAL_H */
