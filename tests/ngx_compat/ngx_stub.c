/*
 * Copyright (c) Tatsuya Kamijo
 * Copyright (c) Bengo4.com, Inc.
 *
 * ngx_stub.c - nginx type/function test stubs (implementation)
 *
 * Implements ngx_pool_t and ngx_array_t using malloc/free.
 * ngx_pool_t tracks all allocations via ngx_pool_large_t list
 * and frees them all in ngx_destroy_pool.
 */

#include "ngx_stub.h"

#include <stdlib.h>


/* --- ngx_pool_t --- */

ngx_pool_t *
ngx_create_pool(size_t size, ngx_log_t *log)
{
    ngx_pool_t *pool;

    (void) size;

    pool = malloc(sizeof(ngx_pool_t));
    if (pool == NULL) {
        return NULL;
    }

    pool->large = NULL;
    pool->log = log;

    return pool;
}


void
ngx_destroy_pool(ngx_pool_t *pool)
{
    ngx_pool_large_t *l, *next;

    for (l = pool->large; l; l = next) {
        next = l->next;
        free(l->alloc);
        free(l);
    }

    free(pool);
}


void *
ngx_palloc(ngx_pool_t *pool, size_t size)
{
    void *p;
    ngx_pool_large_t *large;

    p = malloc(size);
    if (p == NULL) {
        return NULL;
    }

    large = malloc(sizeof(ngx_pool_large_t));
    if (large == NULL) {
        free(p);
        return NULL;
    }

    large->alloc = p;
    large->next = pool->large;
    pool->large = large;

    return p;
}


void *
ngx_pnalloc(ngx_pool_t *pool, size_t size)
{
    return ngx_palloc(pool, size);
}


void *
ngx_pcalloc(ngx_pool_t *pool, size_t size)
{
    void *p;

    p = ngx_palloc(pool, size);
    if (p != NULL) {
        ngx_memzero(p, size);
    }

    return p;
}


ngx_int_t
ngx_pfree(ngx_pool_t *pool, void *p)
{
    ngx_pool_large_t *l, **prev;

    prev = &pool->large;
    for (l = pool->large; l; l = l->next) {
        if (l->alloc == p) {
            *prev = l->next;
            free(l->alloc);
            free(l);
            return NGX_OK;
        }
        prev = &l->next;
    }

    return NGX_ERROR;
}


/* --- ngx_array_t --- */

ngx_array_t *
ngx_array_create(ngx_pool_t *p, ngx_uint_t n, size_t size)
{
    ngx_array_t *a;

    a = ngx_palloc(p, sizeof(ngx_array_t));
    if (a == NULL) {
        return NULL;
    }

    if (ngx_array_init(a, p, n, size) != NGX_OK) {
        return NULL;
    }

    return a;
}


void
ngx_array_destroy(ngx_array_t *a)
{
    (void) a;
    /* no individual free; managed by pool */
}


void *
ngx_array_push(ngx_array_t *a)
{
    void *elt, *new_elts;

    if (a->nelts == a->nalloc) {
        /* double capacity */
        ngx_uint_t new_nalloc;
        size_t alloc_size;

        new_nalloc = a->nalloc * 2;
        if (new_nalloc == 0) {
            new_nalloc = 4;
        }

        if (a->size != 0 && new_nalloc > SIZE_MAX / a->size) {
            return NULL;
        }

        alloc_size = new_nalloc * a->size;

        new_elts = ngx_palloc(a->pool, alloc_size);
        if (new_elts == NULL) {
            return NULL;
        }

        ngx_memcpy(new_elts, a->elts, a->nelts * a->size);
        a->elts = new_elts;
        a->nalloc = new_nalloc;
    }

    elt = (u_char *) a->elts + a->nelts * a->size;
    a->nelts++;

    return elt;
}


void *
ngx_array_push_n(ngx_array_t *a, ngx_uint_t n)
{
    void *elt, *new_elts;
    ngx_uint_t required;

    if (n > (ngx_uint_t) -1 - a->nelts) {
        return NULL;
    }

    required = a->nelts + n;

    if (required > a->nalloc) {
        ngx_uint_t new_nalloc;
        size_t alloc_size;

        new_nalloc = a->nalloc * 2;
        if (new_nalloc < required) {
            new_nalloc = required;
        }

        if (a->size != 0 && new_nalloc > SIZE_MAX / a->size) {
            return NULL;
        }

        alloc_size = new_nalloc * a->size;

        new_elts = ngx_palloc(a->pool, alloc_size);
        if (new_elts == NULL) {
            return NULL;
        }

        ngx_memcpy(new_elts, a->elts, a->nelts * a->size);
        a->elts = new_elts;
        a->nalloc = new_nalloc;
    }

    elt = (u_char *) a->elts + a->nelts * a->size;
    a->nelts += n;

    return elt;
}


/* --- string operations --- */

u_char *
ngx_pstrdup(ngx_pool_t *pool, ngx_str_t *src)
{
    u_char *dst;

    dst = ngx_palloc(pool, src->len);
    if (dst == NULL) {
        return NULL;
    }

    ngx_memcpy(dst, src->data, src->len);

    return dst;
}
