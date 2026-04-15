/*
 * Copyright (c) Tatsuya Kamijo
 * Copyright (c) Bengo4.com, Inc.
 *
 * nxe_cedar_expr.c - Cedar expression evaluator
 *
 * Recursively evaluates AST nodes.
 * Returns ERROR on missing attributes or type mismatch, making the policy
 * not applicable.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include "nxe_cedar_expr.h"


/* value constructors */

static nxe_cedar_value_t
nxe_cedar_make_error(void)
{
    nxe_cedar_value_t val;

    ngx_memzero(&val, sizeof(nxe_cedar_value_t));
    val.type = NXE_CEDAR_RVAL_ERROR;
    return val;
}


static nxe_cedar_value_t
nxe_cedar_make_bool(ngx_flag_t b)
{
    nxe_cedar_value_t val;

    val.type = NXE_CEDAR_RVAL_BOOL;
    val.v.bool_val = b;
    return val;
}


static nxe_cedar_value_t
nxe_cedar_make_string(ngx_str_t s)
{
    nxe_cedar_value_t val;

    val.type = NXE_CEDAR_RVAL_STRING;
    val.v.str_val = s;
    return val;
}


static nxe_cedar_value_t
nxe_cedar_make_long(ngx_int_t n)
{
    nxe_cedar_value_t val;

    val.type = NXE_CEDAR_RVAL_LONG;
    val.v.long_val = n;
    return val;
}


static nxe_cedar_value_t
nxe_cedar_make_entity(ngx_str_t type, ngx_str_t id)
{
    nxe_cedar_value_t val;

    val.type = NXE_CEDAR_RVAL_ENTITY;
    val.v.entity.type = type;
    val.v.entity.id = id;
    return val;
}


/* value equality (same-type comparison) */
static ngx_int_t
nxe_cedar_value_equals(nxe_cedar_value_t *a, nxe_cedar_value_t *b)
{
    if (a->type != b->type) {
        return 0;
    }

    switch (a->type) {

    case NXE_CEDAR_RVAL_STRING:
        return nxe_cedar_str_eq(&a->v.str_val, &b->v.str_val);

    case NXE_CEDAR_RVAL_LONG:
        return (a->v.long_val == b->v.long_val);

    case NXE_CEDAR_RVAL_BOOL:
        return (a->v.bool_val == b->v.bool_val);

    case NXE_CEDAR_RVAL_ENTITY:
        return (nxe_cedar_str_eq(&a->v.entity.type, &b->v.entity.type)
                && nxe_cedar_str_eq(&a->v.entity.id, &b->v.entity.id));

    case NXE_CEDAR_RVAL_SET:
        if (a->v.set_elts == NULL || b->v.set_elts == NULL) {
            return 0;
        }
        if (a->v.set_elts->nelts != b->v.set_elts->nelts) {
            return 0;
        }
        {
            nxe_cedar_value_t *a_elts = a->v.set_elts->elts;
            nxe_cedar_value_t *b_elts = b->v.set_elts->elts;
            ngx_uint_t i, j;

            for (i = 0; i < a->v.set_elts->nelts; i++) {
                ngx_flag_t found = 0;
                for (j = 0; j < b->v.set_elts->nelts; j++) {
                    if (nxe_cedar_value_equals(&a_elts[i], &b_elts[j])) {
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    return 0;
                }
            }
            return 1;
        }

    default:
        return 0;
    }
}


/* find attribute by name in attr array */
static nxe_cedar_attr_t *
nxe_cedar_find_attr(ngx_array_t *attrs, ngx_str_t *name)
{
    nxe_cedar_attr_t *attr;
    ngx_uint_t i;

    if (attrs == NULL) {
        return NULL;
    }

    attr = attrs->elts;

    for (i = 0; i < attrs->nelts; i++) {
        if (nxe_cedar_str_eq(&attr[i].name, name)) {
            return &attr[i];
        }
    }

    return NULL;
}


/* convert nxe_cedar_attr_t to runtime value */
static nxe_cedar_value_t
nxe_cedar_attr_to_value(nxe_cedar_attr_t *attr)
{
    switch (attr->value_type) {

    case NXE_CEDAR_VALUE_STRING:
        return nxe_cedar_make_string(attr->value.str_val);

    case NXE_CEDAR_VALUE_LONG:
        return nxe_cedar_make_long(attr->value.long_val);

    case NXE_CEDAR_VALUE_BOOL:
        return nxe_cedar_make_bool(attr->value.bool_val);

    default:
        return nxe_cedar_make_error();
    }
}


/* resolve variable type to its attribute array */
static ngx_array_t *
nxe_cedar_resolve_var_attrs(nxe_cedar_var_type_t var_type,
    nxe_cedar_eval_ctx_t *ctx)
{
    switch (var_type) {

    case NXE_CEDAR_VAR_PRINCIPAL:
        return ctx->principal_attrs;

    case NXE_CEDAR_VAR_ACTION:
        return ctx->action_attrs;

    case NXE_CEDAR_VAR_RESOURCE:
        return ctx->resource_attrs;

    case NXE_CEDAR_VAR_CONTEXT:
        return ctx->context_attrs;

    default:
        return NULL;
    }
}


/* evaluate attribute access on a variable node */
static nxe_cedar_value_t
nxe_cedar_eval_attr_access(nxe_cedar_node_t *node,
    nxe_cedar_eval_ctx_t *ctx)
{
    nxe_cedar_node_t *object;
    ngx_array_t *attrs;
    nxe_cedar_attr_t *attr;

    object = node->u.attr_access.object;

    /* fast path: direct variable access */
    if (object->type == NXE_CEDAR_NODE_VAR) {
        attrs = nxe_cedar_resolve_var_attrs(object->u.var_type, ctx);
        if (attrs == NULL) {
            return nxe_cedar_make_error();
        }

        attr = nxe_cedar_find_attr(attrs, &node->u.attr_access.attr);
        if (attr == NULL) {
            return nxe_cedar_make_error();
        }

        return nxe_cedar_attr_to_value(attr);
    }

    /* nested access not supported in Phase 1 */
    return nxe_cedar_make_error();
}


/* evaluate has expression: check if attribute exists */
static nxe_cedar_value_t
nxe_cedar_eval_has(nxe_cedar_node_t *node,
    nxe_cedar_eval_ctx_t *ctx)
{
    nxe_cedar_node_t *object;
    ngx_array_t *attrs;

    object = node->u.has.object;

    if (object->type == NXE_CEDAR_NODE_VAR) {
        attrs = nxe_cedar_resolve_var_attrs(object->u.var_type, ctx);
        if (attrs == NULL) {
            return nxe_cedar_make_bool(0);
        }

        return nxe_cedar_make_bool(
            nxe_cedar_find_attr(attrs, &node->u.has.attr) != NULL);
    }

    /* nested access (e.g. record has field) not yet supported */
    return nxe_cedar_make_error();
}


/*
 * Wildcard pattern matching for like operator.
 * Pattern bytes: 0xFF = wildcard (matches 0+ chars), all others = literal.
 * Uses a greedy/backtracking approach.
 *
 * Invariant: patterns are always produced by
 * nxe_cedar_parser_compile_pattern(), which guarantees that 0xFF bytes
 * in the pattern are exclusively wildcard markers.  Subject strings may
 * contain arbitrary bytes (including 0xFF in non-UTF-8 input), but this
 * is safe: pattern-side 0xFF is always consumed first by the wildcard
 * branch (line *p == 0xFF), so it never reaches the literal comparison.
 */
static ngx_flag_t
nxe_cedar_like_match(ngx_str_t *subject, ngx_str_t *pattern)
{
    u_char *s, *p, *s_end, *p_end;
    u_char *star_p, *star_s;

    s = subject->data;
    s_end = s + subject->len;
    p = pattern->data;
    p_end = p + pattern->len;
    star_p = NULL;
    star_s = NULL;

    while (s < s_end) {
        if (p < p_end && *p == 0xFF) {
            /* wildcard: record position for backtracking */
            star_p = ++p;
            star_s = s;
            continue;
        }

        if (p < p_end && *p == *s) {
            p++;
            s++;
            continue;
        }

        /* mismatch: backtrack to last wildcard */
        if (star_p != NULL) {
            p = star_p;
            s = ++star_s;
            continue;
        }

        return 0;
    }

    /* consume trailing wildcards in pattern */
    while (p < p_end && *p == 0xFF) {
        p++;
    }

    return (p == p_end);
}


/* evaluate method call: expr.method(arg) */
static nxe_cedar_value_t
nxe_cedar_eval_method_call(nxe_cedar_node_t *node,
    nxe_cedar_eval_ctx_t *ctx, ngx_pool_t *pool,
    ngx_log_t *log)
{
    nxe_cedar_value_t obj, arg;
    ngx_str_t *method;
    nxe_cedar_value_t *obj_elts, *arg_elts;
    ngx_uint_t i, j;

    obj = nxe_cedar_expr_eval(node->u.method_call.object, ctx,
                              pool, log);
    if (obj.type == NXE_CEDAR_RVAL_ERROR) {
        return obj;
    }

    arg = nxe_cedar_expr_eval(node->u.method_call.arg, ctx,
                              pool, log);
    if (arg.type == NXE_CEDAR_RVAL_ERROR) {
        return arg;
    }

    method = &node->u.method_call.method;

    /* containsAll */
    if (method->len == 11
        && ngx_memcmp(method->data, "containsAll", 11) == 0)
    {
        if (obj.type != NXE_CEDAR_RVAL_SET
            || arg.type != NXE_CEDAR_RVAL_SET)
        {
            return nxe_cedar_make_error();
        }

        if (obj.v.set_elts == NULL || arg.v.set_elts == NULL) {
            return nxe_cedar_make_error();
        }

        obj_elts = obj.v.set_elts->elts;
        arg_elts = arg.v.set_elts->elts;

        /* every element in arg must exist in obj */
        for (i = 0; i < arg.v.set_elts->nelts; i++) {
            ngx_flag_t found = 0;

            for (j = 0; j < obj.v.set_elts->nelts; j++) {
                if (nxe_cedar_value_equals(&arg_elts[i],
                                           &obj_elts[j]))
                {
                    found = 1;
                    break;
                }
            }

            if (!found) {
                return nxe_cedar_make_bool(0);
            }
        }

        return nxe_cedar_make_bool(1);
    }

    /* containsAny */
    if (method->len == 11
        && ngx_memcmp(method->data, "containsAny", 11) == 0)
    {
        if (obj.type != NXE_CEDAR_RVAL_SET
            || arg.type != NXE_CEDAR_RVAL_SET)
        {
            return nxe_cedar_make_error();
        }

        if (obj.v.set_elts == NULL || arg.v.set_elts == NULL) {
            return nxe_cedar_make_error();
        }

        obj_elts = obj.v.set_elts->elts;
        arg_elts = arg.v.set_elts->elts;

        /* at least one element in arg must exist in obj */
        for (i = 0; i < arg.v.set_elts->nelts; i++) {
            for (j = 0; j < obj.v.set_elts->nelts; j++) {
                if (nxe_cedar_value_equals(&arg_elts[i],
                                           &obj_elts[j]))
                {
                    return nxe_cedar_make_bool(1);
                }
            }
        }

        return nxe_cedar_make_bool(0);
    }

    /* contains (single element membership) */
    if (method->len == 8
        && ngx_memcmp(method->data, "contains", 8) == 0)
    {
        if (obj.type != NXE_CEDAR_RVAL_SET) {
            return nxe_cedar_make_error();
        }

        if (obj.v.set_elts == NULL) {
            return nxe_cedar_make_error();
        }

        obj_elts = obj.v.set_elts->elts;

        for (i = 0; i < obj.v.set_elts->nelts; i++) {
            if (nxe_cedar_value_equals(&obj_elts[i], &arg)) {
                return nxe_cedar_make_bool(1);
            }
        }

        return nxe_cedar_make_bool(0);
    }

    /* unknown method */
    return nxe_cedar_make_error();
}


/* entity in entity-or-set check */
static nxe_cedar_value_t
nxe_cedar_eval_in(nxe_cedar_value_t *left, nxe_cedar_value_t *right)
{
    nxe_cedar_value_t *elts;
    ngx_uint_t i;

    if (left->type != NXE_CEDAR_RVAL_ENTITY) {
        return nxe_cedar_make_error();
    }

    /* entity in entity: no hierarchy, degrades to == */
    if (right->type == NXE_CEDAR_RVAL_ENTITY) {
        return nxe_cedar_make_bool(
            nxe_cedar_value_equals(left, right));
    }

    /* entity in set: check if any element matches */
    if (right->type == NXE_CEDAR_RVAL_SET) {
        if (right->v.set_elts == NULL) {
            return nxe_cedar_make_bool(0);
        }

        elts = right->v.set_elts->elts;

        for (i = 0; i < right->v.set_elts->nelts; i++) {
            if (nxe_cedar_value_equals(left, &elts[i])) {
                return nxe_cedar_make_bool(1);
            }
        }

        return nxe_cedar_make_bool(0);
    }

    return nxe_cedar_make_error();
}


nxe_cedar_value_t
nxe_cedar_expr_eval(nxe_cedar_node_t *node,
    nxe_cedar_eval_ctx_t *ctx, ngx_pool_t *pool,
    ngx_log_t *log)
{
    nxe_cedar_value_t left, right, val;
    nxe_cedar_node_t **node_elts;
    nxe_cedar_value_t *val_slot;
    ngx_uint_t i;

    if (node == NULL) {
        return nxe_cedar_make_error();
    }

    switch (node->type) {

    case NXE_CEDAR_NODE_BOOL_LIT:
        return nxe_cedar_make_bool(node->u.bool_val);

    case NXE_CEDAR_NODE_STRING_LIT:
        return nxe_cedar_make_string(node->u.string_val);

    case NXE_CEDAR_NODE_LONG_LIT:
        return nxe_cedar_make_long(node->u.long_val);

    case NXE_CEDAR_NODE_ENTITY_REF:
        return nxe_cedar_make_entity(node->u.entity_ref.entity_type,
                                     node->u.entity_ref.entity_id);

    case NXE_CEDAR_NODE_VAR:
        switch (node->u.var_type) {
        case NXE_CEDAR_VAR_PRINCIPAL:
            return nxe_cedar_make_entity(ctx->principal_type,
                                         ctx->principal_id);
        case NXE_CEDAR_VAR_ACTION:
            return nxe_cedar_make_entity(ctx->action_type,
                                         ctx->action_id);
        case NXE_CEDAR_VAR_RESOURCE:
            return nxe_cedar_make_entity(ctx->resource_type,
                                         ctx->resource_id);
        case NXE_CEDAR_VAR_CONTEXT:
            /* context alone is not a value; only context.attr */
            return nxe_cedar_make_error();
        default:
            return nxe_cedar_make_error();
        }

    case NXE_CEDAR_NODE_ATTR_ACCESS:
        return nxe_cedar_eval_attr_access(node, ctx);

    case NXE_CEDAR_NODE_SET:
        if (node->u.set_elts == NULL) {
            return nxe_cedar_make_error();
        }

        val.type = NXE_CEDAR_RVAL_SET;
        val.v.set_elts = ngx_array_create(pool,
                                          node->u.set_elts->nelts,
                                          sizeof(nxe_cedar_value_t));
        if (val.v.set_elts == NULL) {
            return nxe_cedar_make_error();
        }

        node_elts = node->u.set_elts->elts;

        for (i = 0; i < node->u.set_elts->nelts; i++) {
            left = nxe_cedar_expr_eval(node_elts[i], ctx, pool, log);
            if (left.type == NXE_CEDAR_RVAL_ERROR) {
                return nxe_cedar_make_error();
            }

            val_slot = ngx_array_push(val.v.set_elts);
            if (val_slot == NULL) {
                return nxe_cedar_make_error();
            }
            *val_slot = left;
        }

        return val;

    case NXE_CEDAR_NODE_BINOP:
        switch (node->u.binop.op) {

        case NXE_CEDAR_OP_AND:
            left = nxe_cedar_expr_eval(node->u.binop.left, ctx,
                                       pool, log);
            if (left.type == NXE_CEDAR_RVAL_ERROR) {
                return left;
            }
            if (left.type != NXE_CEDAR_RVAL_BOOL) {
                return nxe_cedar_make_error();
            }
            if (!left.v.bool_val) {
                return nxe_cedar_make_bool(0);
            }

            right = nxe_cedar_expr_eval(node->u.binop.right, ctx,
                                        pool, log);
            if (right.type == NXE_CEDAR_RVAL_ERROR) {
                return right;
            }
            if (right.type != NXE_CEDAR_RVAL_BOOL) {
                return nxe_cedar_make_error();
            }
            return right;

        case NXE_CEDAR_OP_OR:
            left = nxe_cedar_expr_eval(node->u.binop.left, ctx,
                                       pool, log);
            if (left.type == NXE_CEDAR_RVAL_ERROR) {
                return left;
            }
            if (left.type != NXE_CEDAR_RVAL_BOOL) {
                return nxe_cedar_make_error();
            }
            if (left.v.bool_val) {
                return nxe_cedar_make_bool(1);
            }

            right = nxe_cedar_expr_eval(node->u.binop.right, ctx,
                                        pool, log);
            if (right.type == NXE_CEDAR_RVAL_ERROR) {
                return right;
            }
            if (right.type != NXE_CEDAR_RVAL_BOOL) {
                return nxe_cedar_make_error();
            }
            return right;

        case NXE_CEDAR_OP_EQ:
            left = nxe_cedar_expr_eval(node->u.binop.left, ctx,
                                       pool, log);
            if (left.type == NXE_CEDAR_RVAL_ERROR) {
                return left;
            }
            right = nxe_cedar_expr_eval(node->u.binop.right, ctx,
                                        pool, log);
            if (right.type == NXE_CEDAR_RVAL_ERROR) {
                return right;
            }
            if (left.type != right.type) {
                return nxe_cedar_make_error();
            }
            return nxe_cedar_make_bool(
                nxe_cedar_value_equals(&left, &right));

        case NXE_CEDAR_OP_NE:
            left = nxe_cedar_expr_eval(node->u.binop.left, ctx,
                                       pool, log);
            if (left.type == NXE_CEDAR_RVAL_ERROR) {
                return left;
            }
            right = nxe_cedar_expr_eval(node->u.binop.right, ctx,
                                        pool, log);
            if (right.type == NXE_CEDAR_RVAL_ERROR) {
                return right;
            }
            if (left.type != right.type) {
                return nxe_cedar_make_error();
            }
            return nxe_cedar_make_bool(
                !nxe_cedar_value_equals(&left, &right));

        case NXE_CEDAR_OP_IN:
            left = nxe_cedar_expr_eval(node->u.binop.left, ctx,
                                       pool, log);
            if (left.type == NXE_CEDAR_RVAL_ERROR) {
                return left;
            }
            right = nxe_cedar_expr_eval(node->u.binop.right, ctx,
                                        pool, log);
            if (right.type == NXE_CEDAR_RVAL_ERROR) {
                return right;
            }
            return nxe_cedar_eval_in(&left, &right);

        case NXE_CEDAR_OP_LT:
        case NXE_CEDAR_OP_GT:
        case NXE_CEDAR_OP_LE:
        case NXE_CEDAR_OP_GE:
            left = nxe_cedar_expr_eval(node->u.binop.left, ctx,
                                       pool, log);
            if (left.type == NXE_CEDAR_RVAL_ERROR) {
                return left;
            }
            right = nxe_cedar_expr_eval(node->u.binop.right, ctx,
                                        pool, log);
            if (right.type == NXE_CEDAR_RVAL_ERROR) {
                return right;
            }
            if (left.type != NXE_CEDAR_RVAL_LONG
                || right.type != NXE_CEDAR_RVAL_LONG)
            {
                return nxe_cedar_make_error();
            }

            switch (node->u.binop.op) {
            case NXE_CEDAR_OP_LT:
                return nxe_cedar_make_bool(
                    left.v.long_val < right.v.long_val);
            case NXE_CEDAR_OP_GT:
                return nxe_cedar_make_bool(
                    left.v.long_val > right.v.long_val);
            case NXE_CEDAR_OP_LE:
                return nxe_cedar_make_bool(
                    left.v.long_val <= right.v.long_val);
            case NXE_CEDAR_OP_GE:
                return nxe_cedar_make_bool(
                    left.v.long_val >= right.v.long_val);
            default:
                return nxe_cedar_make_error();
            }

        default:
            return nxe_cedar_make_error();
        }

    case NXE_CEDAR_NODE_UNOP:
        left = nxe_cedar_expr_eval(node->u.unop.operand, ctx,
                                   pool, log);
        if (left.type == NXE_CEDAR_RVAL_ERROR) {
            return left;
        }
        if (left.type != NXE_CEDAR_RVAL_BOOL) {
            return nxe_cedar_make_error();
        }
        return nxe_cedar_make_bool(!left.v.bool_val);

    case NXE_CEDAR_NODE_NEGATE:
        left = nxe_cedar_expr_eval(node->u.unop.operand, ctx,
                                   pool, log);
        if (left.type == NXE_CEDAR_RVAL_ERROR) {
            return left;
        }
        if (left.type != NXE_CEDAR_RVAL_LONG) {
            return nxe_cedar_make_error();
        }
        /* -MIN_INT is undefined; reject it */
        if (left.v.long_val == (-NGX_MAX_INT_T_VALUE - 1)) {
            return nxe_cedar_make_error();
        }
        return nxe_cedar_make_long(-left.v.long_val);

    /* Phase 2 */
    case NXE_CEDAR_NODE_HAS:
        return nxe_cedar_eval_has(node, ctx);

    case NXE_CEDAR_NODE_LIKE:
        left = nxe_cedar_expr_eval(node->u.like.object, ctx,
                                   pool, log);
        if (left.type == NXE_CEDAR_RVAL_ERROR) {
            return left;
        }
        if (left.type != NXE_CEDAR_RVAL_STRING) {
            return nxe_cedar_make_error();
        }
        return nxe_cedar_make_bool(
            nxe_cedar_like_match(&left.v.str_val,
                                 &node->u.like.pattern));

    case NXE_CEDAR_NODE_METHOD_CALL:
        return nxe_cedar_eval_method_call(node, ctx, pool, log);

    case NXE_CEDAR_NODE_IF_THEN_ELSE:
        left = nxe_cedar_expr_eval(node->u.if_then_else.cond, ctx,
                                   pool, log);
        if (left.type == NXE_CEDAR_RVAL_ERROR) {
            return left;
        }
        if (left.type != NXE_CEDAR_RVAL_BOOL) {
            return nxe_cedar_make_error();
        }
        if (left.v.bool_val) {
            return nxe_cedar_expr_eval(
                node->u.if_then_else.then_expr, ctx, pool, log);
        }
        return nxe_cedar_expr_eval(
            node->u.if_then_else.else_expr, ctx, pool, log);

    default:
        return nxe_cedar_make_error();
    }
}
