/*
 * Copyright (c) Tatsuya Kamijo
 * Copyright (c) Bengo4.com, Inc.
 *
 * nxe_cedar_lexer.h - Cedar policy text tokenizer
 */

#ifndef NXE_CEDAR_LEXER_H
#define NXE_CEDAR_LEXER_H

#include "nxe_cedar_types.h"


typedef struct {
    ngx_str_t   input;
    size_t      pos;
    ngx_pool_t *pool;
    ngx_log_t  *log;
} nxe_cedar_lexer_t;

void nxe_cedar_lexer_init(nxe_cedar_lexer_t *lexer,
    ngx_pool_t *pool, ngx_log_t *log, const ngx_str_t *input);
nxe_cedar_token_t nxe_cedar_lexer_next(nxe_cedar_lexer_t *lexer);


#endif /* NXE_CEDAR_LEXER_H */
