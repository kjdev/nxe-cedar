/*
 * Copyright (c) Tatsuya Kamijo
 * Copyright (c) Bengo4.com, Inc.
 *
 * nxe_cedar_parser.c - Cedar policy text recursive descent parser
 *
 * Converts token stream to AST and builds policy set.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include "nxe_cedar_lexer.h"
#include "nxe_cedar_parser.h"


#define NXE_CEDAR_MAX_PARSE_DEPTH   64
#define NXE_CEDAR_MAX_POLICIES     256
#define NXE_CEDAR_MAX_CONDITIONS    16
#define NXE_CEDAR_MAX_SET_ELEMENTS 256
#define NXE_CEDAR_MAX_TYPE_PARTS    16
#define NXE_CEDAR_MAX_MEMBER_CHAIN  16
#define NXE_CEDAR_MAX_BINOP_CHAIN  256


/* parser context (file-local) */
typedef struct {
    nxe_cedar_lexer_t  lexer;
    nxe_cedar_token_t  current;
    ngx_pool_t        *pool;
    ngx_log_t         *log;
    ngx_uint_t         depth;
    unsigned           error:1;
} nxe_cedar_parser_ctx_t;


/* forward declarations */
static nxe_cedar_node_t *nxe_cedar_parse_expr(
    nxe_cedar_parser_ctx_t *ctx);
static nxe_cedar_node_t *nxe_cedar_parse_entity_ref_with_ident(
    nxe_cedar_parser_ctx_t *ctx, ngx_str_t first_ident);
static nxe_cedar_node_t *nxe_cedar_parse_unary_expr(
    nxe_cedar_parser_ctx_t *ctx);


static void
nxe_cedar_parser_advance(nxe_cedar_parser_ctx_t *ctx)
{
    ctx->current = nxe_cedar_lexer_next(&ctx->lexer);

    if (ctx->current.type == NXE_CEDAR_TOKEN_ERROR) {
        ctx->error = 1;
    }
}


static ngx_int_t
nxe_cedar_parser_expect(nxe_cedar_parser_ctx_t *ctx,
    nxe_cedar_token_type_t type)
{
    if (ctx->current.type != type) {
        ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                      "nxe_cedar_parse: expected token %d, got %d", type,
                      ctx->current.type);
        ctx->error = 1;
        return NGX_ERROR;
    }

    nxe_cedar_parser_advance(ctx);
    return NGX_OK;
}


static nxe_cedar_node_t *
nxe_cedar_parser_alloc_node(nxe_cedar_parser_ctx_t *ctx,
    nxe_cedar_node_type_t type)
{
    nxe_cedar_node_t *node;

    node = ngx_pcalloc(ctx->pool, sizeof(nxe_cedar_node_t));
    if (node == NULL) {
        ctx->error = 1;
        return NULL;
    }

    node->type = type;
    return node;
}


/*
 * Compile a like pattern from raw source bytes (between quotes).
 * Unescaped '*' becomes 0xFF (wildcard marker).
 * '\*' is the only escape that produces a literal '*'.
 * Other escapes that decode to '*' (e.g. \x2A, \u{2A}) become
 * wildcards, matching Cedar's official semantics.
 * Returns NGX_ERROR on invalid escape (sets ctx->error).
 */
static ngx_int_t
nxe_cedar_parser_compile_pattern(nxe_cedar_parser_ctx_t *ctx,
    ngx_str_t *raw, ngx_str_t *out)
{
    u_char *src, *dst, *end;

    dst = ngx_palloc(ctx->pool, raw->len);
    if (dst == NULL) {
        ctx->error = 1;
        return NGX_ERROR;
    }

    out->data = dst;
    src = raw->data;
    end = src + raw->len;

    while (src < end) {
        /* reject raw 0xFF: reserved as wildcard sentinel */
        if (*src == 0xFF) {
            ctx->error = 1;
            return NGX_ERROR;
        }

        if (*src == '*') {
            *dst++ = 0xFF;
            src++;
            continue;
        }

        if (*src == '\\') {
            if (src + 1 >= end) {
                ctx->error = 1;
                return NGX_ERROR;
            }

            src++;  /* skip backslash */

            /* \* is the only escape producing literal '*' */
            if (*src == '*') {
                *dst++ = '*';
                src++;
                continue;
            }

            {
                u_char *dst_before;

                dst_before = dst;

                if (nxe_cedar_decode_escape(&src, end, &dst, 1)
                    != NGX_OK)
                {
                    ctx->error = 1;
                    return NGX_ERROR;
                }

                /*
                 * If an escape like \x2A or \u{2A} produced '*',
                 * treat it as a wildcard (Cedar semantics).
                 */
                if (dst == dst_before + 1 && *dst_before == '*') {
                    *dst_before = 0xFF;
                }
            }

            continue;
        }

        *dst++ = *src++;
    }

    out->len = dst - out->data;

    /* compress consecutive wildcards: "**" → single 0xFF */
    {
        u_char *r, *w, *oend;

        r = out->data;
        w = out->data;
        oend = r + out->len;

        while (r < oend) {
            *w++ = *r++;

            if (*(r - 1) == 0xFF) {
                while (r < oend && *r == 0xFF) {
                    r++;
                }
            }
        }

        out->len = w - out->data;
    }

    return NGX_OK;
}


/*
 * parse integer from ngx_str_t
 * returns NGX_ERROR on overflow (sets *result to 0)
 */
static ngx_int_t
nxe_cedar_parse_long(ngx_str_t *s, ngx_int_t *result)
{
    ngx_int_t val, digit;
    size_t i;

    val = 0;

    for (i = 0; i < s->len; i++) {
        digit = s->data[i] - '0';

        /* overflow check: val * 10 + digit > NGX_MAX_INT_T_VALUE */
        if (val > (NGX_MAX_INT_T_VALUE - digit) / 10) {
            *result = 0;
            return NGX_ERROR;
        }

        val = val * 10 + digit;
    }

    *result = val;
    return NGX_OK;
}


/*
 * parse negative integer from ngx_str_t
 * accumulates in negative domain to handle MIN_INT correctly
 * returns NGX_ERROR on underflow (sets *result to 0)
 */
static ngx_int_t
nxe_cedar_parse_neg_long(ngx_str_t *s, ngx_int_t *result)
{
    ngx_int_t val, digit;
    ngx_int_t min_val;
    size_t i;

    min_val = -NGX_MAX_INT_T_VALUE - 1;
    val = 0;

    for (i = 0; i < s->len; i++) {
        digit = s->data[i] - '0';

        /* underflow check: val * 10 - digit < min_val */
        if (val < (min_val + digit) / 10) {
            *result = 0;
            return NGX_ERROR;
        }

        val = val * 10 - digit;
    }

    *result = val;
    return NGX_OK;
}


/*
 * parse_entity_ref_with_ident: IDENT already consumed, parse rest
 * Format: Type { :: Type } :: "id"
 */
static nxe_cedar_node_t *
nxe_cedar_parse_entity_ref_with_ident(nxe_cedar_parser_ctx_t *ctx,
    ngx_str_t first_ident)
{
    nxe_cedar_node_t *node;
    ngx_str_t type_name;
    u_char *p;
    ngx_uint_t parts;
    size_t new_len;

    type_name = first_ident;
    parts = 1;

    /* consume :: segments */
    while (ctx->current.type == NXE_CEDAR_TOKEN_COLONCOLON) {
        nxe_cedar_parser_advance(ctx);  /* skip :: */

        if (ctx->current.type == NXE_CEDAR_TOKEN_STRING) {
            if (ctx->current.has_star_escape) {
                ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                              "nxe_cedar_parse: "
                              "\\* escape is only valid in like patterns");
                ctx->error = 1;
                return NULL;
            }

            /* Type::"id" - this is the entity id */
            node = nxe_cedar_parser_alloc_node(ctx,
                                               NXE_CEDAR_NODE_ENTITY_REF);
            if (node == NULL) {
                return NULL;
            }

            node->u.entity_ref.entity_type = type_name;
            node->u.entity_ref.entity_id = ctx->current.value;
            nxe_cedar_parser_advance(ctx);  /* consume string */
            return node;
        }

        if (ctx->current.type == NXE_CEDAR_TOKEN_IDENT) {
            if (++parts > NXE_CEDAR_MAX_TYPE_PARTS) {
                ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                              "nxe_cedar_parse: too many type name segments");
                ctx->error = 1;
                return NULL;
            }

            /* Type::SubType - concatenate */
            new_len = type_name.len + 2 + ctx->current.value.len;
            if (new_len < type_name.len) {
                /* overflow */
                ctx->error = 1;
                return NULL;
            }

            p = ngx_palloc(ctx->pool, new_len);
            if (p == NULL) {
                ctx->error = 1;
                return NULL;
            }

            ngx_memcpy(p, type_name.data, type_name.len);
            p[type_name.len] = ':';
            p[type_name.len + 1] = ':';
            ngx_memcpy(p + type_name.len + 2,
                       ctx->current.value.data, ctx->current.value.len);

            type_name.data = p;
            type_name.len = new_len;
            nxe_cedar_parser_advance(ctx);  /* consume ident */
            continue;
        }

        ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                      "nxe_cedar_parse: expected string or ident after ::");
        ctx->error = 1;
        return NULL;
    }

    ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                  "nxe_cedar_parse: expected :: after type name");
    ctx->error = 1;
    return NULL;
}


/* parse set literal: [ expr, ... ] */
static nxe_cedar_node_t *
nxe_cedar_parse_set_literal(nxe_cedar_parser_ctx_t *ctx)
{
    nxe_cedar_node_t *node, *elem, **slot;
    ngx_uint_t count;

    nxe_cedar_parser_advance(ctx);  /* skip [ */

    node = nxe_cedar_parser_alloc_node(ctx, NXE_CEDAR_NODE_SET);
    if (node == NULL) {
        return NULL;
    }

    node->u.set_elts = ngx_array_create(ctx->pool, 4,
                                        sizeof(nxe_cedar_node_t *));
    if (node->u.set_elts == NULL) {
        ctx->error = 1;
        return NULL;
    }

    count = 0;

    if (ctx->current.type != NXE_CEDAR_TOKEN_RBRACKET) {
        elem = nxe_cedar_parse_expr(ctx);
        if (ctx->error) {
            return NULL;
        }

        slot = ngx_array_push(node->u.set_elts);
        if (slot == NULL) {
            ctx->error = 1;
            return NULL;
        }
        *slot = elem;
        count++;

        while (ctx->current.type == NXE_CEDAR_TOKEN_COMMA) {
            if (++count > NXE_CEDAR_MAX_SET_ELEMENTS) {
                ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                              "nxe_cedar_parse: too many set elements");
                ctx->error = 1;
                return NULL;
            }

            nxe_cedar_parser_advance(ctx);

            elem = nxe_cedar_parse_expr(ctx);
            if (ctx->error) {
                return NULL;
            }

            slot = ngx_array_push(node->u.set_elts);
            if (slot == NULL) {
                ctx->error = 1;
                return NULL;
            }
            *slot = elem;
        }
    }

    if (nxe_cedar_parser_expect(ctx, NXE_CEDAR_TOKEN_RBRACKET)
        != NGX_OK)
    {
        return NULL;
    }

    return node;
}


/* parse primary expression */
static nxe_cedar_node_t *
nxe_cedar_parse_primary(nxe_cedar_parser_ctx_t *ctx)
{
    nxe_cedar_node_t *node;
    ngx_str_t ident;

    switch (ctx->current.type) {

    case NXE_CEDAR_TOKEN_TRUE:
        node = nxe_cedar_parser_alloc_node(ctx,
                                           NXE_CEDAR_NODE_BOOL_LIT);
        if (node == NULL) {
            return NULL;
        }
        node->u.bool_val = 1;
        nxe_cedar_parser_advance(ctx);
        return node;

    case NXE_CEDAR_TOKEN_FALSE:
        node = nxe_cedar_parser_alloc_node(ctx,
                                           NXE_CEDAR_NODE_BOOL_LIT);
        if (node == NULL) {
            return NULL;
        }
        node->u.bool_val = 0;
        nxe_cedar_parser_advance(ctx);
        return node;

    case NXE_CEDAR_TOKEN_NUMBER:
        node = nxe_cedar_parser_alloc_node(ctx,
                                           NXE_CEDAR_NODE_LONG_LIT);
        if (node == NULL) {
            return NULL;
        }
        if (nxe_cedar_parse_long(&ctx->current.value,
                                 &node->u.long_val) != NGX_OK)
        {
            ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "nxe_cedar_parse: integer overflow");
            ctx->error = 1;
            return NULL;
        }
        nxe_cedar_parser_advance(ctx);
        return node;

    case NXE_CEDAR_TOKEN_STRING:
        if (ctx->current.has_star_escape) {
            ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "nxe_cedar_parse: "
                          "\\* escape is only valid in like patterns");
            ctx->error = 1;
            return NULL;
        }
        node = nxe_cedar_parser_alloc_node(ctx,
                                           NXE_CEDAR_NODE_STRING_LIT);
        if (node == NULL) {
            return NULL;
        }
        node->u.string_val = ctx->current.value;
        nxe_cedar_parser_advance(ctx);
        return node;

    case NXE_CEDAR_TOKEN_PRINCIPAL:
        node = nxe_cedar_parser_alloc_node(ctx, NXE_CEDAR_NODE_VAR);
        if (node == NULL) {
            return NULL;
        }
        node->u.var_type = NXE_CEDAR_VAR_PRINCIPAL;
        nxe_cedar_parser_advance(ctx);
        return node;

    case NXE_CEDAR_TOKEN_ACTION:
        node = nxe_cedar_parser_alloc_node(ctx, NXE_CEDAR_NODE_VAR);
        if (node == NULL) {
            return NULL;
        }
        node->u.var_type = NXE_CEDAR_VAR_ACTION;
        nxe_cedar_parser_advance(ctx);
        return node;

    case NXE_CEDAR_TOKEN_RESOURCE:
        node = nxe_cedar_parser_alloc_node(ctx, NXE_CEDAR_NODE_VAR);
        if (node == NULL) {
            return NULL;
        }
        node->u.var_type = NXE_CEDAR_VAR_RESOURCE;
        nxe_cedar_parser_advance(ctx);
        return node;

    case NXE_CEDAR_TOKEN_CONTEXT:
        node = nxe_cedar_parser_alloc_node(ctx, NXE_CEDAR_NODE_VAR);
        if (node == NULL) {
            return NULL;
        }
        node->u.var_type = NXE_CEDAR_VAR_CONTEXT;
        nxe_cedar_parser_advance(ctx);
        return node;

    case NXE_CEDAR_TOKEN_IDENT:
        ident = ctx->current.value;
        nxe_cedar_parser_advance(ctx);
        return nxe_cedar_parse_entity_ref_with_ident(ctx, ident);

    case NXE_CEDAR_TOKEN_LPAREN:
        nxe_cedar_parser_advance(ctx);
        node = nxe_cedar_parse_expr(ctx);
        if (ctx->error) {
            return NULL;
        }
        if (nxe_cedar_parser_expect(ctx, NXE_CEDAR_TOKEN_RPAREN)
            != NGX_OK)
        {
            return NULL;
        }
        return node;

    case NXE_CEDAR_TOKEN_LBRACKET:
        return nxe_cedar_parse_set_literal(ctx);

    default:
        ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                      "nxe_cedar_parse: unexpected token %d in expression",
                      ctx->current.type);
        ctx->error = 1;
        return NULL;
    }
}


/* parse member expression: primary { .ident | .ident(expr) } */
static nxe_cedar_node_t *
nxe_cedar_parse_member_expr(nxe_cedar_parser_ctx_t *ctx)
{
    nxe_cedar_node_t *node, *access;
    ngx_str_t ident;
    ngx_uint_t chain;

    node = nxe_cedar_parse_primary(ctx);
    if (ctx->error) {
        return NULL;
    }

    chain = 0;

    while (ctx->current.type == NXE_CEDAR_TOKEN_DOT) {
        if (++chain > NXE_CEDAR_MAX_MEMBER_CHAIN) {
            ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "nxe_cedar_parse: too many member access levels");
            ctx->error = 1;
            return NULL;
        }
        nxe_cedar_parser_advance(ctx);

        if (ctx->current.type != NXE_CEDAR_TOKEN_IDENT) {
            ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "nxe_cedar_parse: expected identifier after '.'");
            ctx->error = 1;
            return NULL;
        }

        ident = ctx->current.value;
        nxe_cedar_parser_advance(ctx);

        /* method call: expr.method(arg) */
        if (ctx->current.type == NXE_CEDAR_TOKEN_LPAREN) {
            nxe_cedar_node_t *call;

            nxe_cedar_parser_advance(ctx);

            call = nxe_cedar_parser_alloc_node(ctx,
                                               NXE_CEDAR_NODE_METHOD_CALL);
            if (call == NULL) {
                return NULL;
            }

            call->u.method_call.object = node;
            call->u.method_call.method = ident;
            call->u.method_call.arg = nxe_cedar_parse_expr(ctx);
            if (ctx->error) {
                return NULL;
            }

            if (nxe_cedar_parser_expect(ctx, NXE_CEDAR_TOKEN_RPAREN)
                != NGX_OK)
            {
                return NULL;
            }

            node = call;
            continue;
        }

        /* attribute access: expr.ident */
        access = nxe_cedar_parser_alloc_node(ctx,
                                             NXE_CEDAR_NODE_ATTR_ACCESS);
        if (access == NULL) {
            return NULL;
        }

        access->u.attr_access.object = node;
        access->u.attr_access.attr = ident;

        node = access;
    }

    return node;
}


/* parse relation expression: unary { (== | != | in | has | like) ... } */
static nxe_cedar_node_t *
nxe_cedar_parse_relation_expr(nxe_cedar_parser_ctx_t *ctx)
{
    nxe_cedar_node_t *left, *right, *binop, *has_node;
    ngx_uint_t op;

    left = nxe_cedar_parse_unary_expr(ctx);
    if (ctx->error) {
        return NULL;
    }

    /* has operator: expr has (IDENT | STRING) */
    if (ctx->current.type == NXE_CEDAR_TOKEN_HAS) {
        nxe_cedar_parser_advance(ctx);

        if (ctx->current.type != NXE_CEDAR_TOKEN_IDENT
            && ctx->current.type != NXE_CEDAR_TOKEN_STRING)
        {
            ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "nxe_cedar_parse: "
                          "expected identifier or string after 'has'");
            ctx->error = 1;
            return NULL;
        }

        if (ctx->current.type == NXE_CEDAR_TOKEN_STRING
            && ctx->current.has_star_escape)
        {
            ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "nxe_cedar_parse: "
                          "\\* escape is only valid in like patterns");
            ctx->error = 1;
            return NULL;
        }

        has_node = nxe_cedar_parser_alloc_node(ctx, NXE_CEDAR_NODE_HAS);
        if (has_node == NULL) {
            return NULL;
        }

        has_node->u.has.object = left;
        has_node->u.has.attr = ctx->current.value;
        nxe_cedar_parser_advance(ctx);

        return has_node;
    }

    /* like operator: expr like STRING */
    if (ctx->current.type == NXE_CEDAR_TOKEN_LIKE) {
        nxe_cedar_node_t *like_node;

        nxe_cedar_parser_advance(ctx);

        if (ctx->current.type != NXE_CEDAR_TOKEN_STRING) {
            ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "nxe_cedar_parse: "
                          "expected string pattern after 'like'");
            ctx->error = 1;
            return NULL;
        }

        like_node = nxe_cedar_parser_alloc_node(ctx,
                                                NXE_CEDAR_NODE_LIKE);
        if (like_node == NULL) {
            return NULL;
        }

        like_node->u.like.object = left;

        if (nxe_cedar_parser_compile_pattern(ctx,
                                             &ctx->current.raw,
                                             &like_node->u.like.pattern)
            != NGX_OK)
        {
            ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "nxe_cedar_parse: "
                          "invalid like pattern");
            return NULL;
        }

        nxe_cedar_parser_advance(ctx);

        return like_node;
    }

    if (ctx->current.type == NXE_CEDAR_TOKEN_EQ) {
        op = NXE_CEDAR_OP_EQ;
    } else if (ctx->current.type == NXE_CEDAR_TOKEN_NE) {
        op = NXE_CEDAR_OP_NE;
    } else if (ctx->current.type == NXE_CEDAR_TOKEN_IN) {
        op = NXE_CEDAR_OP_IN;
    } else if (ctx->current.type == NXE_CEDAR_TOKEN_LT) {
        op = NXE_CEDAR_OP_LT;
    } else if (ctx->current.type == NXE_CEDAR_TOKEN_GT) {
        op = NXE_CEDAR_OP_GT;
    } else if (ctx->current.type == NXE_CEDAR_TOKEN_LE) {
        op = NXE_CEDAR_OP_LE;
    } else if (ctx->current.type == NXE_CEDAR_TOKEN_GE) {
        op = NXE_CEDAR_OP_GE;
    } else {
        return left;
    }

    nxe_cedar_parser_advance(ctx);

    right = nxe_cedar_parse_unary_expr(ctx);
    if (ctx->error) {
        return NULL;
    }

    binop = nxe_cedar_parser_alloc_node(ctx, NXE_CEDAR_NODE_BINOP);
    if (binop == NULL) {
        return NULL;
    }

    binop->u.binop.op = op;
    binop->u.binop.left = left;
    binop->u.binop.right = right;

    return binop;
}


/* parse unary expression: [! | -] unary | relation */
static nxe_cedar_node_t *
nxe_cedar_parse_unary_expr(nxe_cedar_parser_ctx_t *ctx)
{
    nxe_cedar_node_t *node, *operand;

    if (ctx->current.type == NXE_CEDAR_TOKEN_NEGATE) {
        nxe_cedar_parser_advance(ctx);

        /* fold -literal into single negative LONG_LIT */
        if (ctx->current.type == NXE_CEDAR_TOKEN_NUMBER) {
            node = nxe_cedar_parser_alloc_node(ctx,
                                               NXE_CEDAR_NODE_LONG_LIT);
            if (node == NULL) {
                return NULL;
            }

            if (nxe_cedar_parse_neg_long(&ctx->current.value,
                                         &node->u.long_val) != NGX_OK)
            {
                ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                              "nxe_cedar_parse: integer overflow");
                ctx->error = 1;
                return NULL;
            }

            nxe_cedar_parser_advance(ctx);
            return node;
        }

        /* non-literal operand: wrap in NEGATE node */
        ctx->depth++;

        if (ctx->depth > NXE_CEDAR_MAX_PARSE_DEPTH) {
            ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "nxe_cedar_parse: expression too deeply nested");
            ctx->error = 1;
            ctx->depth--;
            return NULL;
        }

        operand = nxe_cedar_parse_unary_expr(ctx);
        ctx->depth--;

        if (ctx->error) {
            return NULL;
        }

        node = nxe_cedar_parser_alloc_node(ctx, NXE_CEDAR_NODE_NEGATE);
        if (node == NULL) {
            return NULL;
        }

        node->u.unop.operand = operand;
        return node;
    }

    if (ctx->current.type == NXE_CEDAR_TOKEN_NOT) {
        nxe_cedar_parser_advance(ctx);

        ctx->depth++;

        if (ctx->depth > NXE_CEDAR_MAX_PARSE_DEPTH) {
            ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "nxe_cedar_parse: expression too deeply nested");
            ctx->error = 1;
            ctx->depth--;
            return NULL;
        }

        operand = nxe_cedar_parse_unary_expr(ctx);
        ctx->depth--;

        if (ctx->error) {
            return NULL;
        }

        node = nxe_cedar_parser_alloc_node(ctx, NXE_CEDAR_NODE_UNOP);
        if (node == NULL) {
            return NULL;
        }

        node->u.unop.operand = operand;
        return node;
    }

    return nxe_cedar_parse_member_expr(ctx);
}


/* parse and expression: relation { && relation } */
static nxe_cedar_node_t *
nxe_cedar_parse_and_expr(nxe_cedar_parser_ctx_t *ctx)
{
    nxe_cedar_node_t *left, *right, *binop;
    ngx_uint_t chain;

    left = nxe_cedar_parse_relation_expr(ctx);
    if (ctx->error) {
        return NULL;
    }

    chain = 0;

    while (ctx->current.type == NXE_CEDAR_TOKEN_AND) {
        if (++chain > NXE_CEDAR_MAX_BINOP_CHAIN) {
            ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "nxe_cedar_parse: too many chained && operators");
            ctx->error = 1;
            return NULL;
        }

        nxe_cedar_parser_advance(ctx);

        right = nxe_cedar_parse_relation_expr(ctx);
        if (ctx->error) {
            return NULL;
        }

        binop = nxe_cedar_parser_alloc_node(ctx,
                                            NXE_CEDAR_NODE_BINOP);
        if (binop == NULL) {
            return NULL;
        }

        binop->u.binop.op = NXE_CEDAR_OP_AND;
        binop->u.binop.left = left;
        binop->u.binop.right = right;
        left = binop;
    }

    return left;
}


/* parse or expression: and { || and } */
static nxe_cedar_node_t *
nxe_cedar_parse_or_expr(nxe_cedar_parser_ctx_t *ctx)
{
    nxe_cedar_node_t *left, *right, *binop;
    ngx_uint_t chain;

    left = nxe_cedar_parse_and_expr(ctx);
    if (ctx->error) {
        return NULL;
    }

    chain = 0;

    while (ctx->current.type == NXE_CEDAR_TOKEN_OR) {
        if (++chain > NXE_CEDAR_MAX_BINOP_CHAIN) {
            ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "nxe_cedar_parse: too many chained || operators");
            ctx->error = 1;
            return NULL;
        }

        nxe_cedar_parser_advance(ctx);

        right = nxe_cedar_parse_and_expr(ctx);
        if (ctx->error) {
            return NULL;
        }

        binop = nxe_cedar_parser_alloc_node(ctx,
                                            NXE_CEDAR_NODE_BINOP);
        if (binop == NULL) {
            return NULL;
        }

        binop->u.binop.op = NXE_CEDAR_OP_OR;
        binop->u.binop.left = left;
        binop->u.binop.right = right;
        left = binop;
    }

    return left;
}


/* parse expression (top-level) */
static nxe_cedar_node_t *
nxe_cedar_parse_expr(nxe_cedar_parser_ctx_t *ctx)
{
    nxe_cedar_node_t *node;

    ctx->depth++;

    if (ctx->depth > NXE_CEDAR_MAX_PARSE_DEPTH) {
        ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                      "nxe_cedar_parse: expression too deeply nested");
        ctx->error = 1;
        ctx->depth--;
        return NULL;
    }

    node = nxe_cedar_parse_or_expr(ctx);
    ctx->depth--;

    return node;
}


/* validate that all elements in a scope set are entity refs */
static ngx_int_t
nxe_cedar_parser_validate_scope_set(nxe_cedar_parser_ctx_t *ctx,
    nxe_cedar_node_t *node)
{
    nxe_cedar_node_t **elts;
    ngx_uint_t i;

    if (node->type != NXE_CEDAR_NODE_SET) {
        return NGX_OK;
    }

    if (node->u.set_elts == NULL) {
        return NGX_OK;
    }

    elts = node->u.set_elts->elts;

    for (i = 0; i < node->u.set_elts->nelts; i++) {
        if (elts[i]->type != NXE_CEDAR_NODE_ENTITY_REF) {
            ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "nxe_cedar_parse: scope set must contain"
                          " only entity references");
            ctx->error = 1;
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}


/* parse entity_ref or set literal for scope targets */
static nxe_cedar_node_t *
nxe_cedar_parse_entity_or_set(nxe_cedar_parser_ctx_t *ctx)
{
    ngx_str_t ident;

    if (ctx->current.type == NXE_CEDAR_TOKEN_LBRACKET) {
        return nxe_cedar_parse_set_literal(ctx);
    }

    if (ctx->current.type == NXE_CEDAR_TOKEN_IDENT) {
        ident = ctx->current.value;
        nxe_cedar_parser_advance(ctx);
        return nxe_cedar_parse_entity_ref_with_ident(ctx, ident);
    }

    ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                  "nxe_cedar_parse: expected entity ref or set in scope");
    ctx->error = 1;
    return NULL;
}


/* parse entity_ref only (no set literal) */
static nxe_cedar_node_t *
nxe_cedar_parse_entity_ref_target(nxe_cedar_parser_ctx_t *ctx)
{
    ngx_str_t ident;

    if (ctx->current.type == NXE_CEDAR_TOKEN_IDENT) {
        ident = ctx->current.value;
        nxe_cedar_parser_advance(ctx);
        return nxe_cedar_parse_entity_ref_with_ident(ctx, ident);
    }

    ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                  "nxe_cedar_parse: expected entity ref in scope");
    ctx->error = 1;
    return NULL;
}


/*
 * parse scope: keyword [ (== | in) target ]
 *
 * Cedar spec:
 *   == always takes entity_ref.
 *   in takes entity_ref (all scopes) or set_literal (action only).
 */
static ngx_int_t
nxe_cedar_parse_scope(nxe_cedar_parser_ctx_t *ctx,
    nxe_cedar_token_type_t var_token, nxe_cedar_scope_t *scope)
{
    if (nxe_cedar_parser_expect(ctx, var_token) != NGX_OK) {
        return NGX_ERROR;
    }

    if (ctx->current.type == NXE_CEDAR_TOKEN_EQ) {
        scope->constraint = NXE_CEDAR_SCOPE_EQ;
        nxe_cedar_parser_advance(ctx);

        /* == always takes entity_ref only */
        scope->target = nxe_cedar_parse_entity_ref_target(ctx);
        if (ctx->error) {
            return NGX_ERROR;
        }

    } else if (ctx->current.type == NXE_CEDAR_TOKEN_IN) {
        scope->constraint = NXE_CEDAR_SCOPE_IN;
        nxe_cedar_parser_advance(ctx);

        if (var_token == NXE_CEDAR_TOKEN_ACTION) {
            /* action: entity_ref or set_literal */
            scope->target = nxe_cedar_parse_entity_or_set(ctx);

            if (ctx->error) {
                return NGX_ERROR;
            }

            if (nxe_cedar_parser_validate_scope_set(ctx,
                                                    scope->target)
                != NGX_OK)
            {
                return NGX_ERROR;
            }
        } else {
            /* principal/resource: entity_ref only */
            scope->target = nxe_cedar_parse_entity_ref_target(ctx);

            if (ctx->error) {
                return NGX_ERROR;
            }
        }

    } else {
        scope->constraint = NXE_CEDAR_SCOPE_NONE;
        scope->target = NULL;
    }

    return NGX_OK;
}


/* parse condition: (when | unless) { expr } */
static ngx_int_t
nxe_cedar_parse_condition(nxe_cedar_parser_ctx_t *ctx,
    nxe_cedar_condition_t *cond)
{
    if (ctx->current.type == NXE_CEDAR_TOKEN_UNLESS) {
        cond->is_unless = 1;
    } else {
        cond->is_unless = 0;
    }

    nxe_cedar_parser_advance(ctx);

    if (nxe_cedar_parser_expect(ctx, NXE_CEDAR_TOKEN_LBRACE)
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    cond->expr = nxe_cedar_parse_expr(ctx);
    if (ctx->error) {
        return NGX_ERROR;
    }

    if (nxe_cedar_parser_expect(ctx, NXE_CEDAR_TOKEN_RBRACE)
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    return NGX_OK;
}


/* parse a single policy */
static ngx_int_t
nxe_cedar_parse_policy(nxe_cedar_parser_ctx_t *ctx,
    nxe_cedar_policy_t *policy)
{
    nxe_cedar_condition_t *cond;
    ngx_uint_t nconds;

    /* effect */
    if (ctx->current.type == NXE_CEDAR_TOKEN_FORBID) {
        policy->is_forbid = 1;
    } else {
        policy->is_forbid = 0;
    }

    nxe_cedar_parser_advance(ctx);

    /* ( */
    if (nxe_cedar_parser_expect(ctx, NXE_CEDAR_TOKEN_LPAREN)
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    /* principal */
    if (nxe_cedar_parse_scope(ctx, NXE_CEDAR_TOKEN_PRINCIPAL,
                              &policy->principal) != NGX_OK)
    {
        return NGX_ERROR;
    }

    if (nxe_cedar_parser_expect(ctx, NXE_CEDAR_TOKEN_COMMA) != NGX_OK) {
        return NGX_ERROR;
    }

    /* action */
    if (nxe_cedar_parse_scope(ctx, NXE_CEDAR_TOKEN_ACTION,
                              &policy->action) != NGX_OK)
    {
        return NGX_ERROR;
    }

    if (nxe_cedar_parser_expect(ctx, NXE_CEDAR_TOKEN_COMMA) != NGX_OK) {
        return NGX_ERROR;
    }

    /* resource */
    if (nxe_cedar_parse_scope(ctx, NXE_CEDAR_TOKEN_RESOURCE,
                              &policy->resource) != NGX_OK)
    {
        return NGX_ERROR;
    }

    /* ) */
    if (nxe_cedar_parser_expect(ctx, NXE_CEDAR_TOKEN_RPAREN)
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    /* conditions */
    policy->conditions = ngx_array_create(ctx->pool, 2,
                                          sizeof(nxe_cedar_condition_t));
    if (policy->conditions == NULL) {
        ctx->error = 1;
        return NGX_ERROR;
    }

    nconds = 0;

    while (ctx->current.type == NXE_CEDAR_TOKEN_WHEN
           || ctx->current.type == NXE_CEDAR_TOKEN_UNLESS)
    {
        if (++nconds > NXE_CEDAR_MAX_CONDITIONS) {
            ngx_log_error(NGX_LOG_ERR, ctx->log, 0,
                          "nxe_cedar_parse: too many conditions per policy");
            ctx->error = 1;
            return NGX_ERROR;
        }

        cond = ngx_array_push(policy->conditions);
        if (cond == NULL) {
            ctx->error = 1;
            return NGX_ERROR;
        }

        if (nxe_cedar_parse_condition(ctx, cond) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    /* ; */
    if (nxe_cedar_parser_expect(ctx, NXE_CEDAR_TOKEN_SEMICOLON)
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    return NGX_OK;
}


nxe_cedar_policy_set_t *
nxe_cedar_parse(ngx_pool_t *pool, ngx_log_t *log, const ngx_str_t *text)
{
    nxe_cedar_parser_ctx_t ctx;
    nxe_cedar_policy_set_t *ps;
    nxe_cedar_policy_t *policy;

    if (pool == NULL || log == NULL || text == NULL) {
        return NULL;
    }

    ngx_memzero(&ctx, sizeof(nxe_cedar_parser_ctx_t));
    ctx.pool = pool;
    ctx.log = log;

    nxe_cedar_lexer_init(&ctx.lexer, pool, log, text);
    nxe_cedar_parser_advance(&ctx);

    ps = ngx_pcalloc(pool, sizeof(nxe_cedar_policy_set_t));
    if (ps == NULL) {
        return NULL;
    }

    ps->policies = ngx_array_create(pool, 4,
                                    sizeof(nxe_cedar_policy_t));
    if (ps->policies == NULL) {
        return NULL;
    }

    while (ctx.current.type == NXE_CEDAR_TOKEN_PERMIT
           || ctx.current.type == NXE_CEDAR_TOKEN_FORBID)
    {
        if (ps->policies->nelts >= NXE_CEDAR_MAX_POLICIES) {
            ngx_log_error(NGX_LOG_ERR, log, 0,
                          "nxe_cedar_parse: too many policies (max %d)",
                          NXE_CEDAR_MAX_POLICIES);
            return NULL;
        }

        policy = ngx_array_push(ps->policies);
        if (policy == NULL) {
            return NULL;
        }

        ngx_memzero(policy, sizeof(nxe_cedar_policy_t));

        if (nxe_cedar_parse_policy(&ctx, policy) != NGX_OK) {
            return NULL;
        }
    }

    if (ctx.current.type != NXE_CEDAR_TOKEN_EOF) {
        ngx_log_error(NGX_LOG_ERR, log, 0,
                      "nxe_cedar_parse: unexpected token after policies");
        return NULL;
    }

    return ps;
}
