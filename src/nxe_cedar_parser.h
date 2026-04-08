/*
 * Copyright (c) Tatsuya Kamijo
 * Copyright (c) Bengo4.com, Inc.
 *
 * nxe_cedar_parser.h - Cedar policy text recursive descent parser
 */

#ifndef NXE_CEDAR_PARSER_H
#define NXE_CEDAR_PARSER_H

#include "nxe_cedar_types.h"


nxe_cedar_policy_set_t *nxe_cedar_parse(ngx_pool_t *pool,
    ngx_log_t *log, const ngx_str_t *text);


#endif /* NXE_CEDAR_PARSER_H */
