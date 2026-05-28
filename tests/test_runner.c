/*
 * Copyright (c) Tatsuya Kamijo
 * Copyright (c) Bengo4.com, Inc.
 *
 * test_runner.c - nxe-cedar standalone C test runner
 *
 * Loads JSON test cases from tests/cases/ and
 * verifies evaluation results via nxe_cedar_test_evaluate().
 *
 * Depends on: jansson (JSON parser)
 * Build: cd tests && make test
 *
 * Environment variables:
 *   NXE_CEDAR_TEST_PHASE=N  run only tests at or below specified phase
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <jansson.h>

#include "nxe_cedar_test_wrapper.h"
#include "nxe_cedar_parser.h"
#include "nxe_cedar_eval.h"
#include "nxe_cedar_expr.h"
#include "ngx_stub.h"


typedef struct {
    int  passed;
    int  failed;
    int  skipped;
} test_stats_t;


static char *
read_file(const char *path)
{
    FILE *fp;
    long size;
    char *buf;

    fp = fopen(path, "r");
    if (fp == NULL) {
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }

    size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return NULL;
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    buf = malloc(size + 1);
    if (buf == NULL) {
        fclose(fp);
        return NULL;
    }

    if (fread(buf, 1, size, fp) != (size_t) size) {
        free(buf);
        fclose(fp);
        return NULL;
    }

    buf[size] = '\0';
    fclose(fp);

    return buf;
}


static const char *
extract_label(const char *path, char *buf, size_t buf_size)
{
    const char *start, *end;
    size_t len;

    /* cases/phase1/basic_permit.json -> phase1/basic_permit */
    start = strstr(path, "cases/");
    if (start != NULL) {
        start += 6;  /* skip "cases/" */
    } else {
        start = path;
    }

    end = strrchr(start, '.');
    if (end == NULL) {
        end = start + strlen(start);
    }

    len = (size_t) (end - start);
    if (len >= buf_size) {
        len = buf_size - 1;
    }

    memcpy(buf, start, len);
    buf[len] = '\0';

    return buf;
}


static void
run_test_file(const char *path, int max_phase, test_stats_t *stats)
{
    char *content;
    json_t *root, *tests, *test_obj;
    json_t *policy_val, *request_val, *expected_val, *name_val;
    json_error_t jerr;
    int phase;
    size_t i;
    const char *policy, *expected_str, *test_name;
    char *request_str;
    int32_t result;
    int expected;
    char label[256];

    content = read_file(path);
    if (content == NULL) {
        fprintf(stderr, "  SKIP: cannot read %s\n", path);
        stats->skipped++;
        return;
    }

    root = json_loads(content, 0, &jerr);
    free(content);

    if (root == NULL) {
        fprintf(stderr, "  SKIP: JSON parse error in %s: %s\n",
                path, jerr.text);
        stats->skipped++;
        return;
    }

    if (!json_is_integer(json_object_get(root, "phase"))) {
        fprintf(stderr, "  SKIP: missing or invalid 'phase' in %s\n",
                path);
        json_decref(root);
        stats->skipped++;
        return;
    }

    phase = (int) json_integer_value(json_object_get(root, "phase"));
    if (max_phase > 0 && phase > max_phase) {
        json_decref(root);
        return;
    }

    tests = json_object_get(root, "tests");
    if (!json_is_array(tests)) {
        fprintf(stderr, "  SKIP: no tests array in %s\n", path);
        json_decref(root);
        stats->skipped++;
        return;
    }

    extract_label(path, label, sizeof(label));

    for (i = 0; i < json_array_size(tests); i++) {
        test_obj = json_array_get(tests, i);

        name_val = json_object_get(test_obj, "name");
        test_name = json_is_string(name_val)
                    ? json_string_value(name_val) : "unknown";

        policy_val = json_object_get(test_obj, "policy");
        request_val = json_object_get(test_obj, "request");
        expected_val = json_object_get(test_obj, "expected");

        if (!json_is_string(policy_val)
            || !json_is_object(request_val)
            || !json_is_string(expected_val))
        {
            fprintf(stderr, "  SKIP: invalid test format: %s\n",
                    test_name);
            stats->skipped++;
            continue;
        }

        policy = json_string_value(policy_val);
        expected_str = json_string_value(expected_val);
        request_str = json_dumps(request_val, JSON_COMPACT);

        if (request_str == NULL) {
            fprintf(stderr, "  SKIP: json_dumps failed: %s\n",
                    test_name);
            stats->skipped++;
            continue;
        }

        if (strcmp(expected_str, "allow") == 0) {
            expected = 1;
        } else if (strcmp(expected_str, "deny") == 0) {
            expected = 0;
        } else if (strcmp(expected_str, "error") == 0) {
            expected = -1;
        } else {
            fprintf(stderr, "  SKIP: invalid expected value: %s (%s)\n",
                    expected_str, test_name);
            stats->skipped++;
            free(request_str);
            continue;
        }

        result = nxe_cedar_test_evaluate(policy, request_str);
        free(request_str);

        if (result == (int32_t) expected) {
            printf("%s :: %s ... ok\n", label, test_name);
            stats->passed++;
        } else {
            printf("%s :: %s ... FAILED\n", label, test_name);
            fprintf(stderr, "  expected %s, got %s",
                    expected_str,
                    result == 1 ? "allow"
                                : (result == 0 ? "deny" : "error"));
            if (result == -1) {
                const char *err = nxe_cedar_test_last_error();
                if (err != NULL) {
                    fprintf(stderr, " (%s)", err);
                }
            }
            fprintf(stderr, "\n");
            stats->failed++;
        }
    }

    json_decref(root);
}


static void
run_parser_null_guard_tests(test_stats_t *stats)
{
    ngx_pool_t *pool;
    ngx_log_t log;
    ngx_str_t text;
    nxe_cedar_policy_set_t *ps;
    const char *label = "unit/parse_null_guard";

    memset(&log, 0, sizeof(log));
    pool = ngx_create_pool(1024, &log);
    if (pool == NULL) {
        fprintf(stderr, "%s :: setup ... FAILED (pool create)\n", label);
        stats->failed++;
        return;
    }

    text.data = (u_char *) "permit (principal, action, resource);";
    text.len = strlen((const char *) text.data);

    ps = nxe_cedar_parse(NULL, &log, &text);
    if (ps == NULL) {
        printf("%s :: pool_null ... ok\n", label);
        stats->passed++;
    } else {
        printf("%s :: pool_null ... FAILED\n", label);
        fprintf(stderr, "  expected NULL, got non-NULL\n");
        stats->failed++;
    }

    ps = nxe_cedar_parse(pool, NULL, &text);
    if (ps == NULL) {
        printf("%s :: log_null ... ok\n", label);
        stats->passed++;
    } else {
        printf("%s :: log_null ... FAILED\n", label);
        fprintf(stderr, "  expected NULL, got non-NULL\n");
        stats->failed++;
    }

    ps = nxe_cedar_parse(pool, &log, NULL);
    if (ps == NULL) {
        printf("%s :: text_null ... ok\n", label);
        stats->passed++;
    } else {
        printf("%s :: text_null ... FAILED\n", label);
        fprintf(stderr, "  expected NULL, got non-NULL\n");
        stats->failed++;
    }

    ngx_destroy_pool(pool);
}


static void
run_make_value_null_guard_tests(test_stats_t *stats)
{
    ngx_str_t empty;
    nxe_cedar_value_t val;
    const char *label = "unit/make_value_null_guard";

    val = nxe_cedar_make_ip(NULL);
    if (val.type == NXE_CEDAR_RVAL_ERROR) {
        printf("%s :: ip_null ... ok\n", label);
        stats->passed++;
    } else {
        printf("%s :: ip_null ... FAILED\n", label);
        fprintf(stderr, "  expected RVAL_ERROR, got type=%d\n",
                (int) val.type);
        stats->failed++;
    }

    empty.data = NULL;
    empty.len = 0;
    val = nxe_cedar_make_ip(&empty);
    if (val.type == NXE_CEDAR_RVAL_ERROR) {
        printf("%s :: ip_empty ... ok\n", label);
        stats->passed++;
    } else {
        printf("%s :: ip_empty ... FAILED\n", label);
        fprintf(stderr, "  expected RVAL_ERROR, got type=%d\n",
                (int) val.type);
        stats->failed++;
    }

    val = nxe_cedar_make_decimal(NULL);
    if (val.type == NXE_CEDAR_RVAL_ERROR) {
        printf("%s :: decimal_null ... ok\n", label);
        stats->passed++;
    } else {
        printf("%s :: decimal_null ... FAILED\n", label);
        fprintf(stderr, "  expected RVAL_ERROR, got type=%d\n",
                (int) val.type);
        stats->failed++;
    }
}


/* helper for unit tests: build a static ngx_str_t from a C string */
static void
unit_set_str(ngx_str_t *s, const char *cstr)
{
    s->data = (u_char *) cstr;
    s->len = strlen(cstr);
}


static void
run_injection_duplicate_key_tests(test_stats_t *stats)
{
    ngx_pool_t *pool;
    ngx_log_t log;
    nxe_cedar_eval_ctx_t *ctx;
    nxe_cedar_record_t *rec, *child;
    ngx_str_t name_a, name_b, val_a, val_b;
    ngx_str_t ent_type, ent_id;
    ngx_int_t rc;
    const char *label = "unit/dup_key_rejected";

    memset(&log, 0, sizeof(log));
    pool = ngx_create_pool(4096, &log);
    if (pool == NULL) {
        fprintf(stderr, "%s :: setup ... FAILED (pool create)\n", label);
        stats->failed++;
        return;
    }

    ctx = nxe_cedar_eval_ctx_create(pool);
    if (ctx == NULL) {
        fprintf(stderr, "%s :: setup ... FAILED (ctx create)\n", label);
        stats->failed++;
        ngx_destroy_pool(pool);
        return;
    }

    unit_set_str(&name_a, "role");
    unit_set_str(&val_a, "admin");
    unit_set_str(&val_b, "guest");

    /* first insertion succeeds */
    rc = nxe_cedar_eval_ctx_add_principal_attr(ctx, &name_a, &val_a);
    if (rc != NGX_OK) {
        printf("%s :: principal_str_first ... FAILED\n", label);
        stats->failed++;
    } else {
        printf("%s :: principal_str_first ... ok\n", label);
        stats->passed++;
    }

    /* duplicate (string -> string) must be rejected */
    rc = nxe_cedar_eval_ctx_add_principal_attr(ctx, &name_a, &val_b);
    if (rc == NGX_ERROR) {
        printf("%s :: principal_str_dup ... ok\n", label);
        stats->passed++;
    } else {
        printf("%s :: principal_str_dup ... FAILED\n", label);
        stats->failed++;
    }

    /* duplicate across kinds (string -> long on same entity) must also
     * be rejected: attribute names are unique per entity regardless of
     * value kind */
    rc = nxe_cedar_eval_ctx_add_principal_attr_long(ctx, &name_a, 42);
    if (rc == NGX_ERROR) {
        printf("%s :: principal_kind_collision ... ok\n", label);
        stats->passed++;
    } else {
        printf("%s :: principal_kind_collision ... FAILED\n", label);
        stats->failed++;
    }

    /* entity attribute duplicate */
    unit_set_str(&name_b, "owner");
    unit_set_str(&ent_type, "User");
    unit_set_str(&ent_id, "alice");
    rc = nxe_cedar_eval_ctx_add_principal_attr_entity(ctx, &name_b,
                                                      &ent_type, &ent_id);
    if (rc != NGX_OK) {
        printf("%s :: principal_entity_first ... FAILED\n", label);
        stats->failed++;
    } else {
        printf("%s :: principal_entity_first ... ok\n", label);
        stats->passed++;
    }
    rc = nxe_cedar_eval_ctx_add_principal_attr_entity(ctx, &name_b,
                                                      &ent_type, &ent_id);
    if (rc == NGX_ERROR) {
        printf("%s :: principal_entity_dup ... ok\n", label);
        stats->passed++;
    } else {
        printf("%s :: principal_entity_dup ... FAILED\n", label);
        stats->failed++;
    }

    /* record field duplicate */
    {
        ngx_str_t rec_name;
        unit_set_str(&rec_name, "profile");
        rec = nxe_cedar_eval_ctx_add_principal_attr_record(ctx, &rec_name);
    }
    if (rec == NULL) {
        printf("%s :: record_create ... FAILED\n", label);
        stats->failed++;
    } else {
        ngx_str_t f_name, f_val;
        unit_set_str(&f_name, "city");
        unit_set_str(&f_val, "tokyo");

        rc = nxe_cedar_record_add_str(rec, &f_name, &f_val);
        if (rc != NGX_OK) {
            printf("%s :: record_field_first ... FAILED\n", label);
            stats->failed++;
        } else {
            printf("%s :: record_field_first ... ok\n", label);
            stats->passed++;
        }

        unit_set_str(&f_val, "osaka");
        rc = nxe_cedar_record_add_str(rec, &f_name, &f_val);
        if (rc == NGX_ERROR) {
            printf("%s :: record_field_dup ... ok\n", label);
            stats->passed++;
        } else {
            printf("%s :: record_field_dup ... FAILED\n", label);
            stats->failed++;
        }

        /* nested record name duplicate against existing scalar field */
        child = nxe_cedar_record_add_record(rec, &f_name);
        if (child == NULL) {
            printf("%s :: record_nested_dup ... ok\n", label);
            stats->passed++;
        } else {
            printf("%s :: record_nested_dup ... FAILED\n", label);
            stats->failed++;
        }
    }

    ngx_destroy_pool(pool);
}


/*
 * Verify nxe_cedar_eval_detail() returns the policies responsible
 * for the decision in each branch of the forbid-priority model:
 *   - forbid decision: detail lists matching forbids (every one of them)
 *   - permit decision: detail lists matching permits
 *   - default deny: detail is empty
 * Also covers nxe_cedar_policy_get_annotation() helper.
 */
static void
run_eval_detail_tests(test_stats_t *stats)
{
    ngx_pool_t *pool;
    ngx_log_t log;
    ngx_str_t text, principal_type, principal_id;
    ngx_str_t action_type, action_id, resource_type, resource_id;
    ngx_str_t annot_key;
    ngx_str_t *annot_val;
    nxe_cedar_policy_set_t *ps;
    nxe_cedar_eval_ctx_t *ctx;
    nxe_cedar_decision_t decision;
    nxe_cedar_decision_detail_t detail;
    nxe_cedar_policy_t *expected_policies;
    const char *label = "unit/eval_detail";

    memset(&log, 0, sizeof(log));
    pool = ngx_create_pool(8192, &log);
    if (pool == NULL) {
        fprintf(stderr, "%s :: setup ... FAILED (pool create)\n", label);
        stats->failed++;
        return;
    }

    /* --- permit decision lists every matching permit --- */
    text.data = (u_char *)
                "@id(\"p1\") permit (principal, action, resource);"
                "@id(\"p2\") permit (principal, action, resource);";
    text.len = strlen((const char *) text.data);

    ps = nxe_cedar_parse(pool, &log, &text);
    if (ps == NULL || ps->policies == NULL || ps->policies->nelts != 2) {
        printf("%s :: permit_parse ... FAILED\n", label);
        stats->failed++;
        ngx_destroy_pool(pool);
        return;
    }

    ctx = nxe_cedar_eval_ctx_create(pool);
    if (ctx == NULL) {
        fprintf(stderr, "%s :: permit_setup ... FAILED (ctx create)\n",
                label);
        stats->failed++;
        ngx_destroy_pool(pool);
        return;
    }

    unit_set_str(&principal_type, "User");
    unit_set_str(&principal_id, "alice");
    unit_set_str(&action_type, "Action");
    unit_set_str(&action_id, "GET");
    unit_set_str(&resource_type, "Endpoint");
    unit_set_str(&resource_id, "/api");
    nxe_cedar_eval_ctx_set_principal(ctx, &principal_type, &principal_id);
    nxe_cedar_eval_ctx_set_action(ctx, &action_type, &action_id);
    nxe_cedar_eval_ctx_set_resource(ctx, &resource_type, &resource_id);

    decision = nxe_cedar_eval_detail(ps, ctx, &log, &detail);
    expected_policies = ps->policies->elts;

    if (decision == NXE_CEDAR_DECISION_ALLOW
        && detail.npolicies == 2
        && detail.policies[0] == &expected_policies[0]
        && detail.policies[1] == &expected_policies[1])
    {
        printf("%s :: permit_lists_matches ... ok\n", label);
        stats->passed++;
    } else {
        printf("%s :: permit_lists_matches ... FAILED\n", label);
        fprintf(stderr, "  decision=%d npolicies=%lu\n",
                (int) decision, (unsigned long) detail.npolicies);
        stats->failed++;
    }

    /* Guard detail.policies[0] in case the preceding permit assertion
     * failed and left detail with no recorded policies. */
    if (detail.npolicies == 0 || detail.policies == NULL) {
        printf("%s :: annotation_lookup_hit ... FAILED\n", label);
        stats->failed++;
        printf("%s :: annotation_lookup_miss ... FAILED\n", label);
        stats->failed++;
    } else {
        /* annotation helper on the first matching permit */
        unit_set_str(&annot_key, "id");
        annot_val = nxe_cedar_policy_get_annotation(detail.policies[0],
                                                    &annot_key);
        if (annot_val != NULL && annot_val->len == 2
            && memcmp(annot_val->data, "p1", 2) == 0)
        {
            printf("%s :: annotation_lookup_hit ... ok\n", label);
            stats->passed++;
        } else {
            printf("%s :: annotation_lookup_hit ... FAILED\n", label);
            stats->failed++;
        }

        /* missing annotation returns NULL */
        unit_set_str(&annot_key, "missing");
        annot_val = nxe_cedar_policy_get_annotation(detail.policies[0],
                                                    &annot_key);
        if (annot_val == NULL) {
            printf("%s :: annotation_lookup_miss ... ok\n", label);
            stats->passed++;
        } else {
            printf("%s :: annotation_lookup_miss ... FAILED\n", label);
            stats->failed++;
        }
    }

    /* --- forbid decision overrides matching permits --- */
    text.data = (u_char *)
                "@id(\"allow_all\") permit (principal, action, resource);"
                "@id(\"deny_bob\") forbid (principal, action, resource)"
                " when { principal == User::\"bob\" };"
                "@id(\"deny_all\") forbid (principal, action, resource);";
    text.len = strlen((const char *) text.data);

    ps = nxe_cedar_parse(pool, &log, &text);
    if (ps == NULL || ps->policies == NULL || ps->policies->nelts != 3) {
        printf("%s :: forbid_parse ... FAILED\n", label);
        stats->failed++;
        ngx_destroy_pool(pool);
        return;
    }

    ctx = nxe_cedar_eval_ctx_create(pool);
    if (ctx == NULL) {
        fprintf(stderr, "%s :: forbid_setup ... FAILED (ctx create)\n",
                label);
        stats->failed++;
        ngx_destroy_pool(pool);
        return;
    }

    nxe_cedar_eval_ctx_set_principal(ctx, &principal_type, &principal_id);
    nxe_cedar_eval_ctx_set_action(ctx, &action_type, &action_id);
    nxe_cedar_eval_ctx_set_resource(ctx, &resource_type, &resource_id);

    decision = nxe_cedar_eval_detail(ps, ctx, &log, &detail);
    expected_policies = ps->policies->elts;

    /*
     * Only deny_all (index 2) matches because principal is alice, not
     * bob. The matching permit (allow_all) must not appear in detail
     * when a forbid wins.
     */
    if (decision == NXE_CEDAR_DECISION_DENY
        && detail.npolicies == 1
        && detail.policies[0] == &expected_policies[2])
    {
        printf("%s :: forbid_lists_matching_forbid ... ok\n", label);
        stats->passed++;
    } else {
        printf("%s :: forbid_lists_matching_forbid ... FAILED\n", label);
        fprintf(stderr, "  decision=%d npolicies=%lu\n",
                (int) decision, (unsigned long) detail.npolicies);
        stats->failed++;
    }

    /* --- multiple matching forbids are all listed --- */
    text.data = (u_char *)
                "@id(\"deny_a\") forbid (principal, action, resource);"
                "@id(\"allow_all\") permit (principal, action, resource);"
                "@id(\"deny_b\") forbid (principal, action, resource);";
    text.len = strlen((const char *) text.data);

    ps = nxe_cedar_parse(pool, &log, &text);
    if (ps == NULL || ps->policies == NULL || ps->policies->nelts != 3) {
        printf("%s :: multi_forbid_parse ... FAILED\n", label);
        stats->failed++;
        ngx_destroy_pool(pool);
        return;
    }

    ctx = nxe_cedar_eval_ctx_create(pool);
    if (ctx == NULL) {
        fprintf(stderr, "%s :: multi_forbid_setup ... FAILED (ctx create)\n",
                label);
        stats->failed++;
        ngx_destroy_pool(pool);
        return;
    }

    nxe_cedar_eval_ctx_set_principal(ctx, &principal_type, &principal_id);
    nxe_cedar_eval_ctx_set_action(ctx, &action_type, &action_id);
    nxe_cedar_eval_ctx_set_resource(ctx, &resource_type, &resource_id);

    decision = nxe_cedar_eval_detail(ps, ctx, &log, &detail);
    expected_policies = ps->policies->elts;

    /*
     * Both forbids (index 0 and 2) match; the diagnostic contract is
     * to return every contributing forbid, not just the first.
     */
    if (decision == NXE_CEDAR_DECISION_DENY
        && detail.npolicies == 2
        && detail.policies[0] == &expected_policies[0]
        && detail.policies[1] == &expected_policies[2])
    {
        printf("%s :: multi_forbid_all_listed ... ok\n", label);
        stats->passed++;
    } else {
        printf("%s :: multi_forbid_all_listed ... FAILED\n", label);
        fprintf(stderr, "  decision=%d npolicies=%lu\n",
                (int) decision, (unsigned long) detail.npolicies);
        stats->failed++;
    }

    /* --- annotation helper edge cases --- */
    /* NULL policy returns NULL */
    unit_set_str(&annot_key, "id");
    if (nxe_cedar_policy_get_annotation(NULL, &annot_key) == NULL) {
        printf("%s :: annotation_null_policy ... ok\n", label);
        stats->passed++;
    } else {
        printf("%s :: annotation_null_policy ... FAILED\n", label);
        stats->failed++;
    }

    /* NULL key returns NULL */
    if (nxe_cedar_policy_get_annotation(&expected_policies[0], NULL)
        == NULL)
    {
        printf("%s :: annotation_null_key ... ok\n", label);
        stats->passed++;
    } else {
        printf("%s :: annotation_null_key ... FAILED\n", label);
        stats->failed++;
    }

    /* --- default deny: no policy matches --- */
    text.data = (u_char *)
                "permit (principal == User::\"bob\", action, resource);";
    text.len = strlen((const char *) text.data);

    ps = nxe_cedar_parse(pool, &log, &text);
    if (ps == NULL) {
        printf("%s :: default_deny_parse ... FAILED\n", label);
        stats->failed++;
        ngx_destroy_pool(pool);
        return;
    }

    ctx = nxe_cedar_eval_ctx_create(pool);
    if (ctx == NULL) {
        fprintf(stderr, "%s :: default_deny_setup ... FAILED (ctx create)\n",
                label);
        stats->failed++;
        ngx_destroy_pool(pool);
        return;
    }

    nxe_cedar_eval_ctx_set_principal(ctx, &principal_type, &principal_id);
    nxe_cedar_eval_ctx_set_action(ctx, &action_type, &action_id);
    nxe_cedar_eval_ctx_set_resource(ctx, &resource_type, &resource_id);

    decision = nxe_cedar_eval_detail(ps, ctx, &log, &detail);

    if (decision == NXE_CEDAR_DECISION_DENY
        && detail.npolicies == 0
        && detail.policies == NULL)
    {
        printf("%s :: default_deny_empty_detail ... ok\n", label);
        stats->passed++;
    } else {
        printf("%s :: default_deny_empty_detail ... FAILED\n", label);
        fprintf(stderr, "  decision=%d npolicies=%lu\n",
                (int) decision, (unsigned long) detail.npolicies);
        stats->failed++;
    }

    /* annotation lookup on a policy without any annotations returns NULL */
    expected_policies = ps->policies->elts;
    unit_set_str(&annot_key, "id");
    if (nxe_cedar_policy_get_annotation(&expected_policies[0], &annot_key)
        == NULL)
    {
        printf("%s :: annotation_no_annotations ... ok\n", label);
        stats->passed++;
    } else {
        printf("%s :: annotation_no_annotations ... FAILED\n", label);
        stats->failed++;
    }

    /* --- NULL out: nxe_cedar_eval() wrapper still returns decision --- */
    decision = nxe_cedar_eval(ps, ctx, &log);
    if (decision == NXE_CEDAR_DECISION_DENY) {
        printf("%s :: wrapper_null_out ... ok\n", label);
        stats->passed++;
    } else {
        printf("%s :: wrapper_null_out ... FAILED\n", label);
        stats->failed++;
    }

    ngx_destroy_pool(pool);
}


static void
scan_phase_dir(const char *dir_path, int max_phase, test_stats_t *stats)
{
    DIR *dir;
    struct dirent *entry;
    char path[1024];
    size_t len;

    dir = opendir(dir_path);
    if (dir == NULL) {
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        len = strlen(entry->d_name);

        if (len < 6
            || strcmp(entry->d_name + len - 5, ".json") != 0)
        {
            continue;
        }

        snprintf(path, sizeof(path), "%s/%s",
                 dir_path, entry->d_name);
        run_test_file(path, max_phase, stats);
    }

    closedir(dir);
}


int
main(int argc, char **argv)
{
    DIR *dir;
    struct dirent *entry;
    char path[1024];
    const char *base_dir = "cases";
    const char *phase_env;
    int max_phase = 0;
    test_stats_t stats;

    (void) argc;
    (void) argv;

    memset(&stats, 0, sizeof(stats));

    phase_env = getenv("NXE_CEDAR_TEST_PHASE");
    if (phase_env != NULL) {
        max_phase = atoi(phase_env);
        if (max_phase <= 0) {
            fprintf(stderr, "invalid NXE_CEDAR_TEST_PHASE: %s\n",
                    phase_env);
            return 1;
        }
    }

    dir = opendir(base_dir);
    if (dir == NULL) {
        fprintf(stderr, "cannot open %s\n", base_dir);
        return 1;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "phase", 5) != 0) {
            continue;
        }

        snprintf(path, sizeof(path), "%s/%s",
                 base_dir, entry->d_name);
        scan_phase_dir(path, max_phase, &stats);
    }

    closedir(dir);

    if (max_phase == 0 || max_phase >= 2) {
        run_parser_null_guard_tests(&stats);
        run_injection_duplicate_key_tests(&stats);
        run_make_value_null_guard_tests(&stats);
    }

    if (max_phase == 0 || max_phase >= 4) {
        run_eval_detail_tests(&stats);
    }

    if (stats.passed + stats.failed == 0) {
        fprintf(stderr, "no test cases executed");
        if (max_phase > 0) {
            fprintf(stderr, " (phase <= %d)", max_phase);
        }
        fprintf(stderr, "\n");
        return 1;
    }

    printf("%d passed, %d failed",
           stats.passed, stats.failed);
    if (stats.skipped > 0) {
        printf(", %d skipped", stats.skipped);
    }
    printf("\n");

    return stats.failed > 0 ? 1 : 0;
}
