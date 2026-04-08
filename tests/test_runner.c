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
