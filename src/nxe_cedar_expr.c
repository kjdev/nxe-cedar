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

    ngx_memzero(&val, sizeof(nxe_cedar_value_t));
    val.type = NXE_CEDAR_RVAL_BOOL;
    val.v.bool_val = b;
    return val;
}


static nxe_cedar_value_t
nxe_cedar_make_string(ngx_str_t s)
{
    nxe_cedar_value_t val;

    ngx_memzero(&val, sizeof(nxe_cedar_value_t));
    val.type = NXE_CEDAR_RVAL_STRING;
    val.v.str_val = s;
    return val;
}


static nxe_cedar_value_t
nxe_cedar_make_long(int64_t n)
{
    nxe_cedar_value_t val;

    ngx_memzero(&val, sizeof(nxe_cedar_value_t));
    val.type = NXE_CEDAR_RVAL_LONG;
    val.v.long_val = n;
    return val;
}


static nxe_cedar_value_t
nxe_cedar_make_entity(ngx_str_t type, ngx_str_t id)
{
    nxe_cedar_value_t val;

    ngx_memzero(&val, sizeof(nxe_cedar_value_t));
    val.type = NXE_CEDAR_RVAL_ENTITY;
    val.v.entity.type = type;
    val.v.entity.id = id;
    return val;
}


static nxe_cedar_value_t
nxe_cedar_make_record(ngx_array_t *attrs)
{
    nxe_cedar_value_t val;

    ngx_memzero(&val, sizeof(nxe_cedar_value_t));
    val.type = NXE_CEDAR_RVAL_RECORD;
    val.v.record_attrs = attrs;
    return val;
}


/* parse bounded decimal: overflow-safe with leading-zero rejection */
static ngx_int_t
nxe_cedar_parse_bounded_dec(u_char **pp, u_char *end, ngx_uint_t max,
    ngx_uint_t *out)
{
    u_char *p, *start;
    ngx_uint_t val, digit;

    p = *pp;
    start = p;
    val = 0;

    if (p >= end || *p < '0' || *p > '9') {
        return NGX_ERROR;
    }

    while (p < end && *p >= '0' && *p <= '9') {
        digit = *p - '0';
        if (val > (max - digit) / 10) {
            return NGX_ERROR;
        }
        val = val * 10 + digit;
        p++;
    }

    /* reject leading zeros (e.g. "08", "010") */
    if (p - start > 1 && *start == '0') {
        return NGX_ERROR;
    }

    *out = val;
    *pp = p;

    return NGX_OK;
}


/* parse CIDR prefix length: digits after '/' with leading-zero rejection */
static ngx_int_t
nxe_cedar_parse_cidr_prefix(u_char **pp, u_char *end,
    ngx_uint_t max_prefix, ngx_uint_t *prefix_len)
{
    return nxe_cedar_parse_bounded_dec(pp, end, max_prefix, prefix_len);
}


/* parse IPv4 address: "a.b.c.d" with optional "/prefix" */
static ngx_int_t
nxe_cedar_parse_ipv4(u_char *data, size_t len,
    u_char *addr, ngx_uint_t *prefix_len)
{
    u_char *p, *end;
    ngx_uint_t octet, i;

    p = data;
    end = data + len;

    for (i = 0; i < 4; i++) {

        if (nxe_cedar_parse_bounded_dec(&p, end, 255, &octet) != NGX_OK) {
            return NGX_ERROR;
        }

        addr[i] = (u_char) octet;

        if (i < 3) {
            if (p >= end || *p != '.') {
                return NGX_ERROR;
            }
            p++;
        }
    }

    if (p < end && *p == '/') {
        p++;

        if (nxe_cedar_parse_cidr_prefix(&p, end, 32, prefix_len)
            != NGX_OK)
        {
            return NGX_ERROR;
        }

    } else {
        *prefix_len = 32;
    }

    if (p != end) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


/* parse IPv6 address with optional "/prefix" */
static ngx_int_t
nxe_cedar_parse_ipv6(u_char *data, size_t len,
    u_char *addr, ngx_uint_t *prefix_len)
{
    u_char *p, *end, *slash;
    ngx_uint_t groups[8], n_groups, gap_pos, i, val;
    size_t addr_len;

    p = data;

    /* split off /prefix if present */
    slash = memchr(data, '/', len);

    if (slash != NULL) {
        addr_len = slash - data;
    } else {
        addr_len = len;
    }

    end = data + addr_len;
    ngx_memzero(groups, sizeof(groups));
    n_groups = 0;
    gap_pos = 8; /* sentinel: no gap */

    /* handle leading "::" */
    if (addr_len >= 2 && p[0] == ':' && p[1] == ':') {
        gap_pos = 0;
        p += 2;

        if (p == end) {
            /* just "::" */
            goto done_groups;
        }
    }

    while (p < end) {
        ngx_uint_t digits;

        if (n_groups >= 8) {
            return NGX_ERROR;
        }

        val = 0;
        digits = 0;

        if (*p < '0'
            || (*p > '9' && *p < 'A')
            || (*p > 'F' && *p < 'a')
            || *p > 'f')
        {
            return NGX_ERROR;
        }

        while (p < end && *p != ':') {
            if (++digits > 4) {
                return NGX_ERROR;
            }

            if (*p >= '0' && *p <= '9') {
                val = (val << 4) + (*p - '0');
            } else if (*p >= 'a' && *p <= 'f') {
                val = (val << 4) + (*p - 'a' + 10);
            } else if (*p >= 'A' && *p <= 'F') {
                val = (val << 4) + (*p - 'A' + 10);
            } else {
                return NGX_ERROR;
            }

            p++;
        }

        groups[n_groups++] = val;

        if (p < end && *p == ':') {
            p++;

            if (p < end && *p == ':') {
                if (gap_pos != 8) {
                    return NGX_ERROR; /* double :: */
                }
                gap_pos = n_groups;
                p++;

                if (p == end) {
                    break;
                }

            } else if (p >= end) {
                return NGX_ERROR; /* trailing single colon */
            }
        }
    }

done_groups:

    /* expand :: gap into 16-byte addr */
    ngx_memzero(addr, 16);

    if (gap_pos == 8) {
        /* no gap: must have exactly 8 groups */
        if (n_groups != 8) {
            return NGX_ERROR;
        }

        for (i = 0; i < 8; i++) {
            addr[i * 2] = (u_char) (groups[i] >> 8);
            addr[i * 2 + 1] = (u_char) (groups[i] & 0xFF);
        }

    } else {
        ngx_uint_t tail;

        if (n_groups < gap_pos) {
            return NGX_ERROR;
        }

        tail = n_groups - gap_pos;

        /* :: must expand to at least one zero group */
        if (gap_pos + tail >= 8) {
            return NGX_ERROR;
        }

        for (i = 0; i < gap_pos; i++) {
            addr[i * 2] = (u_char) (groups[i] >> 8);
            addr[i * 2 + 1] = (u_char) (groups[i] & 0xFF);
        }

        for (i = 0; i < tail; i++) {
            ngx_uint_t pos = 8 - tail + i;
            addr[pos * 2] =
                (u_char) (groups[gap_pos + i] >> 8);
            addr[pos * 2 + 1] =
                (u_char) (groups[gap_pos + i] & 0xFF);
        }
    }

    /* parse prefix */
    if (slash != NULL) {
        p = slash + 1;
        end = data + len;

        if (nxe_cedar_parse_cidr_prefix(&p, end, 128, prefix_len)
            != NGX_OK)
        {
            return NGX_ERROR;
        }

        if (p != end) {
            return NGX_ERROR;
        }

    } else {
        *prefix_len = 128;
    }

    return NGX_OK;
}


/* parse IP string to binary runtime value */
nxe_cedar_value_t
nxe_cedar_make_ip(ngx_str_t *s)
{
    nxe_cedar_value_t val;

    /* max valid: "xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:xxxx/128" = 43 chars;
     * use 45 as a conservative upper bound */
    if (s->len == 0 || s->len > 45) {
        return nxe_cedar_make_error();
    }

    /*
     * zero the entire value including addr[4..15] so IPv4
     * (which only writes addr[0..3]) leaves no uninitialised bytes
     */
    ngx_memzero(&val, sizeof(nxe_cedar_value_t));
    val.type = NXE_CEDAR_RVAL_IP;

    /* try IPv4 first (contains dots, no colons) */
    if (memchr(s->data, ':', s->len) == NULL) {
        if (nxe_cedar_parse_ipv4(s->data, s->len,
                                 val.v.ip_addr.addr,
                                 &val.v.ip_addr.prefix_len)
            != NGX_OK)
        {
            return nxe_cedar_make_error();
        }

        val.v.ip_addr.is_ipv6 = 0;
        return val;
    }

    /* IPv6 */
    if (nxe_cedar_parse_ipv6(s->data, s->len,
                             val.v.ip_addr.addr,
                             &val.v.ip_addr.prefix_len)
        != NGX_OK)
    {
        return nxe_cedar_make_error();
    }

    val.v.ip_addr.is_ipv6 = 1;
    return val;
}


/* overflow-checked Long arithmetic (Cedar i64::checked_{add,sub,mul}).
 * Accepts any result representable in int64_t (including INT64_MIN);
 * rejects true overflow. */
static ngx_int_t
nxe_cedar_long_arith(nxe_cedar_op_t op, int64_t a, int64_t b, int64_t *out)
{
    switch (op) {
    case NXE_CEDAR_OP_PLUS:
        return __builtin_add_overflow(a, b, out) ? NGX_ERROR : NGX_OK;
    case NXE_CEDAR_OP_MINUS:
        return __builtin_sub_overflow(a, b, out) ? NGX_ERROR : NGX_OK;
    case NXE_CEDAR_OP_MUL:
        return __builtin_mul_overflow(a, b, out) ? NGX_ERROR : NGX_OK;
    default:
        return NGX_ERROR;
    }
}


/*
 * Tri-state value equality: returns 1 (equal), 0 (not equal), or
 * NGX_ERROR when either operand is RVAL_ERROR. Defense in depth: the
 * normal evaluation paths reject RVAL_ERROR before storing it in
 * record_attrs / set_elts, so NGX_ERROR is not expected in practice;
 * callers must still propagate it as nxe_cedar_make_error() instead of
 * treating it as "not equal".
 */
static ngx_int_t
nxe_cedar_value_equals(nxe_cedar_value_t *a, nxe_cedar_value_t *b)
{
    if (a->type == NXE_CEDAR_RVAL_ERROR
        || b->type == NXE_CEDAR_RVAL_ERROR)
    {
        return NGX_ERROR;
    }

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

    case NXE_CEDAR_RVAL_IP:
        return (a->v.ip_addr.is_ipv6 == b->v.ip_addr.is_ipv6
                && a->v.ip_addr.prefix_len == b->v.ip_addr.prefix_len
                && ngx_memcmp(a->v.ip_addr.addr, b->v.ip_addr.addr,
                              a->v.ip_addr.is_ipv6 ? 16 : 4) == 0);

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
                    ngx_int_t r = nxe_cedar_value_equals(&a_elts[i],
                                                         &b_elts[j]);
                    if (r == NGX_ERROR) {
                        return NGX_ERROR;
                    }
                    if (r) {
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

    case NXE_CEDAR_RVAL_RECORD:
        if (a->v.record_attrs == NULL || b->v.record_attrs == NULL) {
            return 0;
        }
        if (a->v.record_attrs->nelts != b->v.record_attrs->nelts) {
            return 0;
        }
        {
            nxe_cedar_attr_t *a_attrs = a->v.record_attrs->elts;
            nxe_cedar_attr_t *b_attrs = b->v.record_attrs->elts;
            ngx_uint_t i, j;

            for (i = 0; i < a->v.record_attrs->nelts; i++) {
                ngx_flag_t found = 0;
                for (j = 0; j < b->v.record_attrs->nelts; j++) {
                    if (nxe_cedar_str_eq(&a_attrs[i].name,
                                         &b_attrs[j].name))
                    {
                        ngx_int_t r = nxe_cedar_value_equals(
                            &a_attrs[i].value, &b_attrs[j].value);
                        if (r == NGX_ERROR) {
                            return NGX_ERROR;
                        }
                        if (r) {
                            found = 1;
                            break;
                        }
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


/*
 * Evaluate attribute access expr.attr.
 *
 * Fast path: object is a VAR (principal/action/resource/context) — look
 * up the attribute directly from the corresponding eval_ctx array.
 *
 * Slow path: object is any other expression (nested ATTR_ACCESS, etc.) —
 * evaluate it recursively. If the result is a record, look up the
 * attribute from the record's attr array; otherwise return error.
 */
static nxe_cedar_value_t
nxe_cedar_eval_attr_access(nxe_cedar_node_t *node,
    nxe_cedar_eval_ctx_t *ctx, ngx_pool_t *pool, ngx_log_t *log)
{
    nxe_cedar_node_t *object;
    ngx_array_t *attrs;
    nxe_cedar_attr_t *attr;
    nxe_cedar_value_t obj_val;

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

        return attr->value;
    }

    /* slow path: evaluate object and descend into record */
    obj_val = nxe_cedar_expr_eval(object, ctx, pool, log);
    if (obj_val.type == NXE_CEDAR_RVAL_ERROR) {
        return obj_val;
    }
    if (obj_val.type != NXE_CEDAR_RVAL_RECORD) {
        return nxe_cedar_make_error();
    }

    attr = nxe_cedar_find_attr(obj_val.v.record_attrs,
                               &node->u.attr_access.attr);
    if (attr == NULL) {
        return nxe_cedar_make_error();
    }

    return attr->value;
}


/*
 * Evaluate has expression.
 *
 * Fast path: object is a VAR — check the corresponding eval_ctx array.
 *
 * Slow path: object is any other expression — evaluate it. If the
 * result is a record, return whether the attribute exists. If the
 * object evaluation fails or produces a non-record, `has` is an error
 * per Cedar semantics (the expression is not applicable to the object).
 */
static nxe_cedar_value_t
nxe_cedar_eval_has(nxe_cedar_node_t *node,
    nxe_cedar_eval_ctx_t *ctx, ngx_pool_t *pool, ngx_log_t *log)
{
    nxe_cedar_node_t *object;
    ngx_array_t *attrs;
    nxe_cedar_value_t obj_val;

    object = node->u.has.object;

    if (object->type == NXE_CEDAR_NODE_VAR) {
        attrs = nxe_cedar_resolve_var_attrs(object->u.var_type, ctx);
        if (attrs == NULL) {
            return nxe_cedar_make_bool(0);
        }

        return nxe_cedar_make_bool(
            nxe_cedar_find_attr(attrs, &node->u.has.attr) != NULL);
    }

    obj_val = nxe_cedar_expr_eval(object, ctx, pool, log);
    if (obj_val.type == NXE_CEDAR_RVAL_ERROR) {
        return obj_val;
    }
    if (obj_val.type != NXE_CEDAR_RVAL_RECORD) {
        return nxe_cedar_make_error();
    }

    return nxe_cedar_make_bool(
        nxe_cedar_find_attr(obj_val.v.record_attrs,
                            &node->u.has.attr) != NULL);
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


/* CIDR containment: true if obj (host or range) is entirely within range */
static ngx_flag_t
nxe_cedar_ip_cidr_contains(nxe_cedar_value_t *obj,
    nxe_cedar_value_t *range)
{
    ngx_uint_t addr_len, full_bytes, remaining_bits;
    u_char mask;

    if (obj->v.ip_addr.is_ipv6 != range->v.ip_addr.is_ipv6) {
        return 0;
    }

    if (obj->v.ip_addr.prefix_len < range->v.ip_addr.prefix_len) {
        return 0;
    }

    addr_len = obj->v.ip_addr.is_ipv6 ? 16 : 4;
    full_bytes = range->v.ip_addr.prefix_len / 8;
    remaining_bits = range->v.ip_addr.prefix_len % 8;

    if (full_bytes > 0
        && ngx_memcmp(obj->v.ip_addr.addr,
                      range->v.ip_addr.addr, full_bytes) != 0)
    {
        return 0;
    }

    if (remaining_bits > 0 && full_bytes < addr_len) {
        mask = (u_char) (0xFF << (8 - remaining_bits));

        if ((obj->v.ip_addr.addr[full_bytes] & mask)
            != (range->v.ip_addr.addr[full_bytes] & mask))
        {
            return 0;
        }
    }

    return 1;
}


/* build well-known IP CIDR range from a fixed prefix byte sequence */
static nxe_cedar_value_t
nxe_cedar_make_ip_range(ngx_flag_t is_ipv6,
    const u_char *prefix_bytes, ngx_uint_t prefix_len)
{
    nxe_cedar_value_t val;
    ngx_uint_t addr_len;

    ngx_memzero(&val, sizeof(nxe_cedar_value_t));
    val.type = NXE_CEDAR_RVAL_IP;
    val.v.ip_addr.is_ipv6 = is_ipv6;
    val.v.ip_addr.prefix_len = prefix_len;
    addr_len = is_ipv6 ? 16 : 4;
    ngx_memcpy(val.v.ip_addr.addr, prefix_bytes, addr_len);
    return val;
}


/* evaluate method call: expr.method(arg) or expr.method() */
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

    method = &node->u.method_call.method;

    /* zero-argument methods */
    if (node->u.method_call.arg == NULL) {
        /* isEmpty: receiver must be a Set */
        if (method->len == 7
            && ngx_memcmp(method->data, "isEmpty", 7) == 0)
        {
            if (obj.type != NXE_CEDAR_RVAL_SET) {
                return nxe_cedar_make_error();
            }

            if (obj.v.set_elts == NULL) {
                return nxe_cedar_make_error();
            }

            return nxe_cedar_make_bool(obj.v.set_elts->nelts == 0);
        }

        /* IP inspection methods: receiver must be IP */
        if (obj.type != NXE_CEDAR_RVAL_IP) {
            return nxe_cedar_make_error();
        }

        /* isIpv4 */
        if (method->len == 6
            && ngx_memcmp(method->data, "isIpv4", 6) == 0)
        {
            return nxe_cedar_make_bool(!obj.v.ip_addr.is_ipv6);
        }

        /* isIpv6 */
        if (method->len == 6
            && ngx_memcmp(method->data, "isIpv6", 6) == 0)
        {
            return nxe_cedar_make_bool(obj.v.ip_addr.is_ipv6);
        }

        /* isLoopback: IPv4 127.0.0.0/8, IPv6 ::1/128 */
        if (method->len == 10
            && ngx_memcmp(method->data, "isLoopback", 10) == 0)
        {
            static const u_char loopback_v4[4] = { 127, 0, 0, 0 };
            static const u_char loopback_v6[16] = {
                0, 0, 0, 0, 0, 0, 0, 0,
                0, 0, 0, 0, 0, 0, 0, 1
            };
            nxe_cedar_value_t range;

            if (obj.v.ip_addr.is_ipv6) {
                range = nxe_cedar_make_ip_range(1, loopback_v6, 128);

            } else {
                range = nxe_cedar_make_ip_range(0, loopback_v4, 8);
            }

            return nxe_cedar_make_bool(
                nxe_cedar_ip_cidr_contains(&obj, &range));
        }

        /* isMulticast: IPv4 224.0.0.0/4, IPv6 ff00::/8 */
        if (method->len == 11
            && ngx_memcmp(method->data, "isMulticast", 11) == 0)
        {
            static const u_char multicast_v4[4] = { 224, 0, 0, 0 };
            static const u_char multicast_v6[16] = {
                0xff, 0, 0, 0, 0, 0, 0, 0,
                0,    0, 0, 0, 0, 0, 0, 0
            };
            nxe_cedar_value_t range;

            if (obj.v.ip_addr.is_ipv6) {
                range = nxe_cedar_make_ip_range(1, multicast_v6, 8);

            } else {
                range = nxe_cedar_make_ip_range(0, multicast_v4, 4);
            }

            return nxe_cedar_make_bool(
                nxe_cedar_ip_cidr_contains(&obj, &range));
        }

        /* unknown zero-arg method */
        return nxe_cedar_make_error();
    }

    arg = nxe_cedar_expr_eval(node->u.method_call.arg, ctx,
                              pool, log);
    if (arg.type == NXE_CEDAR_RVAL_ERROR) {
        return arg;
    }

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
                ngx_int_t r = nxe_cedar_value_equals(&arg_elts[i],
                                                     &obj_elts[j]);
                if (r == NGX_ERROR) {
                    return nxe_cedar_make_error();
                }
                if (r) {
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
                ngx_int_t r = nxe_cedar_value_equals(&arg_elts[i],
                                                     &obj_elts[j]);
                if (r == NGX_ERROR) {
                    return nxe_cedar_make_error();
                }
                if (r) {
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
            ngx_int_t r = nxe_cedar_value_equals(&obj_elts[i], &arg);
            if (r == NGX_ERROR) {
                return nxe_cedar_make_error();
            }
            if (r) {
                return nxe_cedar_make_bool(1);
            }
        }

        return nxe_cedar_make_bool(0);
    }

    /* isInRange (IP address range membership) */
    if (method->len == 9
        && ngx_memcmp(method->data, "isInRange", 9) == 0)
    {
        if (obj.type != NXE_CEDAR_RVAL_IP
            || arg.type != NXE_CEDAR_RVAL_IP)
        {
            return nxe_cedar_make_error();
        }

        return nxe_cedar_make_bool(
            nxe_cedar_ip_cidr_contains(&obj, &arg));
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
        ngx_int_t r = nxe_cedar_value_equals(left, right);
        if (r == NGX_ERROR) {
            return nxe_cedar_make_error();
        }
        return nxe_cedar_make_bool(r);
    }

    /* entity in set: check if any element matches */
    if (right->type == NXE_CEDAR_RVAL_SET) {
        if (right->v.set_elts == NULL) {
            return nxe_cedar_make_bool(0);
        }

        elts = right->v.set_elts->elts;

        for (i = 0; i < right->v.set_elts->nelts; i++) {
            ngx_int_t r = nxe_cedar_value_equals(left, &elts[i]);
            if (r == NGX_ERROR) {
                return nxe_cedar_make_error();
            }
            if (r) {
                return nxe_cedar_make_bool(1);
            }
        }

        return nxe_cedar_make_bool(0);
    }

    return nxe_cedar_make_error();
}


/* evaluate entity type check: expr is Type [in expr] */
static nxe_cedar_value_t
nxe_cedar_eval_is_check(nxe_cedar_node_t *node,
    nxe_cedar_eval_ctx_t *ctx, ngx_pool_t *pool,
    ngx_log_t *log)
{
    nxe_cedar_value_t left, right;

    left = nxe_cedar_expr_eval(node->u.is_check.object, ctx, pool, log);
    if (left.type == NXE_CEDAR_RVAL_ERROR) {
        return left;
    }
    if (left.type != NXE_CEDAR_RVAL_ENTITY) {
        return nxe_cedar_make_error();
    }

    if (!nxe_cedar_str_eq(&left.v.entity.type,
                          &node->u.is_check.entity_type))
    {
        return nxe_cedar_make_bool(0);
    }

    if (node->u.is_check.in_entity == NULL) {
        return nxe_cedar_make_bool(1);
    }

    right = nxe_cedar_expr_eval(node->u.is_check.in_entity, ctx,
                                pool, log);
    if (right.type == NXE_CEDAR_RVAL_ERROR) {
        return right;
    }

    return nxe_cedar_eval_in(&left, &right);
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

    case NXE_CEDAR_NODE_IP_LITERAL:
        return nxe_cedar_make_ip(&node->u.ip_literal.addr);

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
        return nxe_cedar_eval_attr_access(node, ctx, pool, log);

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

    case NXE_CEDAR_NODE_RECORD: {
        nxe_cedar_record_entry_t *entries;
        nxe_cedar_attr_t *attr_slot;
        ngx_array_t *attrs;

        if (node->u.record_entries == NULL) {
            return nxe_cedar_make_error();
        }

        /* avoid ngx_palloc(pool, 0) for empty record `{}` */
        attrs = ngx_array_create(pool,
                                 node->u.record_entries->nelts > 0
                                 ? node->u.record_entries->nelts : 1,
                                 sizeof(nxe_cedar_attr_t));
        if (attrs == NULL) {
            return nxe_cedar_make_error();
        }

        entries = node->u.record_entries->elts;

        for (i = 0; i < node->u.record_entries->nelts; i++) {
            left = nxe_cedar_expr_eval(entries[i].value, ctx, pool, log);
            if (left.type == NXE_CEDAR_RVAL_ERROR) {
                return nxe_cedar_make_error();
            }

            attr_slot = ngx_array_push(attrs);
            if (attr_slot == NULL) {
                return nxe_cedar_make_error();
            }

            attr_slot->name = entries[i].key;
            attr_slot->value = left;
        }

        return nxe_cedar_make_record(attrs);
    }

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
            {
                ngx_int_t r = nxe_cedar_value_equals(&left, &right);
                if (r == NGX_ERROR) {
                    return nxe_cedar_make_error();
                }
                return nxe_cedar_make_bool(r);
            }

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
            {
                ngx_int_t r = nxe_cedar_value_equals(&left, &right);
                if (r == NGX_ERROR) {
                    return nxe_cedar_make_error();
                }
                return nxe_cedar_make_bool(!r);
            }

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

        case NXE_CEDAR_OP_PLUS:
        case NXE_CEDAR_OP_MINUS:
        case NXE_CEDAR_OP_MUL: {
            int64_t result;

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
            if (nxe_cedar_long_arith(node->u.binop.op,
                                     left.v.long_val, right.v.long_val,
                                     &result) != NGX_OK)
            {
                return nxe_cedar_make_error();
            }
            return nxe_cedar_make_long(result);
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
        /* -INT64_MIN is undefined; reject it */
        if (left.v.long_val == INT64_MIN) {
            return nxe_cedar_make_error();
        }
        return nxe_cedar_make_long(-left.v.long_val);

    /* Phase 2 */
    case NXE_CEDAR_NODE_HAS:
        return nxe_cedar_eval_has(node, ctx, pool, log);

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

    case NXE_CEDAR_NODE_IS:
        return nxe_cedar_eval_is_check(node, ctx, pool, log);

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
