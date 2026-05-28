/*
 * Copyright (c) Tatsuya Kamijo
 * Copyright (c) Bengo4.com, Inc.
 *
 * nxe_cedar_util.h - Common utility helpers shared across layers
 *
 * Header for small, dependency-free helpers used by both the parser
 * and the evaluator. Keeping them here lets the parser avoid pulling
 * in evaluator-only headers solely for tiny utilities.
 */

#ifndef NXE_CEDAR_UTIL_H
#define NXE_CEDAR_UTIL_H

#include "nxe_cedar_types.h"


/* string equality (shared across parser/expr/eval layers) */
static inline ngx_int_t
nxe_cedar_str_eq(const ngx_str_t *a, const ngx_str_t *b)
{
    return (a->len == b->len
            && (a->len == 0
                || ngx_memcmp(a->data, b->data, a->len) == 0));
}


#endif /* NXE_CEDAR_UTIL_H */
