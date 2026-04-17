/*
 * Copyright (c) Tatsuya Kamijo
 * Copyright (c) Bengo4.com, Inc.
 *
 * nxe_cedar_eval.c - Cedar policy set evaluator
 *
 * Forbid-priority evaluation model:
 * 1. Evaluate all policies
 * 2. If any forbid matches -> DENY
 * 3. If any permit matches -> ALLOW
 * 4. If none match -> DENY (default deny)
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include "nxe_cedar_eval.h"


/* --- scope matching --- */

static ngx_int_t
nxe_cedar_scope_matches(nxe_cedar_scope_t *scope,
    ngx_str_t *entity_type, ngx_str_t *entity_id)
{
    nxe_cedar_node_t *target, **elts;
    ngx_uint_t i;

    if (scope->constraint == NXE_CEDAR_SCOPE_NONE) {
        return 1;
    }

    if (scope->constraint == NXE_CEDAR_SCOPE_IS
        || scope->constraint == NXE_CEDAR_SCOPE_IS_IN)
    {
        if (!nxe_cedar_str_eq(entity_type, &scope->entity_type)) {
            return 0;
        }

        if (scope->constraint == NXE_CEDAR_SCOPE_IS) {
            return 1;
        }

        /* IS_IN: fall through to target entity_ref check */
        target = scope->target;

        if (target == NULL
            || target->type != NXE_CEDAR_NODE_ENTITY_REF)
        {
            return 0;
        }

        return (nxe_cedar_str_eq(entity_type,
                                 &target->u.entity_ref.entity_type)
                && nxe_cedar_str_eq(entity_id,
                                    &target->u.entity_ref.entity_id));
    }

    target = scope->target;
    if (target == NULL) {
        return 0;
    }

    if (target->type == NXE_CEDAR_NODE_ENTITY_REF) {
        return (nxe_cedar_str_eq(entity_type,
                                 &target->u.entity_ref.entity_type)
                && nxe_cedar_str_eq(entity_id,
                                    &target->u.entity_ref.entity_id));
    }

    /* set target: check if entity matches any element */
    if (target->type == NXE_CEDAR_NODE_SET) {
        if (target->u.set_elts == NULL) {
            return 0;
        }

        elts = target->u.set_elts->elts;

        for (i = 0; i < target->u.set_elts->nelts; i++) {
            if (elts[i]->type == NXE_CEDAR_NODE_ENTITY_REF
                && nxe_cedar_str_eq(entity_type,
                                    &elts[i]->u.entity_ref.entity_type)
                && nxe_cedar_str_eq(entity_id,
                                    &elts[i]->u.entity_ref.entity_id))
            {
                return 1;
            }
        }

        return 0;
    }

    return 0;
}


/* --- condition matching --- */

static ngx_int_t
nxe_cedar_condition_matches(nxe_cedar_condition_t *cond,
    nxe_cedar_eval_ctx_t *ctx, ngx_pool_t *pool, ngx_log_t *log)
{
    nxe_cedar_value_t val;

    val = nxe_cedar_expr_eval(cond->expr, ctx, pool, log);

    if (val.type == NXE_CEDAR_RVAL_ERROR) {
        return 0;
    }

    if (val.type != NXE_CEDAR_RVAL_BOOL) {
        return 0;
    }

    if (cond->is_unless) {
        return !val.v.bool_val;
    }

    return val.v.bool_val;
}


/* --- evaluation context API --- */

static ngx_int_t
nxe_cedar_eval_ctx_add_str_attr(ngx_array_t *attrs,
    ngx_str_t *name, ngx_str_t *value)
{
    nxe_cedar_attr_t *attr;

    attr = ngx_array_push(attrs);
    if (attr == NULL) {
        return NGX_ERROR;
    }

    attr->name = *name;
    attr->value_type = NXE_CEDAR_VALUE_STRING;
    attr->value.str_val = *value;

    return NGX_OK;
}


static ngx_int_t
nxe_cedar_eval_ctx_add_long_attr(ngx_array_t *attrs,
    ngx_str_t *name, int64_t value)
{
    nxe_cedar_attr_t *attr;

    attr = ngx_array_push(attrs);
    if (attr == NULL) {
        return NGX_ERROR;
    }

    attr->name = *name;
    attr->value_type = NXE_CEDAR_VALUE_LONG;
    attr->value.long_val = value;

    return NGX_OK;
}


static ngx_int_t
nxe_cedar_eval_ctx_add_bool_attr(ngx_array_t *attrs,
    ngx_str_t *name, ngx_flag_t value)
{
    nxe_cedar_attr_t *attr;

    attr = ngx_array_push(attrs);
    if (attr == NULL) {
        return NGX_ERROR;
    }

    attr->name = *name;
    attr->value_type = NXE_CEDAR_VALUE_BOOL;
    attr->value.bool_val = value;

    return NGX_OK;
}


static ngx_int_t
nxe_cedar_eval_ctx_add_ip_attr(ngx_array_t *attrs,
    ngx_str_t *name, ngx_str_t *value)
{
    nxe_cedar_attr_t *attr;

    attr = ngx_array_push(attrs);
    if (attr == NULL) {
        return NGX_ERROR;
    }

    attr->name = *name;
    attr->value_type = NXE_CEDAR_VALUE_IP;
    attr->value.ip_str = *value;

    return NGX_OK;
}


nxe_cedar_eval_ctx_t *
nxe_cedar_eval_ctx_create(ngx_pool_t *pool)
{
    nxe_cedar_eval_ctx_t *ctx;

    if (pool == NULL) {
        return NULL;
    }

    ctx = ngx_pcalloc(pool, sizeof(nxe_cedar_eval_ctx_t));
    if (ctx == NULL) {
        return NULL;
    }

    ctx->pool = pool;

    ctx->principal_attrs = ngx_array_create(pool, 4,
                                            sizeof(nxe_cedar_attr_t));
    ctx->action_attrs = ngx_array_create(pool, 4,
                                         sizeof(nxe_cedar_attr_t));
    ctx->resource_attrs = ngx_array_create(pool, 4,
                                           sizeof(nxe_cedar_attr_t));
    ctx->context_attrs = ngx_array_create(pool, 4,
                                          sizeof(nxe_cedar_attr_t));

    if (ctx->principal_attrs == NULL
        || ctx->action_attrs == NULL
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
    return nxe_cedar_eval_ctx_add_str_attr(ctx->principal_attrs,
                                           name, value);
}


ngx_int_t
nxe_cedar_eval_ctx_add_principal_attr_long(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name, int64_t value)
{
    return nxe_cedar_eval_ctx_add_long_attr(ctx->principal_attrs,
                                            name, value);
}


ngx_int_t
nxe_cedar_eval_ctx_add_principal_attr_bool(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name, ngx_flag_t value)
{
    return nxe_cedar_eval_ctx_add_bool_attr(ctx->principal_attrs,
                                            name, value);
}


ngx_int_t
nxe_cedar_eval_ctx_add_principal_attr_ip(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name, ngx_str_t *value)
{
    return nxe_cedar_eval_ctx_add_ip_attr(ctx->principal_attrs,
                                          name, value);
}


void
nxe_cedar_eval_ctx_set_action(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *type, ngx_str_t *id)
{
    ctx->action_type = *type;
    ctx->action_id = *id;
}


ngx_int_t
nxe_cedar_eval_ctx_add_action_attr(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name, ngx_str_t *value)
{
    return nxe_cedar_eval_ctx_add_str_attr(ctx->action_attrs,
                                           name, value);
}


ngx_int_t
nxe_cedar_eval_ctx_add_action_attr_long(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name, int64_t value)
{
    return nxe_cedar_eval_ctx_add_long_attr(ctx->action_attrs,
                                            name, value);
}


ngx_int_t
nxe_cedar_eval_ctx_add_action_attr_bool(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name, ngx_flag_t value)
{
    return nxe_cedar_eval_ctx_add_bool_attr(ctx->action_attrs,
                                            name, value);
}


ngx_int_t
nxe_cedar_eval_ctx_add_action_attr_ip(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name, ngx_str_t *value)
{
    return nxe_cedar_eval_ctx_add_ip_attr(ctx->action_attrs,
                                          name, value);
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
    return nxe_cedar_eval_ctx_add_str_attr(ctx->resource_attrs,
                                           name, value);
}


ngx_int_t
nxe_cedar_eval_ctx_add_resource_attr_long(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name, int64_t value)
{
    return nxe_cedar_eval_ctx_add_long_attr(ctx->resource_attrs,
                                            name, value);
}


ngx_int_t
nxe_cedar_eval_ctx_add_resource_attr_bool(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name, ngx_flag_t value)
{
    return nxe_cedar_eval_ctx_add_bool_attr(ctx->resource_attrs,
                                            name, value);
}


ngx_int_t
nxe_cedar_eval_ctx_add_resource_attr_ip(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name, ngx_str_t *value)
{
    return nxe_cedar_eval_ctx_add_ip_attr(ctx->resource_attrs,
                                          name, value);
}


ngx_int_t
nxe_cedar_eval_ctx_add_context_attr(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name, ngx_str_t *value)
{
    return nxe_cedar_eval_ctx_add_str_attr(ctx->context_attrs,
                                           name, value);
}


ngx_int_t
nxe_cedar_eval_ctx_add_context_attr_long(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name, int64_t value)
{
    return nxe_cedar_eval_ctx_add_long_attr(ctx->context_attrs,
                                            name, value);
}


ngx_int_t
nxe_cedar_eval_ctx_add_context_attr_bool(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name, ngx_flag_t value)
{
    return nxe_cedar_eval_ctx_add_bool_attr(ctx->context_attrs,
                                            name, value);
}


ngx_int_t
nxe_cedar_eval_ctx_add_context_attr_ip(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name, ngx_str_t *value)
{
    return nxe_cedar_eval_ctx_add_ip_attr(ctx->context_attrs,
                                          name, value);
}


/* --- main evaluation --- */

nxe_cedar_decision_t
nxe_cedar_eval(nxe_cedar_policy_set_t *policy_set,
    nxe_cedar_eval_ctx_t *ctx, ngx_log_t *log)
{
    nxe_cedar_policy_t *policies, *p;
    nxe_cedar_condition_t *conds, *c;
    ngx_uint_t i, j;
    ngx_uint_t has_permit, all_met;

    has_permit = 0;

    if (policy_set == NULL || policy_set->policies == NULL
        || ctx == NULL)
    {
        return NXE_CEDAR_DECISION_DENY;
    }

    policies = policy_set->policies->elts;

    for (i = 0; i < policy_set->policies->nelts; i++) {
        p = &policies[i];

        /* scope matching */
        if (!nxe_cedar_scope_matches(&p->principal,
                                     &ctx->principal_type, &ctx->principal_id))
        {
            continue;
        }

        if (!nxe_cedar_scope_matches(&p->action,
                                     &ctx->action_type, &ctx->action_id))
        {
            continue;
        }

        if (!nxe_cedar_scope_matches(&p->resource,
                                     &ctx->resource_type, &ctx->resource_id))
        {
            continue;
        }

        /* condition matching */
        all_met = 1;

        if (p->conditions != NULL && p->conditions->nelts > 0) {
            conds = p->conditions->elts;

            for (j = 0; j < p->conditions->nelts; j++) {
                c = &conds[j];

                if (!nxe_cedar_condition_matches(c, ctx,
                                                 ctx->pool, log))
                {
                    all_met = 0;
                    break;
                }
            }
        }

        if (!all_met) {
            continue;
        }

        if (p->is_forbid) {
            return NXE_CEDAR_DECISION_DENY;
        }

        has_permit = 1;
    }

    if (has_permit) {
        return NXE_CEDAR_DECISION_ALLOW;
    }

    return NXE_CEDAR_DECISION_DENY;
}
