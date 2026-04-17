/*
 * Copyright (c) Tatsuya Kamijo
 * Copyright (c) Bengo4.com, Inc.
 *
 * nxe_cedar_test_wrapper.c - C test wrapper implementation
 *
 * Parses JSON requests with jansson, converts to nxe-cedar C API, and evaluates.
 */

#include "nxe_cedar_test_wrapper.h"
#include "ngx_stub.h"
#include "nxe_cedar_parser.h"
#include "nxe_cedar_eval.h"

#include <jansson.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


/* error message buffer (thread-local to avoid data races) */
static __thread char error_buf[1024];
static __thread int error_set = 0;


static void
set_error(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vsnprintf(error_buf, sizeof(error_buf), fmt, args);
    va_end(args);
    error_set = 1;
}


static void
clear_error(void)
{
    error_set = 0;
    error_buf[0] = '\0';
}


/*
 * Convert "type" and "id" from a JSON object to ngx_str_t.
 * {"type": "User", "id": "alice"} -> type_out, id_out
 */
static int
parse_entity(json_t *obj, ngx_str_t *type_out, ngx_str_t *id_out)
{
    json_t *type_val, *id_val;
    const char *type_str, *id_str;

    if (obj == NULL || !json_is_object(obj)) {
        return -1;
    }

    type_val = json_object_get(obj, "type");
    id_val = json_object_get(obj, "id");

    if (type_val == NULL || id_val == NULL
        || !json_is_string(type_val) || !json_is_string(id_val))
    {
        return -1;
    }

    type_str = json_string_value(type_val);
    id_str = json_string_value(id_val);

    type_out->len = json_string_length(type_val);
    type_out->data = (u_char *) type_str;

    id_out->len = json_string_length(id_val);
    id_out->data = (u_char *) id_str;

    return 0;
}


/* function pointer types for typed attribute adders */
typedef ngx_int_t (*add_str_attr_pt)(nxe_cedar_eval_ctx_t *,
    ngx_str_t *, ngx_str_t *);
typedef ngx_int_t (*add_long_attr_pt)(nxe_cedar_eval_ctx_t *,
    ngx_str_t *, int64_t);
typedef ngx_int_t (*add_bool_attr_pt)(nxe_cedar_eval_ctx_t *,
    ngx_str_t *, ngx_flag_t);
typedef ngx_int_t (*add_ip_attr_pt)(nxe_cedar_eval_ctx_t *,
    ngx_str_t *, ngx_str_t *);


/*
 * Add attributes from a JSON object via the public eval_ctx API.
 * Value types are auto-detected from JSON native types:
 *   - string  -> add_str
 *   - integer -> add_long
 *   - boolean -> add_bool
 *   - object with "__extn" -> extension type (ip -> add_ip)
 */
static int
add_attrs_via_api(nxe_cedar_eval_ctx_t *ctx, json_t *obj,
    add_str_attr_pt add_str, add_long_attr_pt add_long,
    add_bool_attr_pt add_bool, add_ip_attr_pt add_ip)
{
    const char *key;
    json_t *value;
    ngx_str_t name, str_val;

    if (obj == NULL) {
        return 0;
    }

    if (!json_is_object(obj)) {
        set_error("attributes must be a JSON object");
        return -1;
    }

    json_object_foreach(obj, key, value) {
        name.len = strlen(key);
        name.data = (u_char *) key;

        if (json_is_string(value)) {
            str_val.len = json_string_length(value);
            str_val.data = (u_char *) json_string_value(value);

            if (add_str(ctx, &name, &str_val) != NGX_OK) {
                set_error("failed to add attribute: %s", key);
                return -1;
            }

        } else if (json_is_integer(value)) {
            if (add_long(ctx, &name,
                         (int64_t) json_integer_value(value)) != NGX_OK)
            {
                set_error("failed to add attribute: %s", key);
                return -1;
            }

        } else if (json_is_boolean(value)) {
            if (add_bool(ctx, &name,
                         json_is_true(value) ? 1 : 0) != NGX_OK)
            {
                set_error("failed to add attribute: %s", key);
                return -1;
            }

        } else if (json_is_object(value)) {
            /* Cedar extension type: {"__extn": {"fn": "ip", "arg": "..."}} */
            json_t *extn, *fn, *arg;

            extn = json_object_get(value, "__extn");
            if (extn == NULL || !json_is_object(extn)) {
                set_error("unsupported object attribute for key: %s", key);
                return -1;
            }

            fn = json_object_get(extn, "fn");
            arg = json_object_get(extn, "arg");

            if (fn == NULL || !json_is_string(fn)
                || arg == NULL || !json_is_string(arg))
            {
                set_error("invalid __extn format for key: %s", key);
                return -1;
            }

            if (strcmp(json_string_value(fn), "ip") == 0) {
                str_val.len = json_string_length(arg);
                str_val.data = (u_char *) json_string_value(arg);

                if (add_ip(ctx, &name, &str_val) != NGX_OK) {
                    set_error("failed to add IP attribute: %s", key);
                    return -1;
                }
            } else {
                set_error("unsupported extension function: %s",
                          json_string_value(fn));
                return -1;
            }

        } else {
            set_error("unsupported attribute type for key: %s", key);
            return -1;
        }
    }

    return 0;
}


int32_t
nxe_cedar_test_evaluate(const char *policy_text, const char *request_json)
{
    json_t *root;
    json_error_t jerr;
    ngx_pool_t *pool;
    ngx_log_t log;
    ngx_str_t text;
    nxe_cedar_policy_set_t *ps;
    nxe_cedar_eval_ctx_t *ctx;
    nxe_cedar_decision_t decision;
    json_t *principal, *action, *resource;
    json_t *context_obj, *principal_attrs, *action_attrs, *resource_attrs;
    ngx_str_t type, id;

    clear_error();

    if (policy_text == NULL || request_json == NULL) {
        set_error("policy_text and request_json must not be NULL");
        return -1;
    }

    /* JSON parse */
    root = json_loads(request_json, 0, &jerr);
    if (root == NULL) {
        set_error("JSON parse error: %s (line %d)", jerr.text, jerr.line);
        return -1;
    }

    /* create nginx stub pool */
    log.log_level = NGX_LOG_ERR;
    pool = ngx_create_pool(4096, &log);
    if (pool == NULL) {
        set_error("failed to create pool");
        json_decref(root);
        return -1;
    }

    /* parse policy */
    text.len = strlen(policy_text);
    text.data = (u_char *) policy_text;

    ps = nxe_cedar_parse(pool, &log, &text);
    if (ps == NULL) {
        set_error("nxe_cedar_parse failed");
        ngx_destroy_pool(pool);
        json_decref(root);
        return -1;
    }

    /* build evaluation context */
    ctx = nxe_cedar_eval_ctx_create(pool);
    if (ctx == NULL) {
        set_error("nxe_cedar_eval_ctx_create failed");
        ngx_destroy_pool(pool);
        json_decref(root);
        return -1;
    }

    /* principal */
    principal = json_object_get(root, "principal");
    if (principal == NULL) {
        set_error("missing required field: principal");
        ngx_destroy_pool(pool);
        json_decref(root);
        return -1;
    }
    if (parse_entity(principal, &type, &id) != 0) {
        set_error("invalid principal entity");
        ngx_destroy_pool(pool);
        json_decref(root);
        return -1;
    }
    nxe_cedar_eval_ctx_set_principal(ctx, &type, &id);

    /* action */
    action = json_object_get(root, "action");
    if (action == NULL) {
        set_error("missing required field: action");
        ngx_destroy_pool(pool);
        json_decref(root);
        return -1;
    }
    if (parse_entity(action, &type, &id) != 0) {
        set_error("invalid action entity");
        ngx_destroy_pool(pool);
        json_decref(root);
        return -1;
    }
    nxe_cedar_eval_ctx_set_action(ctx, &type, &id);

    /* resource */
    resource = json_object_get(root, "resource");
    if (resource == NULL) {
        set_error("missing required field: resource");
        ngx_destroy_pool(pool);
        json_decref(root);
        return -1;
    }
    if (parse_entity(resource, &type, &id) != 0) {
        set_error("invalid resource entity");
        ngx_destroy_pool(pool);
        json_decref(root);
        return -1;
    }
    nxe_cedar_eval_ctx_set_resource(ctx, &type, &id);

    /* principal_attrs */
    principal_attrs = json_object_get(root, "principal_attrs");
    if (principal_attrs != NULL) {
        if (add_attrs_via_api(ctx, principal_attrs,
                              nxe_cedar_eval_ctx_add_principal_attr,
                              nxe_cedar_eval_ctx_add_principal_attr_long,
                              nxe_cedar_eval_ctx_add_principal_attr_bool,
                              nxe_cedar_eval_ctx_add_principal_attr_ip)
            != 0)
        {
            ngx_destroy_pool(pool);
            json_decref(root);
            return -1;
        }
    }

    /* action_attrs */
    action_attrs = json_object_get(root, "action_attrs");
    if (action_attrs != NULL) {
        if (add_attrs_via_api(ctx, action_attrs,
                              nxe_cedar_eval_ctx_add_action_attr,
                              nxe_cedar_eval_ctx_add_action_attr_long,
                              nxe_cedar_eval_ctx_add_action_attr_bool,
                              nxe_cedar_eval_ctx_add_action_attr_ip)
            != 0)
        {
            ngx_destroy_pool(pool);
            json_decref(root);
            return -1;
        }
    }

    /* resource_attrs */
    resource_attrs = json_object_get(root, "resource_attrs");
    if (resource_attrs != NULL) {
        if (add_attrs_via_api(ctx, resource_attrs,
                              nxe_cedar_eval_ctx_add_resource_attr,
                              nxe_cedar_eval_ctx_add_resource_attr_long,
                              nxe_cedar_eval_ctx_add_resource_attr_bool,
                              nxe_cedar_eval_ctx_add_resource_attr_ip)
            != 0)
        {
            ngx_destroy_pool(pool);
            json_decref(root);
            return -1;
        }
    }

    /* context */
    context_obj = json_object_get(root, "context");
    if (context_obj != NULL) {
        if (add_attrs_via_api(ctx, context_obj,
                              nxe_cedar_eval_ctx_add_context_attr,
                              nxe_cedar_eval_ctx_add_context_attr_long,
                              nxe_cedar_eval_ctx_add_context_attr_bool,
                              nxe_cedar_eval_ctx_add_context_attr_ip)
            != 0)
        {
            ngx_destroy_pool(pool);
            json_decref(root);
            return -1;
        }
    }

    /* evaluate */
    decision = nxe_cedar_eval(ps, ctx, &log);

    /* cleanup */
    ngx_destroy_pool(pool);
    json_decref(root);

    return (int32_t) decision;
}


const char *
nxe_cedar_test_last_error(void)
{
    if (error_set) {
        return error_buf;
    }
    return NULL;
}
