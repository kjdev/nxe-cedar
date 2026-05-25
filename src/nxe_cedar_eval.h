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

/*
 * Opaque handle used to populate a set-valued attribute element by
 * element. Created by nxe_cedar_eval_ctx_add_*_attr_set() for a
 * top-level set, by nxe_cedar_record_add_set() inside a record, or by
 * nxe_cedar_set_add_set() inside another set.
 */
typedef struct nxe_cedar_set_s nxe_cedar_set_t;


nxe_cedar_decision_t nxe_cedar_eval(nxe_cedar_policy_set_t *policy_set,
    nxe_cedar_eval_ctx_t *ctx, ngx_log_t *log);

/*
 * Variant of nxe_cedar_eval() that records the policies responsible
 * for the decision into `out`. On DENY because at least one `forbid`
 * matched, `out->policies` lists every matching `forbid`; on ALLOW it
 * lists every matching `permit`; on default DENY (no policy matched)
 * `out->policies` is NULL and `out->npolicies` is 0. The pointer
 * array is allocated from `ctx->pool`; each entry points into the
 * input policy set, so the caller must not dereference entries past
 * the shorter of `ctx->pool` and the policy set's lifetimes.
 *
 * Detail collection is best-effort: if allocation fails while growing
 * the pointer array, the returned decision remains correct but
 * `out->policies` may be truncated (it lists a prefix of the matching
 * policies rather than every one).
 *
 * `out` may be NULL; nxe_cedar_eval() is a thin wrapper that passes
 * NULL and is preserved for callers that only need the decision.
 */
nxe_cedar_decision_t nxe_cedar_eval_detail(
    nxe_cedar_policy_set_t *policy_set,
    nxe_cedar_eval_ctx_t *ctx, ngx_log_t *log,
    nxe_cedar_decision_detail_t *out);

/*
 * Lookup an annotation value by key on a parsed policy. Returns the
 * annotation's value (which may be an empty string for valueless
 * annotations like `@deprecated`) or NULL when the key is absent.
 * The returned ngx_str_t is owned by the policy set; the caller must
 * not modify or free it.
 */
ngx_str_t *nxe_cedar_policy_get_annotation(nxe_cedar_policy_t *policy,
    ngx_str_t *key);

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
ngx_int_t nxe_cedar_eval_ctx_add_principal_attr_decimal(
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
ngx_int_t nxe_cedar_eval_ctx_add_action_attr_decimal(
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
ngx_int_t nxe_cedar_eval_ctx_add_resource_attr_decimal(
    nxe_cedar_eval_ctx_t *ctx, ngx_str_t *name, ngx_str_t *value);

ngx_int_t nxe_cedar_eval_ctx_add_context_attr(nxe_cedar_eval_ctx_t *ctx,
    ngx_str_t *name, ngx_str_t *value);
ngx_int_t nxe_cedar_eval_ctx_add_context_attr_long(
    nxe_cedar_eval_ctx_t *ctx, ngx_str_t *name, int64_t value);
ngx_int_t nxe_cedar_eval_ctx_add_context_attr_bool(
    nxe_cedar_eval_ctx_t *ctx, ngx_str_t *name, ngx_flag_t value);
ngx_int_t nxe_cedar_eval_ctx_add_context_attr_ip(
    nxe_cedar_eval_ctx_t *ctx, ngx_str_t *name, ngx_str_t *value);
ngx_int_t nxe_cedar_eval_ctx_add_context_attr_decimal(
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
ngx_int_t nxe_cedar_record_add_decimal(nxe_cedar_record_t *rec,
    ngx_str_t *name, ngx_str_t *value);
nxe_cedar_record_t *nxe_cedar_record_add_record(nxe_cedar_record_t *rec,
    ngx_str_t *name);
ngx_int_t nxe_cedar_record_add_entity(nxe_cedar_record_t *rec,
    ngx_str_t *name, ngx_str_t *type, ngx_str_t *id);
nxe_cedar_set_t *nxe_cedar_record_add_set(nxe_cedar_record_t *rec,
    ngx_str_t *name);


/*
 * Set-valued attribute constructors. Each call reserves a new
 * set-valued attribute on the corresponding entity / context and
 * returns a handle for appending elements via
 * nxe_cedar_set_add_{str,long,bool,ip,entity,set,record}().
 *
 * Returns NULL on allocation failure.
 */
nxe_cedar_set_t *nxe_cedar_eval_ctx_add_principal_attr_set(
    nxe_cedar_eval_ctx_t *ctx, ngx_str_t *name);
nxe_cedar_set_t *nxe_cedar_eval_ctx_add_action_attr_set(
    nxe_cedar_eval_ctx_t *ctx, ngx_str_t *name);
nxe_cedar_set_t *nxe_cedar_eval_ctx_add_resource_attr_set(
    nxe_cedar_eval_ctx_t *ctx, ngx_str_t *name);
nxe_cedar_set_t *nxe_cedar_eval_ctx_add_context_attr_set(
    nxe_cedar_eval_ctx_t *ctx, ngx_str_t *name);

ngx_int_t nxe_cedar_eval_ctx_add_principal_attr_entity(
    nxe_cedar_eval_ctx_t *ctx, ngx_str_t *name,
    ngx_str_t *type, ngx_str_t *id);
ngx_int_t nxe_cedar_eval_ctx_add_action_attr_entity(
    nxe_cedar_eval_ctx_t *ctx, ngx_str_t *name,
    ngx_str_t *type, ngx_str_t *id);
ngx_int_t nxe_cedar_eval_ctx_add_resource_attr_entity(
    nxe_cedar_eval_ctx_t *ctx, ngx_str_t *name,
    ngx_str_t *type, ngx_str_t *id);
ngx_int_t nxe_cedar_eval_ctx_add_context_attr_entity(
    nxe_cedar_eval_ctx_t *ctx, ngx_str_t *name,
    ngx_str_t *type, ngx_str_t *id);


/*
 * Set element constructors. Add one element to an existing set
 * handle. nxe_cedar_set_add_set() and nxe_cedar_set_add_record()
 * return a handle for the new nested container; the other variants
 * return NGX_OK / NGX_ERROR.
 *
 * Set handles enforce NXE_CEDAR_MAX_SET_DEPTH for set-in-set nesting
 * and NXE_CEDAR_MAX_RECORD_DEPTH for record values placed inside a
 * set; exceeding either ceiling returns NULL.
 */
ngx_int_t nxe_cedar_set_add_str(nxe_cedar_set_t *set, ngx_str_t *value);
ngx_int_t nxe_cedar_set_add_long(nxe_cedar_set_t *set, int64_t value);
ngx_int_t nxe_cedar_set_add_bool(nxe_cedar_set_t *set, ngx_flag_t value);
ngx_int_t nxe_cedar_set_add_ip(nxe_cedar_set_t *set, ngx_str_t *value);
ngx_int_t nxe_cedar_set_add_decimal(nxe_cedar_set_t *set,
    ngx_str_t *value);
ngx_int_t nxe_cedar_set_add_entity(nxe_cedar_set_t *set,
    ngx_str_t *type, ngx_str_t *id);
nxe_cedar_set_t *nxe_cedar_set_add_set(nxe_cedar_set_t *set);
nxe_cedar_record_t *nxe_cedar_set_add_record(nxe_cedar_set_t *set);


/*
 * Entity hierarchy registration.
 *
 * Each call records one ancestor of the given entity (principal,
 * action, or resource) for `in` evaluation. The caller is responsible
 * for supplying the transitive closure: if `User::"alice"` is a member
 * of `Group::"developers"`, which is a member of `Group::"staff"`,
 * register both `Group::"developers"` and `Group::"staff"` as
 * principal parents. Reflexive membership (`X in X`) is handled by the
 * evaluator and does not need to be registered.
 *
 * Returns NGX_OK on success, NGX_ERROR on allocation failure.
 */
ngx_int_t nxe_cedar_eval_ctx_add_principal_parent(
    nxe_cedar_eval_ctx_t *ctx, ngx_str_t *type, ngx_str_t *id);
ngx_int_t nxe_cedar_eval_ctx_add_action_parent(
    nxe_cedar_eval_ctx_t *ctx, ngx_str_t *type, ngx_str_t *id);
ngx_int_t nxe_cedar_eval_ctx_add_resource_parent(
    nxe_cedar_eval_ctx_t *ctx, ngx_str_t *type, ngx_str_t *id);


/*
 * Internal helpers shared with the expression evaluator. Resolve the
 * ancestor list by the origin slot stamped on the entity value
 * (NXE_CEDAR_ENTITY_SLOT_*); returns NULL for NXE_CEDAR_ENTITY_SLOT_NONE
 * so `in` evaluation falls back to reflexive comparison only. The
 * second helper performs the reflexive + ancestor membership check used
 * by `in` operators.
 */
ngx_array_t *nxe_cedar_eval_ctx_lookup_parents(
    nxe_cedar_eval_ctx_t *ctx, ngx_uint_t slot);
ngx_int_t nxe_cedar_entity_in_target(
    ngx_str_t *entity_type, ngx_str_t *entity_id, ngx_array_t *parents,
    ngx_str_t *target_type, ngx_str_t *target_id);


#endif /* NXE_CEDAR_EVAL_H */
