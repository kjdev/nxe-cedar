/*
 * Copyright (c) Tatsuya Kamijo
 * Copyright (c) Bengo4.com, Inc.
 *
 * cedar_ffi.h - C bindings for Rust Cedar FFI
 *
 * C interface for calling the official Rust Cedar implementation as an oracle.
 * Test-only (not included in production builds).
 */

#ifndef CEDAR_FFI_H
#define CEDAR_FFI_H

#include <stdint.h>

/*
 * Evaluate policy text and JSON request using Rust Cedar.
 *
 * @param policy_text  NUL-terminated Cedar policy text
 * @param request_json NUL-terminated JSON string
 *                     Format:
 *                     {
 *                       "principal": {"type": "...", "id": "..."},
 *                       "action":    {"type": "...", "id": "..."},
 *                       "resource":  {"type": "...", "id": "..."},
 *                       "context":   {...},
 *                       "principal_attrs": {...},
 *                       "resource_attrs":  {...}
 *                     }
 * @return             0=Deny, 1=Allow, -1=Error
 */
int32_t cedar_ffi_authorize(const char *policy_text,
    const char *request_json);

/*
 * Validate whether policy text can be parsed.
 *
 * @param policy_text  NUL-terminated Cedar policy text
 * @return             0=success, -1=error
 */
int32_t cedar_ffi_parse_check(const char *policy_text);

/*
 * Retrieve the last error message.
 *
 * @return  NUL-terminated error message, or NULL if no error.
 *          Invalidated by the next FFI call.
 */
const char *cedar_ffi_last_error(void);

#endif /* CEDAR_FFI_H */
