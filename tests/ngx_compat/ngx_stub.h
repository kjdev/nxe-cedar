/*
 * Copyright (c) Tatsuya Kamijo
 * Copyright (c) Bengo4.com, Inc.
 *
 * ngx_stub.h - nginx type/function test stubs (declarations)
 *
 * Provides minimal nginx core types and functions for nxe-cedar unit tests.
 * Implementation in ngx_stub.c uses malloc/free.
 */

#ifndef NGX_STUB_H
#define NGX_STUB_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>


/* --- basic types --- */

typedef unsigned char u_char;
typedef intptr_t ngx_int_t;
typedef uintptr_t ngx_uint_t;
typedef intptr_t ngx_flag_t;

#define NGX_OK                      0
#define NGX_ERROR                   (-1)

#ifndef NULL
#define NULL                        ((void *) 0)
#endif


/* --- ngx_str_t --- */

typedef struct {
    size_t  len;
    u_char *data;
} ngx_str_t;

#define ngx_string(str)             { sizeof(str) - 1, (u_char *) str }
#define ngx_null_string             { 0, NULL }

#define ngx_str_set(str, text)                                               \
        (str)->len = sizeof(text) - 1; (str)->data = (u_char *) text

#define ngx_str_null(str)           (str)->len = 0; (str)->data = NULL


/* --- forward declarations --- */

typedef struct ngx_pool_s ngx_pool_t;
typedef struct ngx_log_s ngx_log_t;
typedef struct ngx_pool_large_s ngx_pool_large_t;


/* --- ngx_log_t (minimal) --- */

#define NGX_LOG_STDERR              0
#define NGX_LOG_EMERG               1
#define NGX_LOG_ALERT               2
#define NGX_LOG_CRIT                3
#define NGX_LOG_ERR                 4
#define NGX_LOG_WARN                5
#define NGX_LOG_NOTICE              6
#define NGX_LOG_INFO                7
#define NGX_LOG_DEBUG               8

struct ngx_log_s {
    ngx_uint_t  log_level;
};

#define ngx_log_error(level, log, err, ...)                                   \
        do {                                                                     \
            (void) (err);                                                        \
            if ((log) != NULL && (ngx_uint_t) (level) <= (log)->log_level) {      \
                fprintf(stderr, "[ngx_stub] ");                                  \
                fprintf(stderr, __VA_ARGS__);                                    \
                fprintf(stderr, "\n");                                           \
            }                                                                    \
        } while (0)


/* --- ngx_pool_t (malloc-based simple implementation) --- */

struct ngx_pool_large_s {
    ngx_pool_large_t *next;
    void             *alloc;
};

struct ngx_pool_s {
    ngx_pool_large_t *large;
    ngx_log_t        *log;
};

ngx_pool_t *ngx_create_pool(size_t size, ngx_log_t *log);
void ngx_destroy_pool(ngx_pool_t *pool);

void *ngx_palloc(ngx_pool_t *pool, size_t size);
void *ngx_pnalloc(ngx_pool_t *pool, size_t size);
void *ngx_pcalloc(ngx_pool_t *pool, size_t size);
ngx_int_t ngx_pfree(ngx_pool_t *pool, void *p);


/* --- ngx_array_t --- */

typedef struct {
    void       *elts;
    ngx_uint_t  nelts;
    size_t      size;
    ngx_uint_t  nalloc;
    ngx_pool_t *pool;
} ngx_array_t;

ngx_array_t *ngx_array_create(ngx_pool_t *p, ngx_uint_t n, size_t size);
void ngx_array_destroy(ngx_array_t *a);
void *ngx_array_push(ngx_array_t *a);
void *ngx_array_push_n(ngx_array_t *a, ngx_uint_t n);

static inline ngx_int_t
ngx_array_init(ngx_array_t *array, ngx_pool_t *pool,
    ngx_uint_t n, size_t size)
{
    size_t  alloc_size;

    if (size != 0 && n > SIZE_MAX / size) {
        return NGX_ERROR;
    }

    alloc_size = n * size;

    array->nelts = 0;
    array->size = size;
    array->nalloc = n;
    array->pool = pool;

    array->elts = ngx_palloc(pool, alloc_size);
    if (array->elts == NULL) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


/* --- string/memory operation macros --- */

#define ngx_memcmp(s1, s2, n)       memcmp(s1, s2, n)
#define ngx_memcpy(dst, src, n)     memcpy(dst, src, n)
#define ngx_memzero(buf, n)         memset(buf, 0, n)
#define ngx_memset(buf, c, n)       memset(buf, c, n)
#define ngx_strlen(s)               strlen((const char *) (s))
#define ngx_strncmp(s1, s2, n)      strncmp((const char *) (s1), \
                                            (const char *) (s2), n)
#define ngx_strcmp(s1, s2)          strcmp((const char *) (s1), \
                                           (const char *) (s2))

/* ngx_pstrdup: duplicate string on pool (not NUL-terminated) */
u_char *ngx_pstrdup(ngx_pool_t *pool, ngx_str_t *src);


#endif /* NGX_STUB_H */
