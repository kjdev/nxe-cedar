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
    const ngx_str_t *, const ngx_str_t *);
typedef ngx_int_t (*add_long_attr_pt)(nxe_cedar_eval_ctx_t *,
    const ngx_str_t *, int64_t);
typedef ngx_int_t (*add_bool_attr_pt)(nxe_cedar_eval_ctx_t *,
    const ngx_str_t *, ngx_flag_t);
typedef ngx_int_t (*add_ip_attr_pt)(nxe_cedar_eval_ctx_t *,
    const ngx_str_t *, const ngx_str_t *);
typedef ngx_int_t (*add_decimal_attr_pt)(nxe_cedar_eval_ctx_t *,
    const ngx_str_t *, const ngx_str_t *);
typedef nxe_cedar_record_t *(*add_record_attr_pt)(nxe_cedar_eval_ctx_t *,
    const ngx_str_t *);
typedef nxe_cedar_set_t *(*add_set_attr_pt)(nxe_cedar_eval_ctx_t *,
    const ngx_str_t *);
typedef ngx_int_t (*add_entity_attr_pt)(nxe_cedar_eval_ctx_t *,
    const ngx_str_t *, const ngx_str_t *, const ngx_str_t *);
typedef ngx_int_t (*add_parent_pt)(nxe_cedar_eval_ctx_t *,
    const ngx_str_t *, const ngx_str_t *);


/* bundle of attribute-adder function pointers per entity / context */
typedef struct {
    add_str_attr_pt      add_str;
    add_long_attr_pt     add_long;
    add_bool_attr_pt     add_bool;
    add_ip_attr_pt       add_ip;
    add_decimal_attr_pt  add_decimal;
    add_record_attr_pt   add_record;
    add_set_attr_pt      add_set;
    add_entity_attr_pt   add_entity;
} attr_api_t;


/*
 * Return the "__extn" sub-object of value if present, or NULL when
 * value is not an object or has no "__extn" key. Does not validate
 * the shape of the sub-object itself; callers pass the result to
 * parse_extn() for shape validation.
 */
static json_t *
extn_of(json_t *value)
{
    json_t *extn;

    if (!json_is_object(value)) {
        return NULL;
    }
    extn = json_object_get(value, "__extn");
    if (extn == NULL) {
        return NULL;
    }
    return extn;
}


/*
 * Parse an "__extn" object ({"fn": "ip", "arg": "..."}) into a
 * (fn, arg) pair. Returns 0 on success, -1 on malformed input.
 */
static int
parse_extn(const char *key, json_t *extn, const char **fn_out,
    json_t **arg_out)
{
    json_t *fn, *arg;

    if (!json_is_object(extn)) {
        set_error("invalid __extn format for key: %s", key);
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

    *fn_out = json_string_value(fn);
    *arg_out = arg;
    return 0;
}


static int add_record_entries(nxe_cedar_record_t *rec, json_t *obj);
static int add_set_elements(nxe_cedar_set_t *set, json_t *arr);


/*
 * Tri-state parse of an `{"__entity": {"type", "id"}}` literal:
 *   1 = valid entity, outputs populated
 *   0 = no `__entity` key (caller should fall back to record handling)
 *  -1 = `__entity` present but malformed (set_error already called)
 *
 * The previous boolean form collapsed "absent" and "malformed" into
 * the same NULL, letting invalid entity literals silently fall back to
 * record construction and drift away from the Rust oracle.
 */
static int
parse_entity_literal(json_t *value, ngx_str_t *type_out, ngx_str_t *id_out)
{
    json_t *entity, *type_val, *id_val;

    if (!json_is_object(value)) {
        return 0;
    }
    entity = json_object_get(value, "__entity");
    if (entity == NULL) {
        return 0;
    }
    if (!json_is_object(entity)) {
        set_error("__entity must be an object with type and id");
        return -1;
    }
    type_val = json_object_get(entity, "type");
    id_val = json_object_get(entity, "id");
    if (type_val == NULL || !json_is_string(type_val)
        || id_val == NULL || !json_is_string(id_val))
    {
        set_error("__entity requires string \"type\" and \"id\" fields");
        return -1;
    }

    type_out->len = json_string_length(type_val);
    type_out->data = (u_char *) json_string_value(type_val);
    id_out->len = json_string_length(id_val);
    id_out->data = (u_char *) json_string_value(id_val);
    return 1;
}


/*
 * Recursively append one JSON value to a nxe_cedar_set_t handle.
 * JSON array -> nested set; JSON object with __extn / __entity -> the
 * matching scalar; plain object -> nested record.
 */
static int
add_set_element(nxe_cedar_set_t *set, json_t *value)
{
    ngx_str_t str_val;

    if (json_is_string(value)) {
        str_val.len = json_string_length(value);
        str_val.data = (u_char *) json_string_value(value);
        if (nxe_cedar_set_add_str(set, &str_val) != NGX_OK) {
            set_error("failed to add string to set");
            return -1;
        }
        return 0;
    }

    if (json_is_integer(value)) {
        if (nxe_cedar_set_add_long(set,
                                   (int64_t) json_integer_value(value))
            != NGX_OK)
        {
            set_error("failed to add long to set");
            return -1;
        }
        return 0;
    }

    if (json_is_boolean(value)) {
        if (nxe_cedar_set_add_bool(set,
                                   json_is_true(value) ? 1 : 0) != NGX_OK)
        {
            set_error("failed to add bool to set");
            return -1;
        }
        return 0;
    }

    if (json_is_array(value)) {
        nxe_cedar_set_t *child = nxe_cedar_set_add_set(set);
        if (child == NULL) {
            set_error("failed to create nested set");
            return -1;
        }
        return add_set_elements(child, value);
    }

    if (json_is_object(value)) {
        json_t *extn = extn_of(value);
        if (extn != NULL) {
            const char *fn;
            json_t *arg;
            if (parse_extn("(set element)", extn, &fn, &arg) != 0) {
                return -1;
            }
            if (strcmp(fn, "ip") == 0) {
                str_val.len = json_string_length(arg);
                str_val.data = (u_char *) json_string_value(arg);
                if (nxe_cedar_set_add_ip(set, &str_val) != NGX_OK) {
                    set_error("failed to add IP to set");
                    return -1;
                }
                return 0;
            }
            if (strcmp(fn, "decimal") == 0) {
                str_val.len = json_string_length(arg);
                str_val.data = (u_char *) json_string_value(arg);
                if (nxe_cedar_set_add_decimal(set, &str_val) != NGX_OK) {
                    set_error("failed to add decimal to set");
                    return -1;
                }
                return 0;
            }
            set_error("unsupported extension function in set: %s", fn);
            return -1;
        }

        ngx_str_t type, id;
        int entity_rc = parse_entity_literal(value, &type, &id);
        if (entity_rc < 0) {
            return -1;
        }
        if (entity_rc > 0) {
            if (nxe_cedar_set_add_entity(set, &type, &id) != NGX_OK) {
                set_error("failed to add entity to set");
                return -1;
            }
            return 0;
        }

        nxe_cedar_record_t *child = nxe_cedar_set_add_record(set);
        if (child == NULL) {
            set_error("failed to create record in set");
            return -1;
        }
        return add_record_entries(child, value);
    }

    set_error("unsupported value type in set element");
    return -1;
}


static int
add_set_elements(nxe_cedar_set_t *set, json_t *arr)
{
    size_t i, n;

    if (set == NULL) {
        set_error("set handle is NULL (depth limit reached?)");
        return -1;
    }
    if (!json_is_array(arr)) {
        set_error("set elements must be a JSON array");
        return -1;
    }

    n = json_array_size(arr);
    for (i = 0; i < n; i++) {
        if (add_set_element(set, json_array_get(arr, i)) != 0) {
            return -1;
        }
    }
    return 0;
}


/*
 * Populate a record handle from a JSON object, recursing into nested
 * non-__extn objects to produce nested records. NULL record handle
 * indicates the C API refused creation (e.g. depth limit exceeded);
 * callers should treat that as a test-setup error.
 */
static int
add_record_entries(nxe_cedar_record_t *rec, json_t *obj)
{
    const char *key;
    json_t *value;
    ngx_str_t name, str_val;

    if (rec == NULL) {
        set_error("record handle is NULL (depth limit reached?)");
        return -1;
    }

    if (!json_is_object(obj)) {
        set_error("record entries must be a JSON object");
        return -1;
    }

    json_object_foreach(obj, key, value) {
        name.len = strlen(key);
        name.data = (u_char *) key;

        if (json_is_string(value)) {
            str_val.len = json_string_length(value);
            str_val.data = (u_char *) json_string_value(value);
            if (nxe_cedar_record_add_str(rec, &name, &str_val) != NGX_OK) {
                set_error("failed to add record attribute: %s", key);
                return -1;
            }

        } else if (json_is_integer(value)) {
            if (nxe_cedar_record_add_long(rec, &name,
                                          (int64_t) json_integer_value(
                                              value)) != NGX_OK)
            {
                set_error("failed to add record attribute: %s", key);
                return -1;
            }

        } else if (json_is_boolean(value)) {
            if (nxe_cedar_record_add_bool(rec, &name,
                                          json_is_true(value) ? 1 : 0) !=
                NGX_OK)
            {
                set_error("failed to add record attribute: %s", key);
                return -1;
            }

        } else if (json_is_array(value)) {
            nxe_cedar_set_t *child;

            child = nxe_cedar_record_add_set(rec, &name);
            if (child == NULL) {
                set_error("failed to create set in record: %s", key);
                return -1;
            }
            if (add_set_elements(child, value) != 0) {
                return -1;
            }

        } else if (json_is_object(value)) {
            json_t *extn = extn_of(value);
            ngx_str_t type, id;
            int entity_rc;

            if (extn != NULL) {
                const char *fn;
                json_t *arg;

                if (parse_extn(key, extn, &fn, &arg) != 0) {
                    return -1;
                }
                if (strcmp(fn, "ip") == 0) {
                    str_val.len = json_string_length(arg);
                    str_val.data = (u_char *) json_string_value(arg);
                    if (nxe_cedar_record_add_ip(rec, &name, &str_val)
                        != NGX_OK)
                    {
                        set_error("failed to add record IP attribute: %s",
                                  key);
                        return -1;
                    }
                } else if (strcmp(fn, "decimal") == 0) {
                    str_val.len = json_string_length(arg);
                    str_val.data = (u_char *) json_string_value(arg);
                    if (nxe_cedar_record_add_decimal(rec, &name, &str_val)
                        != NGX_OK)
                    {
                        set_error("failed to add record decimal "
                                  "attribute: %s", key);
                        return -1;
                    }
                } else {
                    set_error("unsupported extension function: %s", fn);
                    return -1;
                }

            } else if ((entity_rc = parse_entity_literal(value, &type, &id))
                       < 0)
            {
                return -1;

            } else if (entity_rc > 0) {
                if (nxe_cedar_record_add_entity(rec, &name, &type, &id)
                    != NGX_OK)
                {
                    set_error("failed to add entity to record: %s", key);
                    return -1;
                }

            } else {
                nxe_cedar_record_t *child;

                child = nxe_cedar_record_add_record(rec, &name);
                if (child == NULL) {
                    set_error("failed to create nested record: %s", key);
                    return -1;
                }
                if (add_record_entries(child, value) != 0) {
                    return -1;
                }
            }

        } else {
            set_error("unsupported record attribute type for key: %s",
                      key);
            return -1;
        }
    }

    return 0;
}


/*
 * Add attributes from a JSON object via the public eval_ctx API.
 * Value types are auto-detected from JSON native types:
 *   - string  -> add_str
 *   - integer -> add_long
 *   - boolean -> add_bool
 *   - array   -> set (recursed via add_set + add_set_elements)
 *   - object with "__extn"   -> extension type (ip -> add_ip)
 *   - object with "__entity" -> entity-valued attribute
 *   - plain object           -> record (recursed via add_record +
 *                                       add_record_entries)
 */
static int
add_attrs_via_api(nxe_cedar_eval_ctx_t *ctx, json_t *obj,
    const attr_api_t *api)
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

            if (api->add_str(ctx, &name, &str_val) != NGX_OK) {
                set_error("failed to add attribute: %s", key);
                return -1;
            }

        } else if (json_is_integer(value)) {
            if (api->add_long(ctx, &name,
                              (int64_t) json_integer_value(value))
                != NGX_OK)
            {
                set_error("failed to add attribute: %s", key);
                return -1;
            }

        } else if (json_is_boolean(value)) {
            if (api->add_bool(ctx, &name,
                              json_is_true(value) ? 1 : 0) != NGX_OK)
            {
                set_error("failed to add attribute: %s", key);
                return -1;
            }

        } else if (json_is_array(value)) {
            nxe_cedar_set_t *set;

            set = api->add_set(ctx, &name);
            if (set == NULL) {
                set_error("failed to create set attribute: %s", key);
                return -1;
            }
            if (add_set_elements(set, value) != 0) {
                return -1;
            }

        } else if (json_is_object(value)) {
            json_t *extn = extn_of(value);
            ngx_str_t type, id;
            int entity_rc;

            if (extn != NULL) {
                const char *fn;
                json_t *arg;

                if (parse_extn(key, extn, &fn, &arg) != 0) {
                    return -1;
                }
                if (strcmp(fn, "ip") == 0) {
                    str_val.len = json_string_length(arg);
                    str_val.data = (u_char *) json_string_value(arg);
                    if (api->add_ip(ctx, &name, &str_val) != NGX_OK) {
                        set_error("failed to add IP attribute: %s", key);
                        return -1;
                    }
                } else if (strcmp(fn, "decimal") == 0) {
                    str_val.len = json_string_length(arg);
                    str_val.data = (u_char *) json_string_value(arg);
                    if (api->add_decimal(ctx, &name, &str_val) != NGX_OK) {
                        set_error("failed to add decimal attribute: %s",
                                  key);
                        return -1;
                    }
                } else {
                    set_error("unsupported extension function: %s", fn);
                    return -1;
                }

            } else if ((entity_rc = parse_entity_literal(value, &type, &id))
                       < 0)
            {
                return -1;

            } else if (entity_rc > 0) {
                if (api->add_entity(ctx, &name, &type, &id) != NGX_OK) {
                    set_error("failed to add entity attribute: %s", key);
                    return -1;
                }

            } else {
                nxe_cedar_record_t *rec;

                rec = api->add_record(ctx, &name);
                if (rec == NULL) {
                    set_error("failed to create record attribute: %s",
                              key);
                    return -1;
                }
                if (add_record_entries(rec, value) != 0) {
                    return -1;
                }
            }

        } else {
            set_error("unsupported attribute type for key: %s", key);
            return -1;
        }
    }

    return 0;
}


/* attribute adder bundles for the four contexts */
static const attr_api_t principal_api = {
    nxe_cedar_eval_ctx_add_principal_attr,
    nxe_cedar_eval_ctx_add_principal_attr_long,
    nxe_cedar_eval_ctx_add_principal_attr_bool,
    nxe_cedar_eval_ctx_add_principal_attr_ip,
    nxe_cedar_eval_ctx_add_principal_attr_decimal,
    nxe_cedar_eval_ctx_add_principal_attr_record,
    nxe_cedar_eval_ctx_add_principal_attr_set,
    nxe_cedar_eval_ctx_add_principal_attr_entity,
};

static const attr_api_t action_api = {
    nxe_cedar_eval_ctx_add_action_attr,
    nxe_cedar_eval_ctx_add_action_attr_long,
    nxe_cedar_eval_ctx_add_action_attr_bool,
    nxe_cedar_eval_ctx_add_action_attr_ip,
    nxe_cedar_eval_ctx_add_action_attr_decimal,
    nxe_cedar_eval_ctx_add_action_attr_record,
    nxe_cedar_eval_ctx_add_action_attr_set,
    nxe_cedar_eval_ctx_add_action_attr_entity,
};

static const attr_api_t resource_api = {
    nxe_cedar_eval_ctx_add_resource_attr,
    nxe_cedar_eval_ctx_add_resource_attr_long,
    nxe_cedar_eval_ctx_add_resource_attr_bool,
    nxe_cedar_eval_ctx_add_resource_attr_ip,
    nxe_cedar_eval_ctx_add_resource_attr_decimal,
    nxe_cedar_eval_ctx_add_resource_attr_record,
    nxe_cedar_eval_ctx_add_resource_attr_set,
    nxe_cedar_eval_ctx_add_resource_attr_entity,
};

static const attr_api_t context_api = {
    nxe_cedar_eval_ctx_add_context_attr,
    nxe_cedar_eval_ctx_add_context_attr_long,
    nxe_cedar_eval_ctx_add_context_attr_bool,
    nxe_cedar_eval_ctx_add_context_attr_ip,
    nxe_cedar_eval_ctx_add_context_attr_decimal,
    nxe_cedar_eval_ctx_add_context_attr_record,
    nxe_cedar_eval_ctx_add_context_attr_set,
    nxe_cedar_eval_ctx_add_context_attr_entity,
};


/*
 * Add parents from a JSON array of {"type": ..., "id": ...} entries
 * via the public eval_ctx parent registration API.
 */
static int
add_parents_via_api(nxe_cedar_eval_ctx_t *ctx, json_t *arr,
    add_parent_pt add_parent, const char *field)
{
    json_t *entry;
    ngx_str_t type, id;
    size_t i, n;

    if (arr == NULL) {
        return 0;
    }

    if (!json_is_array(arr)) {
        set_error("%s must be a JSON array", field);
        return -1;
    }

    n = json_array_size(arr);
    for (i = 0; i < n; i++) {
        entry = json_array_get(arr, i);
        if (parse_entity(entry, &type, &id) != 0) {
            set_error("invalid entity ref in %s[%zu]", field, i);
            return -1;
        }
        if (add_parent(ctx, &type, &id) != NGX_OK) {
            set_error("failed to add parent in %s[%zu]", field, i);
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
    json_t *principal_parents, *action_parents, *resource_parents;
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
        if (add_attrs_via_api(ctx, principal_attrs, &principal_api) != 0) {
            ngx_destroy_pool(pool);
            json_decref(root);
            return -1;
        }
    }

    /* action_attrs */
    action_attrs = json_object_get(root, "action_attrs");
    if (action_attrs != NULL) {
        if (add_attrs_via_api(ctx, action_attrs, &action_api) != 0) {
            ngx_destroy_pool(pool);
            json_decref(root);
            return -1;
        }
    }

    /* resource_attrs */
    resource_attrs = json_object_get(root, "resource_attrs");
    if (resource_attrs != NULL) {
        if (add_attrs_via_api(ctx, resource_attrs, &resource_api) != 0) {
            ngx_destroy_pool(pool);
            json_decref(root);
            return -1;
        }
    }

    /* context */
    context_obj = json_object_get(root, "context");
    if (context_obj != NULL) {
        if (add_attrs_via_api(ctx, context_obj, &context_api) != 0) {
            ngx_destroy_pool(pool);
            json_decref(root);
            return -1;
        }
    }

    /* principal_parents */
    principal_parents = json_object_get(root, "principal_parents");
    if (add_parents_via_api(ctx, principal_parents,
                            nxe_cedar_eval_ctx_add_principal_parent,
                            "principal_parents") != 0)
    {
        ngx_destroy_pool(pool);
        json_decref(root);
        return -1;
    }

    /* action_parents */
    action_parents = json_object_get(root, "action_parents");
    if (add_parents_via_api(ctx, action_parents,
                            nxe_cedar_eval_ctx_add_action_parent,
                            "action_parents") != 0)
    {
        ngx_destroy_pool(pool);
        json_decref(root);
        return -1;
    }

    /* resource_parents */
    resource_parents = json_object_get(root, "resource_parents");
    if (add_parents_via_api(ctx, resource_parents,
                            nxe_cedar_eval_ctx_add_resource_parent,
                            "resource_parents") != 0)
    {
        ngx_destroy_pool(pool);
        json_decref(root);
        return -1;
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
