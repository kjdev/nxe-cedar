/*
 * Copyright (c) Tatsuya Kamijo
 * Copyright (c) Bengo4.com, Inc.
 *
 * nxe_cedar_parser.h - Cedar policy text recursive descent parser
 */

#ifndef NXE_CEDAR_PARSER_H
#define NXE_CEDAR_PARSER_H

#include "nxe_cedar_types.h"


/*
 * Parse a Cedar policy-set text into an AST.
 *
 * All three arguments are required: pool / log / text must be non-NULL.
 * Passing NULL for any of them returns NULL without dereferencing it
 * (defensive guard against caller mistakes; the implementation also
 * relies on these being non-NULL once it starts allocating).
 *
 * Returns NULL on parse error or allocation failure as well; details
 * are logged to `log`.
 */
nxe_cedar_policy_set_t *nxe_cedar_parse(ngx_pool_t *pool,
    ngx_log_t *log, const ngx_str_t *text);


#endif /* NXE_CEDAR_PARSER_H */
