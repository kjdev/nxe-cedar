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

/*
 * Reflexive-transitive entity membership check used by `in` scope
 * constraints and the `in` expression operator. `parents` is the
 * pre-computed transitive closure supplied through
 * nxe_cedar_eval_ctx_add_*_parent(); the reflexive case (X in X) is
 * handled inline without registration.
 */
ngx_int_t
nxe_cedar_entity_in_target(ngx_str_t *entity_type, ngx_str_t *entity_id,
    ngx_array_t *parents,
    ngx_str_t *target_type, ngx_str_t *target_id)
{
    nxe_cedar_entity_ref_t *elts;
    ngx_uint_t i;

    if (nxe_cedar_str_eq(entity_type, target_type)
        && nxe_cedar_str_eq(entity_id, target_id))
    {
        return 1;
    }

    if (parents == NULL) {
        return 0;
    }

    elts = parents->elts;
    for (i = 0; i < parents->nelts; i++) {
        if (nxe_cedar_str_eq(&elts[i].type, target_type)
            && nxe_cedar_str_eq(&elts[i].id, target_id))
        {
            return 1;
        }
    }

    return 0;
}


/*
 * Resolve the parents array for an entity value by its origin slot.
 * The slot is stamped on the value when NXE_CEDAR_NODE_VAR evaluation
 * produces the principal / action / resource entity. Returns NULL for
 * NXE_CEDAR_ENTITY_SLOT_NONE (literals, attribute lookups, set
 * elements) so `in` evaluation falls back to reflexive comparison only,
 * which matches Cedar semantics: derived entities have no ancestor
 * information attached.
 *
 * The previous (type, id) lookup collapsed on collisions and silently
 * returned principal_parents whenever principal / action / resource
 * shared the same identity, flipping `in` decisions.
 */
ngx_array_t *
nxe_cedar_eval_ctx_lookup_parents(nxe_cedar_eval_ctx_t *ctx,
    ngx_uint_t slot)
{
    if (ctx == NULL) {
        return NULL;
    }

    switch (slot) {
    case NXE_CEDAR_ENTITY_SLOT_PRINCIPAL:
        return ctx->principal_parents;
    case NXE_CEDAR_ENTITY_SLOT_ACTION:
        return ctx->action_parents;
    case NXE_CEDAR_ENTITY_SLOT_RESOURCE:
        return ctx->resource_parents;
    default:
        return NULL;
    }
}


static ngx_int_t
nxe_cedar_scope_matches(nxe_cedar_scope_t *scope,
    ngx_str_t *entity_type, ngx_str_t *entity_id,
    ngx_array_t *parents)
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

        /* IS_IN: reuse hierarchical match on the entity_ref target */
        target = scope->target;

        if (target == NULL
            || target->type != NXE_CEDAR_NODE_ENTITY_REF)
        {
            return 0;
        }

        return nxe_cedar_entity_in_target(entity_type, entity_id, parents,
                                          &target->u.entity_ref.entity_type,
                                          &target->u.entity_ref.entity_id);
    }

    target = scope->target;
    if (target == NULL) {
        return 0;
    }

    if (scope->constraint == NXE_CEDAR_SCOPE_EQ) {
        if (target->type != NXE_CEDAR_NODE_ENTITY_REF) {
            return 0;
        }
        return (nxe_cedar_str_eq(entity_type,
                                 &target->u.entity_ref.entity_type)
                && nxe_cedar_str_eq(entity_id,
                                    &target->u.entity_ref.entity_id));
    }

    /* SCOPE_IN */
    if (target->type == NXE_CEDAR_NODE_ENTITY_REF) {
        return nxe_cedar_entity_in_target(entity_type, entity_id, parents,
                                          &target->u.entity_ref.entity_type,
                                          &target->u.entity_ref.entity_id);
    }

    /* set target: entity in [Group::"a", Group::"b"] */
    if (target->type == NXE_CEDAR_NODE_SET) {
        if (target->u.set_elts == NULL) {
            return 0;
        }

        elts = target->u.set_elts->elts;

        for (i = 0; i < target->u.set_elts->nelts; i++) {
            if (elts[i]->type == NXE_CEDAR_NODE_ENTITY_REF
                && nxe_cedar_entity_in_target(entity_type, entity_id,
                                              parents,
                                              &elts[i]->u.entity_ref.entity_type,
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
    attr->value.type = NXE_CEDAR_RVAL_STRING;
    attr->value.v.str_val = *value;

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
    attr->value.type = NXE_CEDAR_RVAL_LONG;
    attr->value.v.long_val = value;

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
    attr->value.type = NXE_CEDAR_RVAL_BOOL;
    attr->value.v.bool_val = value;

    return NGX_OK;
}


/*
 * IP attributes are eagerly parsed at injection time so readers can
 * see the binary representation directly. Invalid IP strings are
 * rejected here with NGX_ERROR instead of surfacing as a silent
 * evaluation error on first access.
 */
static ngx_int_t
nxe_cedar_eval_ctx_add_ip_attr(ngx_array_t *attrs,
    ngx_str_t *name, ngx_str_t *value)
{
    nxe_cedar_attr_t *attr;
    nxe_cedar_value_t ip_val;

    ip_val = nxe_cedar_make_ip(value);
    if (ip_val.type == NXE_CEDAR_RVAL_ERROR) {
        return NGX_ERROR;
    }

    attr = ngx_array_push(attrs);
    if (attr == NULL) {
        return NGX_ERROR;
    }

    attr->name = *name;
    attr->value = ip_val;

    return NGX_OK;
}


/*
 * Record handle.
 *
 * - attrs: array of nxe_cedar_attr_t (shared with the attribute value
 *   stored in the owning entity / parent record).
 * - pool: owns all record / attribute allocations; freed with the
 *   evaluation context.
 * - depth: current nesting depth (1 = direct child of an entity /
 *   context, increments by 1 for each nxe_cedar_record_add_record).
 */
struct nxe_cedar_record_s {
    ngx_array_t *attrs;
    ngx_pool_t  *pool;
    ngx_uint_t   depth;
};


static nxe_cedar_record_t *
nxe_cedar_record_create(ngx_pool_t *pool, ngx_uint_t depth)
{
    nxe_cedar_record_t *rec;

    rec = ngx_pcalloc(pool, sizeof(nxe_cedar_record_t));
    if (rec == NULL) {
        return NULL;
    }

    rec->attrs = ngx_array_create(pool, 4, sizeof(nxe_cedar_attr_t));
    if (rec->attrs == NULL) {
        return NULL;
    }

    rec->pool = pool;
    rec->depth = depth;

    return rec;
}


/*
 * Reserve a new record-valued attribute on the given attr array and
 * return a populated handle. Shared helper for the four
 * nxe_cedar_eval_ctx_add_*_attr_record entry points.
 */
static nxe_cedar_record_t *
nxe_cedar_eval_ctx_add_record_attr(ngx_array_t *attrs, ngx_pool_t *pool,
    ngx_str_t *name)
{
    nxe_cedar_attr_t *attr;
    nxe_cedar_record_t *rec;

    rec = nxe_cedar_record_create(pool, 1);
    if (rec == NULL) {
        return NULL;
    }

    attr = ngx_array_push(attrs);
    if (attr == NULL) {
        return NULL;
    }

    attr->name = *name;
    attr->value.type = NXE_CEDAR_RVAL_RECORD;
    attr->value.v.record_attrs = rec->attrs;

    return rec;
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

    ctx->principal_parents = ngx_array_create(pool, 2,
                                              sizeof(nxe_cedar_entity_ref_t));
    ctx->action_parents = ngx_array_create(pool, 2,
                                           sizeof(nxe_cedar_entity_ref_t));
    ctx->resource_parents = ngx_array_create(pool, 2,
                                             sizeof(nxe_cedar_entity_ref_t));

    if (ctx->principal_attrs == NULL
        || ctx->action_attrs == NULL
        || ctx->resource_attrs == NULL
        || ctx->context_attrs == NULL
        || ctx->principal_parents == NULL
        || ctx->action_parents == NULL
        || ctx->resource_parents == NULL)
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


nxe_cedar_record_t *
nxe_cedar_eval_ctx_add_principal_attr_record(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name)
{
    return nxe_cedar_eval_ctx_add_record_attr(ctx->principal_attrs,
                                              ctx->pool, name);
}


nxe_cedar_record_t *
nxe_cedar_eval_ctx_add_action_attr_record(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name)
{
    return nxe_cedar_eval_ctx_add_record_attr(ctx->action_attrs,
                                              ctx->pool, name);
}


nxe_cedar_record_t *
nxe_cedar_eval_ctx_add_resource_attr_record(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name)
{
    return nxe_cedar_eval_ctx_add_record_attr(ctx->resource_attrs,
                                              ctx->pool, name);
}


nxe_cedar_record_t *
nxe_cedar_eval_ctx_add_context_attr_record(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name)
{
    return nxe_cedar_eval_ctx_add_record_attr(ctx->context_attrs,
                                              ctx->pool, name);
}


ngx_int_t
nxe_cedar_record_add_str(nxe_cedar_record_t *rec, ngx_str_t *name,
    ngx_str_t *value)
{
    if (rec == NULL) {
        return NGX_ERROR;
    }
    return nxe_cedar_eval_ctx_add_str_attr(rec->attrs, name, value);
}


ngx_int_t
nxe_cedar_record_add_long(nxe_cedar_record_t *rec, ngx_str_t *name,
    int64_t value)
{
    if (rec == NULL) {
        return NGX_ERROR;
    }
    return nxe_cedar_eval_ctx_add_long_attr(rec->attrs, name, value);
}


ngx_int_t
nxe_cedar_record_add_bool(nxe_cedar_record_t *rec, ngx_str_t *name,
    ngx_flag_t value)
{
    if (rec == NULL) {
        return NGX_ERROR;
    }
    return nxe_cedar_eval_ctx_add_bool_attr(rec->attrs, name, value);
}


ngx_int_t
nxe_cedar_record_add_ip(nxe_cedar_record_t *rec, ngx_str_t *name,
    ngx_str_t *value)
{
    if (rec == NULL) {
        return NGX_ERROR;
    }
    return nxe_cedar_eval_ctx_add_ip_attr(rec->attrs, name, value);
}


nxe_cedar_record_t *
nxe_cedar_record_add_record(nxe_cedar_record_t *rec, ngx_str_t *name)
{
    nxe_cedar_attr_t *attr;
    nxe_cedar_record_t *child;

    if (rec == NULL) {
        return NULL;
    }

    if (rec->depth >= NXE_CEDAR_MAX_RECORD_DEPTH) {
        ngx_log_error(NGX_LOG_ERR, rec->pool->log, 0,
                      "nxe_cedar_record_add_record: "
                      "record nesting exceeds max depth (%d)",
                      NXE_CEDAR_MAX_RECORD_DEPTH);
        return NULL;
    }

    child = nxe_cedar_record_create(rec->pool, rec->depth + 1);
    if (child == NULL) {
        return NULL;
    }

    attr = ngx_array_push(rec->attrs);
    if (attr == NULL) {
        return NULL;
    }

    attr->name = *name;
    attr->value.type = NXE_CEDAR_RVAL_RECORD;
    attr->value.v.record_attrs = child->attrs;

    return child;
}


/* --- set values --- */

/*
 * Set handle.
 *
 * - elts: array of nxe_cedar_value_t shared with the attribute value
 *   stored in the owning entity / record / set; element pushes are
 *   visible through both views.
 * - pool: owns all set / element allocations; freed with the
 *   evaluation context.
 * - depth: nesting depth for set-in-set (1 = direct child of an
 *   entity / context / record / set).
 */
struct nxe_cedar_set_s {
    ngx_array_t *elts;
    ngx_pool_t  *pool;
    ngx_uint_t   depth;
};


static nxe_cedar_set_t *
nxe_cedar_set_create(ngx_pool_t *pool, ngx_uint_t depth)
{
    nxe_cedar_set_t *set;

    set = ngx_pcalloc(pool, sizeof(nxe_cedar_set_t));
    if (set == NULL) {
        return NULL;
    }

    set->elts = ngx_array_create(pool, 4, sizeof(nxe_cedar_value_t));
    if (set->elts == NULL) {
        return NULL;
    }

    set->pool = pool;
    set->depth = depth;

    return set;
}


/*
 * Reserve a new set-valued attribute on the given attr array and
 * return a populated handle. Shared helper for the four
 * nxe_cedar_eval_ctx_add_*_attr_set entry points (depth = 1) and for
 * nxe_cedar_record_add_set, which passes its own depth + 1 so a mixed
 * record / set graph respects one NXE_CEDAR_MAX_SET_DEPTH ceiling.
 */
static nxe_cedar_set_t *
nxe_cedar_eval_ctx_add_set_attr(ngx_array_t *attrs, ngx_pool_t *pool,
    ngx_str_t *name, ngx_uint_t depth)
{
    nxe_cedar_attr_t *attr;
    nxe_cedar_set_t *set;

    if (attrs == NULL || pool == NULL || name == NULL) {
        return NULL;
    }

    if (depth > NXE_CEDAR_MAX_SET_DEPTH) {
        ngx_log_error(NGX_LOG_ERR, pool->log, 0,
                      "nxe_cedar_eval_ctx_add_set_attr: "
                      "set nesting exceeds max depth (%d)",
                      NXE_CEDAR_MAX_SET_DEPTH);
        return NULL;
    }

    set = nxe_cedar_set_create(pool, depth);
    if (set == NULL) {
        return NULL;
    }

    attr = ngx_array_push(attrs);
    if (attr == NULL) {
        return NULL;
    }

    ngx_memzero(attr, sizeof(nxe_cedar_attr_t));
    attr->name = *name;
    attr->value.type = NXE_CEDAR_RVAL_SET;
    attr->value.v.set_elts = set->elts;

    return set;
}


/*
 * Append an entity-valued attribute to the given attr array.
 * Shared helper for the four nxe_cedar_eval_ctx_add_*_attr_entity
 * entry points and for nxe_cedar_record_add_entity().
 */
static ngx_int_t
nxe_cedar_eval_ctx_add_entity_attr(ngx_array_t *attrs,
    ngx_str_t *name, ngx_str_t *type, ngx_str_t *id)
{
    nxe_cedar_attr_t *attr;

    if (attrs == NULL || name == NULL || type == NULL || id == NULL) {
        return NGX_ERROR;
    }

    attr = ngx_array_push(attrs);
    if (attr == NULL) {
        return NGX_ERROR;
    }

    ngx_memzero(attr, sizeof(nxe_cedar_attr_t));
    attr->name = *name;
    attr->value.type = NXE_CEDAR_RVAL_ENTITY;
    attr->value.v.entity.type = *type;
    attr->value.v.entity.id = *id;

    return NGX_OK;
}


ngx_int_t
nxe_cedar_set_add_str(nxe_cedar_set_t *set, ngx_str_t *value)
{
    nxe_cedar_value_t *v;

    if (set == NULL || value == NULL) {
        return NGX_ERROR;
    }

    v = ngx_array_push(set->elts);
    if (v == NULL) {
        return NGX_ERROR;
    }

    ngx_memzero(v, sizeof(nxe_cedar_value_t));
    v->type = NXE_CEDAR_RVAL_STRING;
    v->v.str_val = *value;

    return NGX_OK;
}


ngx_int_t
nxe_cedar_set_add_long(nxe_cedar_set_t *set, int64_t value)
{
    nxe_cedar_value_t *v;

    if (set == NULL) {
        return NGX_ERROR;
    }

    v = ngx_array_push(set->elts);
    if (v == NULL) {
        return NGX_ERROR;
    }

    ngx_memzero(v, sizeof(nxe_cedar_value_t));
    v->type = NXE_CEDAR_RVAL_LONG;
    v->v.long_val = value;

    return NGX_OK;
}


ngx_int_t
nxe_cedar_set_add_bool(nxe_cedar_set_t *set, ngx_flag_t value)
{
    nxe_cedar_value_t *v;

    if (set == NULL) {
        return NGX_ERROR;
    }

    v = ngx_array_push(set->elts);
    if (v == NULL) {
        return NGX_ERROR;
    }

    ngx_memzero(v, sizeof(nxe_cedar_value_t));
    v->type = NXE_CEDAR_RVAL_BOOL;
    v->v.bool_val = value;

    return NGX_OK;
}


ngx_int_t
nxe_cedar_set_add_ip(nxe_cedar_set_t *set, ngx_str_t *value)
{
    nxe_cedar_value_t *v, ip_val;

    if (set == NULL || value == NULL) {
        return NGX_ERROR;
    }

    ip_val = nxe_cedar_make_ip(value);
    if (ip_val.type == NXE_CEDAR_RVAL_ERROR) {
        return NGX_ERROR;
    }

    v = ngx_array_push(set->elts);
    if (v == NULL) {
        return NGX_ERROR;
    }

    *v = ip_val;

    return NGX_OK;
}


ngx_int_t
nxe_cedar_set_add_entity(nxe_cedar_set_t *set,
    ngx_str_t *type, ngx_str_t *id)
{
    nxe_cedar_value_t *v;

    if (set == NULL || type == NULL || id == NULL) {
        return NGX_ERROR;
    }

    v = ngx_array_push(set->elts);
    if (v == NULL) {
        return NGX_ERROR;
    }

    ngx_memzero(v, sizeof(nxe_cedar_value_t));
    v->type = NXE_CEDAR_RVAL_ENTITY;
    v->v.entity.type = *type;
    v->v.entity.id = *id;

    return NGX_OK;
}


nxe_cedar_set_t *
nxe_cedar_set_add_set(nxe_cedar_set_t *set)
{
    nxe_cedar_value_t *v;
    nxe_cedar_set_t *child;

    if (set == NULL) {
        return NULL;
    }

    if (set->depth >= NXE_CEDAR_MAX_SET_DEPTH) {
        ngx_log_error(NGX_LOG_ERR, set->pool->log, 0,
                      "nxe_cedar_set_add_set: "
                      "set nesting exceeds max depth (%d)",
                      NXE_CEDAR_MAX_SET_DEPTH);
        return NULL;
    }

    child = nxe_cedar_set_create(set->pool, set->depth + 1);
    if (child == NULL) {
        return NULL;
    }

    v = ngx_array_push(set->elts);
    if (v == NULL) {
        return NULL;
    }

    ngx_memzero(v, sizeof(nxe_cedar_value_t));
    v->type = NXE_CEDAR_RVAL_SET;
    v->v.set_elts = child->elts;

    return child;
}


nxe_cedar_record_t *
nxe_cedar_set_add_record(nxe_cedar_set_t *set)
{
    nxe_cedar_value_t *v;
    nxe_cedar_record_t *child;

    if (set == NULL) {
        return NULL;
    }

    /*
     * Inherit the set's depth so a mixed graph (record -> set ->
     * record -> ...) shares one ceiling. Without this, kind switches
     * reset the counter to 1 and `==` could descend deeper than
     * NXE_CEDAR_MAX_RECORD_DEPTH on alternating chains.
     */
    if (set->depth >= NXE_CEDAR_MAX_RECORD_DEPTH) {
        ngx_log_error(NGX_LOG_ERR, set->pool->log, 0,
                      "nxe_cedar_set_add_record: "
                      "record nesting exceeds max depth (%d)",
                      NXE_CEDAR_MAX_RECORD_DEPTH);
        return NULL;
    }

    child = nxe_cedar_record_create(set->pool, set->depth + 1);
    if (child == NULL) {
        return NULL;
    }

    v = ngx_array_push(set->elts);
    if (v == NULL) {
        return NULL;
    }

    ngx_memzero(v, sizeof(nxe_cedar_value_t));
    v->type = NXE_CEDAR_RVAL_RECORD;
    v->v.record_attrs = child->attrs;

    return child;
}


nxe_cedar_set_t *
nxe_cedar_eval_ctx_add_principal_attr_set(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name)
{
    if (ctx == NULL) {
        return NULL;
    }
    return nxe_cedar_eval_ctx_add_set_attr(ctx->principal_attrs,
                                           ctx->pool, name, 1);
}


nxe_cedar_set_t *
nxe_cedar_eval_ctx_add_action_attr_set(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name)
{
    if (ctx == NULL) {
        return NULL;
    }
    return nxe_cedar_eval_ctx_add_set_attr(ctx->action_attrs,
                                           ctx->pool, name, 1);
}


nxe_cedar_set_t *
nxe_cedar_eval_ctx_add_resource_attr_set(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name)
{
    if (ctx == NULL) {
        return NULL;
    }
    return nxe_cedar_eval_ctx_add_set_attr(ctx->resource_attrs,
                                           ctx->pool, name, 1);
}


nxe_cedar_set_t *
nxe_cedar_eval_ctx_add_context_attr_set(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name)
{
    if (ctx == NULL) {
        return NULL;
    }
    return nxe_cedar_eval_ctx_add_set_attr(ctx->context_attrs,
                                           ctx->pool, name, 1);
}


ngx_int_t
nxe_cedar_eval_ctx_add_principal_attr_entity(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name, ngx_str_t *type, ngx_str_t *id)
{
    if (ctx == NULL) {
        return NGX_ERROR;
    }
    return nxe_cedar_eval_ctx_add_entity_attr(ctx->principal_attrs,
                                              name, type, id);
}


ngx_int_t
nxe_cedar_eval_ctx_add_action_attr_entity(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name, ngx_str_t *type, ngx_str_t *id)
{
    if (ctx == NULL) {
        return NGX_ERROR;
    }
    return nxe_cedar_eval_ctx_add_entity_attr(ctx->action_attrs,
                                              name, type, id);
}


ngx_int_t
nxe_cedar_eval_ctx_add_resource_attr_entity(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name, ngx_str_t *type, ngx_str_t *id)
{
    if (ctx == NULL) {
        return NGX_ERROR;
    }
    return nxe_cedar_eval_ctx_add_entity_attr(ctx->resource_attrs,
                                              name, type, id);
}


ngx_int_t
nxe_cedar_eval_ctx_add_context_attr_entity(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name, ngx_str_t *type, ngx_str_t *id)
{
    if (ctx == NULL) {
        return NGX_ERROR;
    }
    return nxe_cedar_eval_ctx_add_entity_attr(ctx->context_attrs,
                                              name, type, id);
}


ngx_int_t
nxe_cedar_record_add_entity(nxe_cedar_record_t *rec, ngx_str_t *name,
    ngx_str_t *type, ngx_str_t *id)
{
    if (rec == NULL) {
        return NGX_ERROR;
    }
    return nxe_cedar_eval_ctx_add_entity_attr(rec->attrs, name, type, id);
}


nxe_cedar_set_t *
nxe_cedar_record_add_set(nxe_cedar_record_t *rec, ngx_str_t *name)
{
    if (rec == NULL) {
        return NULL;
    }
    if (rec->depth >= NXE_CEDAR_MAX_SET_DEPTH) {
        ngx_log_error(NGX_LOG_ERR, rec->pool->log, 0,
                      "nxe_cedar_record_add_set: "
                      "set nesting exceeds max depth (%d)",
                      NXE_CEDAR_MAX_SET_DEPTH);
        return NULL;
    }
    return nxe_cedar_eval_ctx_add_set_attr(rec->attrs, rec->pool, name,
                                           rec->depth + 1);
}


/* --- entity hierarchy --- */

static ngx_int_t
nxe_cedar_eval_ctx_add_parent(ngx_array_t *parents,
    ngx_str_t *type, ngx_str_t *id)
{
    nxe_cedar_entity_ref_t *ref;

    if (parents == NULL || type == NULL || id == NULL) {
        return NGX_ERROR;
    }

    ref = ngx_array_push(parents);
    if (ref == NULL) {
        return NGX_ERROR;
    }

    ref->type = *type;
    ref->id = *id;

    return NGX_OK;
}


ngx_int_t
nxe_cedar_eval_ctx_add_principal_parent(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *type, ngx_str_t *id)
{
    if (ctx == NULL) {
        return NGX_ERROR;
    }
    return nxe_cedar_eval_ctx_add_parent(ctx->principal_parents, type, id);
}


ngx_int_t
nxe_cedar_eval_ctx_add_action_parent(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *type, ngx_str_t *id)
{
    if (ctx == NULL) {
        return NGX_ERROR;
    }
    return nxe_cedar_eval_ctx_add_parent(ctx->action_parents, type, id);
}


ngx_int_t
nxe_cedar_eval_ctx_add_resource_parent(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *type, ngx_str_t *id)
{
    if (ctx == NULL) {
        return NGX_ERROR;
    }
    return nxe_cedar_eval_ctx_add_parent(ctx->resource_parents, type, id);
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
                                     &ctx->principal_type, &ctx->principal_id,
                                     ctx->principal_parents))
        {
            continue;
        }

        if (!nxe_cedar_scope_matches(&p->action,
                                     &ctx->action_type, &ctx->action_id,
                                     ctx->action_parents))
        {
            continue;
        }

        if (!nxe_cedar_scope_matches(&p->resource,
                                     &ctx->resource_type, &ctx->resource_id,
                                     ctx->resource_parents))
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
