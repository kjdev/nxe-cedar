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
#include "nxe_cedar_util.h"


nxe_cedar_value_t nxe_cedar_expr_eval(nxe_cedar_node_t *node,
    nxe_cedar_eval_ctx_t *ctx, ngx_pool_t *pool,
    ngx_log_t *log);


/*
 * Parse an IP literal string (v4 / v6, with optional CIDR) into a
 * runtime value. Returns an RVAL_ERROR value on invalid input.
 * Shared with eval.c so the injection API can eagerly materialize
 * IP attribute values at insertion time.
 */
nxe_cedar_value_t nxe_cedar_make_ip(ngx_str_t *s);


/*
 * Parse a Cedar decimal string ("[-]?d+\.d{1,4}") into a fixed-point
 * i64 runtime value with implicit scale 10^4. Returns an RVAL_ERROR
 * value when the input is malformed or the scaled magnitude does not
 * fit in int64_t. Shared with eval.c so the injection API can eagerly
 * materialize decimal attribute values at insertion time.
 */
nxe_cedar_value_t nxe_cedar_make_decimal(ngx_str_t *s);


#endif /* NXE_CEDAR_EXPR_H */
