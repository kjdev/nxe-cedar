/*
 * Copyright (c) Tatsuya Kamijo
 * Copyright (c) Bengo4.com, Inc.
 *
 * nxe_cedar_lexer.c - Cedar policy text tokenizer
 *
 * Converts input string to a token stream.
 * ngx_str_t is not NUL-terminated; always check bounds with pos < input.len.
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include "nxe_cedar_lexer.h"


/* keyword table entry */
typedef struct {
    ngx_str_t               name;
    nxe_cedar_token_type_t  type;
} nxe_cedar_keyword_t;


static nxe_cedar_keyword_t nxe_cedar_keywords[] = {
    { ngx_string("permit"),    NXE_CEDAR_TOKEN_PERMIT },
    { ngx_string("forbid"),    NXE_CEDAR_TOKEN_FORBID },
    { ngx_string("when"),      NXE_CEDAR_TOKEN_WHEN },
    { ngx_string("unless"),    NXE_CEDAR_TOKEN_UNLESS },
    { ngx_string("principal"), NXE_CEDAR_TOKEN_PRINCIPAL },
    { ngx_string("action"),    NXE_CEDAR_TOKEN_ACTION },
    { ngx_string("resource"),  NXE_CEDAR_TOKEN_RESOURCE },
    { ngx_string("context"),   NXE_CEDAR_TOKEN_CONTEXT },
    { ngx_string("true"),      NXE_CEDAR_TOKEN_TRUE },
    { ngx_string("false"),     NXE_CEDAR_TOKEN_FALSE },
    { ngx_string("in"),        NXE_CEDAR_TOKEN_IN },
    { ngx_string("if"),        NXE_CEDAR_TOKEN_IF },
    { ngx_string("then"),      NXE_CEDAR_TOKEN_THEN },
    { ngx_string("else"),      NXE_CEDAR_TOKEN_ELSE },
    { ngx_string("has"),       NXE_CEDAR_TOKEN_HAS },
    { ngx_string("like"),      NXE_CEDAR_TOKEN_LIKE },
    { ngx_string("ip"),        NXE_CEDAR_TOKEN_IP },
    { ngx_string("is"),        NXE_CEDAR_TOKEN_IS },
    { ngx_null_string,         0 }
};


static void
nxe_cedar_lexer_skip_whitespace(nxe_cedar_lexer_t *lexer)
{
    u_char ch;

    for ( ;; ) {
        if (lexer->pos >= lexer->input.len) {
            return;
        }

        ch = lexer->input.data[lexer->pos];

        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            lexer->pos++;
            continue;
        }

        /* // line comment */
        if (ch == '/'
            && lexer->pos + 1 < lexer->input.len
            && lexer->input.data[lexer->pos + 1] == '/')
        {
            lexer->pos += 2;

            while (lexer->pos < lexer->input.len
                   && lexer->input.data[lexer->pos] != '\n')
            {
                lexer->pos++;
            }

            if (lexer->pos < lexer->input.len) {
                lexer->pos++;  /* skip \n */
            }

            continue;
        }

        return;
    }
}


static ngx_int_t
nxe_cedar_hex_value(u_char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }

    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }

    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }

    return -1;
}


static ngx_uint_t
nxe_cedar_utf8_encode(ngx_uint_t cp, u_char *dst)
{
    if (cp <= 0x7F) {
        dst[0] = (u_char) cp;
        return 1;
    }

    if (cp <= 0x7FF) {
        dst[0] = (u_char) (0xC0 | (cp >> 6));
        dst[1] = (u_char) (0x80 | (cp & 0x3F));
        return 2;
    }

    if (cp <= 0xFFFF) {
        dst[0] = (u_char) (0xE0 | (cp >> 12));
        dst[1] = (u_char) (0x80 | ((cp >> 6) & 0x3F));
        dst[2] = (u_char) (0x80 | (cp & 0x3F));
        return 3;
    }

    /* cp <= 0x10FFFF */
    dst[0] = (u_char) (0xF0 | (cp >> 18));
    dst[1] = (u_char) (0x80 | ((cp >> 12) & 0x3F));
    dst[2] = (u_char) (0x80 | ((cp >> 6) & 0x3F));
    dst[3] = (u_char) (0x80 | (cp & 0x3F));
    return 4;
}


/*
 * Decode one escape sequence after the backslash.
 * *src points to the character after '\' (e.g. 'n' for \n).
 * On success: advances *src past the sequence, writes decoded bytes
 * to *dst, and returns NGX_OK.
 * On error: returns NGX_ERROR (*src and *dst are undefined).
 * reject_xff: if set, reject \xFF (0xFF reserved as wildcard marker).
 */
ngx_int_t
nxe_cedar_decode_escape(u_char **src, u_char *src_end,
    u_char **dst, ngx_flag_t reject_xff)
{
    u_char *p, *d;

    p = *src;
    d = *dst;

    if (p >= src_end) {
        return NGX_ERROR;
    }

    switch (*p) {
    case '"':
        *d++ = '"';
        p++;
        break;
    case '\\':
        *d++ = '\\';
        p++;
        break;
    case 'n':
        *d++ = '\n';
        p++;
        break;
    case 'r':
        *d++ = '\r';
        p++;
        break;
    case 't':
        *d++ = '\t';
        p++;
        break;
    case '0':
        *d++ = '\0';
        p++;
        break;
    case 'x':
    {
        ngx_int_t h1, h2;
        u_char byte;

        if (p + 2 >= src_end) {
            return NGX_ERROR;
        }

        h1 = nxe_cedar_hex_value(p[1]);
        h2 = nxe_cedar_hex_value(p[2]);
        if (h1 < 0 || h2 < 0) {
            return NGX_ERROR;
        }

        byte = (u_char) (h1 * 16 + h2);

        if (reject_xff && byte == 0xFF) {
            return NGX_ERROR;
        }

        *d++ = byte;
        p += 3;
    }
    break;

    case 'u':
    {
        ngx_uint_t cp, n_digits, nbytes;
        ngx_int_t dig;

        if (p + 1 >= src_end || p[1] != '{') {
            return NGX_ERROR;
        }

        p += 2;  /* skip u{ */
        cp = 0;
        n_digits = 0;

        while (p < src_end && *p != '}') {
            dig = nxe_cedar_hex_value(*p);
            if (dig < 0) {
                return NGX_ERROR;
            }

            cp = cp * 16 + dig;
            n_digits++;

            if (n_digits > 6) {
                return NGX_ERROR;
            }

            p++;
        }

        if (p >= src_end || n_digits == 0) {
            return NGX_ERROR;
        }

        if (cp > 0x10FFFF
            || (cp >= 0xD800 && cp <= 0xDFFF))
        {
            return NGX_ERROR;
        }

        /*
         * UTF-8 encoding never produces byte 0xFF (max leading byte
         * is 0xF4), so this cannot inject wildcard markers into
         * like patterns even without an explicit reject_xff check.
         */
        nbytes = nxe_cedar_utf8_encode(cp, d);
        d += nbytes;
        p++;  /* skip '}' */
    }
    break;

    default:
        return NGX_ERROR;
    }

    *src = p;
    *dst = d;
    return NGX_OK;
}


static nxe_cedar_token_t
nxe_cedar_lexer_read_string(nxe_cedar_lexer_t *lexer)
{
    nxe_cedar_token_t token;
    size_t start, len;
    u_char *dst;
    ngx_uint_t has_escape;

    /* skip opening quote */
    lexer->pos++;
    start = lexer->pos;
    has_escape = 0;

    while (lexer->pos < lexer->input.len) {
        if (lexer->input.data[lexer->pos] == '\\') {
            has_escape = 1;

            if (lexer->pos + 1 >= lexer->input.len) {
                /* trailing backslash without following char */
                lexer->pos++;
                break;
            }

            lexer->pos += 2;
            continue;
        }

        if (lexer->input.data[lexer->pos] == '"') {
            break;
        }

        lexer->pos++;
    }

    if (lexer->pos >= lexer->input.len) {
        token.type = NXE_CEDAR_TOKEN_ERROR;
        token.value.data = (u_char *) "unterminated string";
        token.value.len = 19;
        token.raw.data = NULL;
        token.raw.len = 0;
        token.has_star_escape = 0;
        return token;
    }

    /* content between quotes */
    len = lexer->pos - start;
    lexer->pos++;  /* skip closing quote */

    token.type = NXE_CEDAR_TOKEN_STRING;
    token.raw.data = &lexer->input.data[start];
    token.raw.len = len;
    token.has_star_escape = 0;

    if (!has_escape) {
        token.value.data = &lexer->input.data[start];
        token.value.len = len;
        return token;
    }

    /* unescape into pool-allocated buffer */
    dst = ngx_palloc(lexer->pool, len);
    if (dst == NULL) {
        token.type = NXE_CEDAR_TOKEN_ERROR;
        token.value.data = (u_char *) "alloc failed";
        token.value.len = 12;
        token.has_star_escape = 0;
        return token;
    }

    token.value.data = dst;

    {
        u_char *sp, *sp_end;

        sp = &lexer->input.data[start];
        sp_end = sp + len;

        while (sp < sp_end) {
            if (*sp == '\\' && sp + 1 < sp_end) {
                sp++;  /* skip backslash */

                /*
                 * \* is only valid in like pattern strings, but the
                 * lexer cannot distinguish pattern from regular strings.
                 * Accept it here so the token is produced; the pattern
                 * compiler re-processes via token.raw anyway.
                 */
                if (*sp == '*') {
                    *dst++ = '*';
                    sp++;
                    token.has_star_escape = 1;
                } else if (nxe_cedar_decode_escape(&sp, sp_end,
                                                   &dst, 0)
                           != NGX_OK)
                {
                    token.type = NXE_CEDAR_TOKEN_ERROR;
                    token.value.data =
                        (u_char *) "invalid escape sequence";
                    token.value.len = 23;
                    return token;
                }

            } else {
                *dst++ = *sp++;
            }
        }
    }

    token.value.len = dst - token.value.data;

    return token;
}


static nxe_cedar_token_t
nxe_cedar_lexer_read_number(nxe_cedar_lexer_t *lexer)
{
    nxe_cedar_token_t token;
    size_t start;

    start = lexer->pos;

    while (lexer->pos < lexer->input.len
           && lexer->input.data[lexer->pos] >= '0'
           && lexer->input.data[lexer->pos] <= '9')
    {
        lexer->pos++;
    }

    token.type = NXE_CEDAR_TOKEN_NUMBER;
    token.value.data = &lexer->input.data[start];
    token.value.len = lexer->pos - start;
    token.raw.data = NULL;
    token.raw.len = 0;
    token.has_star_escape = 0;

    return token;
}


static nxe_cedar_token_t
nxe_cedar_lexer_read_ident(nxe_cedar_lexer_t *lexer)
{
    nxe_cedar_token_t token;
    nxe_cedar_keyword_t *kw;
    size_t start;
    ngx_uint_t len;

    start = lexer->pos;

    while (lexer->pos < lexer->input.len) {
        u_char ch = lexer->input.data[lexer->pos];

        if ((ch >= 'a' && ch <= 'z')
            || (ch >= 'A' && ch <= 'Z')
            || (ch >= '0' && ch <= '9')
            || ch == '_')
        {
            lexer->pos++;
        } else {
            break;
        }
    }

    len = lexer->pos - start;

    /* check keywords */
    for (kw = nxe_cedar_keywords; kw->name.len != 0; kw++) {
        if (kw->name.len == len
            && ngx_memcmp(kw->name.data,
                          &lexer->input.data[start], len) == 0)
        {
            token.type = kw->type;
            token.value.data = &lexer->input.data[start];
            token.value.len = len;
            token.raw.data = NULL;
            token.raw.len = 0;
            token.has_star_escape = 0;
            return token;
        }
    }

    token.type = NXE_CEDAR_TOKEN_IDENT;
    token.value.data = &lexer->input.data[start];
    token.value.len = len;
    token.raw.data = NULL;
    token.raw.len = 0;
    token.has_star_escape = 0;

    return token;
}


void
nxe_cedar_lexer_init(nxe_cedar_lexer_t *lexer,
    ngx_pool_t *pool, ngx_log_t *log, const ngx_str_t *input)
{
    lexer->input = *input;
    lexer->pos = 0;
    lexer->pool = pool;
    lexer->log = log;
}


nxe_cedar_token_t
nxe_cedar_lexer_next(nxe_cedar_lexer_t *lexer)
{
    nxe_cedar_token_t token;
    u_char ch;

    token.raw.data = NULL;
    token.raw.len = 0;
    token.has_star_escape = 0;

    nxe_cedar_lexer_skip_whitespace(lexer);

    if (lexer->pos >= lexer->input.len) {
        token.type = NXE_CEDAR_TOKEN_EOF;
        token.value.data = NULL;
        token.value.len = 0;
        return token;
    }

    ch = lexer->input.data[lexer->pos];

    /* string literal */
    if (ch == '"') {
        return nxe_cedar_lexer_read_string(lexer);
    }

    /* number literal */
    if (ch >= '0' && ch <= '9') {
        return nxe_cedar_lexer_read_number(lexer);
    }

    /* identifier or keyword */
    if ((ch >= 'a' && ch <= 'z')
        || (ch >= 'A' && ch <= 'Z')
        || ch == '_')
    {
        return nxe_cedar_lexer_read_ident(lexer);
    }

    /* two-character operators */
    if (ch == '=' && lexer->pos + 1 < lexer->input.len
        && lexer->input.data[lexer->pos + 1] == '=')
    {
        token.type = NXE_CEDAR_TOKEN_EQ;
        token.value.data = &lexer->input.data[lexer->pos];
        token.value.len = 2;
        lexer->pos += 2;
        return token;
    }

    if (ch == '!' && lexer->pos + 1 < lexer->input.len
        && lexer->input.data[lexer->pos + 1] == '=')
    {
        token.type = NXE_CEDAR_TOKEN_NE;
        token.value.data = &lexer->input.data[lexer->pos];
        token.value.len = 2;
        lexer->pos += 2;
        return token;
    }

    if (ch == '&' && lexer->pos + 1 < lexer->input.len
        && lexer->input.data[lexer->pos + 1] == '&')
    {
        token.type = NXE_CEDAR_TOKEN_AND;
        token.value.data = &lexer->input.data[lexer->pos];
        token.value.len = 2;
        lexer->pos += 2;
        return token;
    }

    if (ch == '|' && lexer->pos + 1 < lexer->input.len
        && lexer->input.data[lexer->pos + 1] == '|')
    {
        token.type = NXE_CEDAR_TOKEN_OR;
        token.value.data = &lexer->input.data[lexer->pos];
        token.value.len = 2;
        lexer->pos += 2;
        return token;
    }

    if (ch == ':' && lexer->pos + 1 < lexer->input.len
        && lexer->input.data[lexer->pos + 1] == ':')
    {
        token.type = NXE_CEDAR_TOKEN_COLONCOLON;
        token.value.data = &lexer->input.data[lexer->pos];
        token.value.len = 2;
        lexer->pos += 2;
        return token;
    }

    if (ch == '<' && lexer->pos + 1 < lexer->input.len
        && lexer->input.data[lexer->pos + 1] == '=')
    {
        token.type = NXE_CEDAR_TOKEN_LE;
        token.value.data = &lexer->input.data[lexer->pos];
        token.value.len = 2;
        lexer->pos += 2;
        return token;
    }

    if (ch == '>' && lexer->pos + 1 < lexer->input.len
        && lexer->input.data[lexer->pos + 1] == '=')
    {
        token.type = NXE_CEDAR_TOKEN_GE;
        token.value.data = &lexer->input.data[lexer->pos];
        token.value.len = 2;
        lexer->pos += 2;
        return token;
    }

    /* single-character tokens */
    token.value.data = &lexer->input.data[lexer->pos];
    token.value.len = 1;
    lexer->pos++;

    switch (ch) {
    case '!':
        token.type = NXE_CEDAR_TOKEN_NOT;
        return token;
    case '-':
        token.type = NXE_CEDAR_TOKEN_NEGATE;
        return token;
    case '.':
        token.type = NXE_CEDAR_TOKEN_DOT;
        return token;
    case ',':
        token.type = NXE_CEDAR_TOKEN_COMMA;
        return token;
    case ';':
        token.type = NXE_CEDAR_TOKEN_SEMICOLON;
        return token;
    case '(':
        token.type = NXE_CEDAR_TOKEN_LPAREN;
        return token;
    case ')':
        token.type = NXE_CEDAR_TOKEN_RPAREN;
        return token;
    case '{':
        token.type = NXE_CEDAR_TOKEN_LBRACE;
        return token;
    case '}':
        token.type = NXE_CEDAR_TOKEN_RBRACE;
        return token;
    case '[':
        token.type = NXE_CEDAR_TOKEN_LBRACKET;
        return token;
    case ']':
        token.type = NXE_CEDAR_TOKEN_RBRACKET;
        return token;
    case '@':
        token.type = NXE_CEDAR_TOKEN_AT;
        return token;
    case '<':
        token.type = NXE_CEDAR_TOKEN_LT;
        return token;
    case '>':
        token.type = NXE_CEDAR_TOKEN_GT;
        return token;
    default:
        break;
    }

    token.type = NXE_CEDAR_TOKEN_ERROR;
    token.value.data = &lexer->input.data[lexer->pos - 1];
    token.value.len = 1;

    return token;
}
