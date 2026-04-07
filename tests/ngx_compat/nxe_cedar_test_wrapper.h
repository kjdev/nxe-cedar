/*
 * Copyright (c) Tatsuya Kamijo
 * Copyright (c) Bengo4.com, Inc.
 *
 * nxe_cedar_test_wrapper.h - C test wrapper declarations
 *
 * Converts JSON requests to nxe-cedar C API calls and evaluates them.
 * Called from Rust test runner and C test runner.
 */

#ifndef NXE_CEDAR_TEST_WRAPPER_H
#define NXE_CEDAR_TEST_WRAPPER_H

#include <stdint.h>

/*
 * Evaluate policy text with a JSON request.
 *
 * @param policy_text  NUL-terminated Cedar policy text
 * @param request_json NUL-terminated JSON string
 * @return             0=Deny, 1=Allow, -1=Error
 */
int32_t nxe_cedar_test_evaluate(const char *policy_text,
    const char *request_json);

/*
 * Retrieve the last error message.
 *
 * @return  NUL-terminated error message, or NULL if no error.
 *          Invalidated by the next call.
 */
const char *nxe_cedar_test_last_error(void);

#endif /* NXE_CEDAR_TEST_WRAPPER_H */
