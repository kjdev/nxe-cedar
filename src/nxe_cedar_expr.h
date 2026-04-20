/*
 * Copyright (c) Tatsuya Kamijo
 * Copyright (c) Bengo4.com, Inc.
 *
 * nxe_cedar_expr.h - Cedar expression evaluator
 *
 * Recursively evaluates AST nodes and returns runtime values.
 * Internal interface between eval.c and expr.c.
 */

#ifndef NXE_CEDAR_EXPR_H
#define NXE_CEDAR_EXPR_H

#include "nxe_cedar_types.h"


/* runtime value types */
#define NXE_CEDAR_RVAL_STRING   0
#define NXE_CEDAR_RVAL_LONG     1
#define NXE_CEDAR_RVAL_BOOL     2
#define NXE_CEDAR_RVAL_ENTITY   3
#define NXE_CEDAR_RVAL_SET      4
#define NXE_CEDAR_RVAL_ERROR    5
#define NXE_CEDAR_RVAL_IP       6
#define NXE_CEDAR_RVAL_RECORD   7


typedef struct {
    ngx_uint_t  type;       /* NXE_CEDAR_RVAL_* */
    union {
        ngx_str_t    str_val;
        int64_t      long_val;   /* Cedar i64 runtime value */
        ngx_flag_t   bool_val;
        struct {
            ngx_str_t  type;
            ngx_str_t  id;
        } entity;
        ngx_array_t *set_elts;     /* array of nxe_cedar_value_t */
        ngx_array_t *record_attrs; /* array of nxe_cedar_attr_t */
        struct {
            u_char      addr[16];    /* network byte order */
            ngx_uint_t  prefix_len;  /* /prefix; single=32(v4)/128(v6) */
            unsigned    is_ipv6:1;
        } ip_addr;
    } v;
} nxe_cedar_value_t;


/* string equality (shared by expr.c and eval.c) */
static inline ngx_int_t
nxe_cedar_str_eq(ngx_str_t *a, ngx_str_t *b)
{
    return (a->len == b->len
            && (a->len == 0
                || ngx_memcmp(a->data, b->data, a->len) == 0));
}


nxe_cedar_value_t nxe_cedar_expr_eval(nxe_cedar_node_t *node,
    nxe_cedar_eval_ctx_t *ctx, ngx_pool_t *pool,
    ngx_log_t *log);


#endif /* NXE_CEDAR_EXPR_H */
