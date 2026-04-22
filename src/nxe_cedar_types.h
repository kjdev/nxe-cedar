/*
 * Copyright (c) Tatsuya Kamijo
 * Copyright (c) Bengo4.com, Inc.
 *
 * nxe_cedar_types.h - data structure definitions for nxe-cedar
 *
 * Type definitions used by the Cedar policy language subset
 * implementation in C. No implementation code, types only.
 */

#ifndef NXE_CEDAR_TYPES_H
#define NXE_CEDAR_TYPES_H

#include <ngx_config.h>
#include <ngx_core.h>
#include <stdint.h>


/* --- decision result --- */

typedef enum {
    NXE_CEDAR_DECISION_DENY  = 0,   /* deny (default) */
    NXE_CEDAR_DECISION_ALLOW = 1    /* allow */
} nxe_cedar_decision_t;


/* --- token types --- */

typedef enum {
    /* keywords */
    NXE_CEDAR_TOKEN_PERMIT,
    NXE_CEDAR_TOKEN_FORBID,
    NXE_CEDAR_TOKEN_WHEN,
    NXE_CEDAR_TOKEN_UNLESS,
    NXE_CEDAR_TOKEN_PRINCIPAL,
    NXE_CEDAR_TOKEN_ACTION,
    NXE_CEDAR_TOKEN_RESOURCE,
    NXE_CEDAR_TOKEN_CONTEXT,
    NXE_CEDAR_TOKEN_TRUE,
    NXE_CEDAR_TOKEN_FALSE,
    NXE_CEDAR_TOKEN_IN,
    NXE_CEDAR_TOKEN_IF,             /* Phase 2 */
    NXE_CEDAR_TOKEN_THEN,           /* Phase 2 */
    NXE_CEDAR_TOKEN_ELSE,           /* Phase 2 */
    NXE_CEDAR_TOKEN_HAS,            /* Phase 2 */
    NXE_CEDAR_TOKEN_LIKE,           /* Phase 2 */
    NXE_CEDAR_TOKEN_IP,             /* Phase 3 */
    NXE_CEDAR_TOKEN_IS,             /* Phase 4 */

    /* operators */
    NXE_CEDAR_TOKEN_EQ,             /* == */
    NXE_CEDAR_TOKEN_NE,             /* != */
    NXE_CEDAR_TOKEN_AND,            /* && */
    NXE_CEDAR_TOKEN_OR,             /* || */
    NXE_CEDAR_TOKEN_NOT,            /* ! */
    NXE_CEDAR_TOKEN_MINUS,          /* -  (binary and unary; Phase 4) */
    NXE_CEDAR_TOKEN_PLUS,           /* +  (Phase 4) */
    NXE_CEDAR_TOKEN_STAR,           /* *  (Phase 4) */
    NXE_CEDAR_TOKEN_LT,             /* <  (Phase 2) */
    NXE_CEDAR_TOKEN_GT,             /* >  (Phase 2) */
    NXE_CEDAR_TOKEN_LE,             /* <= (Phase 2) */
    NXE_CEDAR_TOKEN_GE,             /* >= (Phase 2) */

    /* delimiters */
    NXE_CEDAR_TOKEN_DOT,            /* . */
    NXE_CEDAR_TOKEN_COMMA,          /* , */
    NXE_CEDAR_TOKEN_SEMICOLON,      /* ; */
    NXE_CEDAR_TOKEN_LPAREN,         /* ( */
    NXE_CEDAR_TOKEN_RPAREN,         /* ) */
    NXE_CEDAR_TOKEN_LBRACE,         /* { */
    NXE_CEDAR_TOKEN_RBRACE,         /* } */
    NXE_CEDAR_TOKEN_LBRACKET,       /* [ */
    NXE_CEDAR_TOKEN_RBRACKET,       /* ] */
    NXE_CEDAR_TOKEN_COLONCOLON,     /* :: */
    NXE_CEDAR_TOKEN_COLON,          /* :  (Phase 4 record literal) */
    NXE_CEDAR_TOKEN_AT,             /* @  (Phase 4) */

    /* literals */
    NXE_CEDAR_TOKEN_STRING,         /* "..." */
    NXE_CEDAR_TOKEN_NUMBER,         /* [0-9]+ */
    NXE_CEDAR_TOKEN_IDENT,          /* identifier */

    /* special */
    NXE_CEDAR_TOKEN_EOF,
    NXE_CEDAR_TOKEN_ERROR
} nxe_cedar_token_type_t;


/* --- token --- */

typedef struct {
    nxe_cedar_token_type_t  type;
    ngx_str_t               value;      /* string representation */
    ngx_str_t               raw;        /* raw source for STRING tokens
                                           (used by like pattern compiler) */
    ngx_flag_t              has_star_escape;  /* 1 if \* found in string */
} nxe_cedar_token_t;


/* --- binary operators --- */

typedef enum {
    NXE_CEDAR_OP_EQ,                /* == */
    NXE_CEDAR_OP_NE,                /* != */
    NXE_CEDAR_OP_AND,               /* && */
    NXE_CEDAR_OP_OR,                /* || */
    NXE_CEDAR_OP_IN,                /* in */
    NXE_CEDAR_OP_LT,               /* <  (Phase 2) */
    NXE_CEDAR_OP_GT,               /* >  (Phase 2) */
    NXE_CEDAR_OP_LE,               /* <= (Phase 2) */
    NXE_CEDAR_OP_GE,               /* >= (Phase 2) */
    NXE_CEDAR_OP_PLUS,             /* +  (Phase 4) */
    NXE_CEDAR_OP_MINUS,            /* -  (Phase 4) */
    NXE_CEDAR_OP_MUL               /* *  (Phase 4) */
} nxe_cedar_op_t;


/* --- variable types --- */

typedef enum {
    NXE_CEDAR_VAR_PRINCIPAL = 0,
    NXE_CEDAR_VAR_ACTION    = 1,
    NXE_CEDAR_VAR_RESOURCE  = 2,
    NXE_CEDAR_VAR_CONTEXT   = 3
} nxe_cedar_var_type_t;


/* --- AST node types --- */

typedef enum {
    /* literals */
    NXE_CEDAR_NODE_BOOL_LIT,        /* true / false */
    NXE_CEDAR_NODE_STRING_LIT,      /* "..." */
    NXE_CEDAR_NODE_LONG_LIT,        /* integer */
    NXE_CEDAR_NODE_ENTITY_REF,      /* Type::"id" */
    NXE_CEDAR_NODE_SET,             /* [expr, ...] */

    /* variables */
    NXE_CEDAR_NODE_VAR,             /* principal, action, resource, context */

    /* operations */
    NXE_CEDAR_NODE_ATTR_ACCESS,     /* expr.ident */
    NXE_CEDAR_NODE_BINOP,           /* ==, !=, <, >, <=, >=, &&, ||, in,
                                     +, -, * (Phase 4) */
    NXE_CEDAR_NODE_UNOP,            /* ! */
    NXE_CEDAR_NODE_NEGATE,          /* - (unary) */

    /* Phase 2 */
    NXE_CEDAR_NODE_HAS,             /* expr has ident */
    NXE_CEDAR_NODE_LIKE,            /* expr like "pattern" */
    NXE_CEDAR_NODE_IF_THEN_ELSE,    /* if expr then expr else expr */
    NXE_CEDAR_NODE_METHOD_CALL,     /* expr.method(args) */

    /* Phase 3 */
    NXE_CEDAR_NODE_IP_LITERAL,      /* ip("addr") */

    /* Phase 4 */
    NXE_CEDAR_NODE_IS,              /* expr is type_name [in expr] */
    NXE_CEDAR_NODE_RECORD           /* { key: expr, ... } */
} nxe_cedar_node_type_t;


/* --- AST node --- */

typedef struct nxe_cedar_node_s nxe_cedar_node_t;

/* parse-time record literal entry (key and value expression) */
typedef struct {
    ngx_str_t         key;
    nxe_cedar_node_t *value;
} nxe_cedar_record_entry_t;

struct nxe_cedar_node_s {
    nxe_cedar_node_type_t  type;
    union {
        ngx_flag_t  bool_val;                   /* BOOL_LIT */
        ngx_str_t   string_val;                 /* STRING_LIT, LIKE pattern */
        int64_t     long_val;                   /* LONG_LIT (Cedar i64) */

        struct {                                /* ENTITY_REF */
            ngx_str_t  entity_type;
            ngx_str_t  entity_id;
        } entity_ref;

        nxe_cedar_var_type_t  var_type;         /* VAR */

        struct {                                /* ATTR_ACCESS */
            nxe_cedar_node_t *object;
            ngx_str_t         attr;
        } attr_access;

        struct {                                /* BINOP */
            ngx_uint_t        op;               /* nxe_cedar_op_t */
            nxe_cedar_node_t *left;
            nxe_cedar_node_t *right;
        } binop;

        struct {                                /* UNOP */
            nxe_cedar_node_t *operand;
        } unop;

        ngx_array_t *set_elts;                  /* SET: array of
                                                        nxe_cedar_node_t* */

        struct {                                /* HAS */
            nxe_cedar_node_t *object;
            ngx_str_t         attr;
        } has;

        struct {                                /* LIKE */
            nxe_cedar_node_t *object;
            ngx_str_t         pattern;
        } like;

        struct {                                /* IF_THEN_ELSE */
            nxe_cedar_node_t *cond;
            nxe_cedar_node_t *then_expr;
            nxe_cedar_node_t *else_expr;
        } if_then_else;

        struct {                                /* METHOD_CALL */
            nxe_cedar_node_t *object;
            ngx_str_t         method;           /* "containsAll",
                                                   "containsAny",
                                                   "contains",
                                                   "isInRange",
                                                   "isIpv4", "isIpv6",
                                                   "isLoopback",
                                                   "isMulticast" */
            nxe_cedar_node_t *arg;              /* NULL for zero-arg
                                                   methods (isIpv4 etc.) */
        } method_call;

        struct {                                /* IP_LITERAL */
            ngx_str_t  addr;
        } ip_literal;

        struct {                                /* IS (Phase 4) */
            nxe_cedar_node_t *object;           /* expression under test */
            ngx_str_t         entity_type;      /* type_name
                                                   ("User", "Ns::User", ...) */
            nxe_cedar_node_t *in_entity;        /* "is T in expr" expr,
                                                   NULL if plain "is T" */
        } is_check;

        ngx_array_t *record_entries;            /* RECORD: array of
                                                        nxe_cedar_record_entry_t */
    } u;
};


/* --- policy structures --- */

/* scope constraint type */
typedef enum {
    NXE_CEDAR_SCOPE_NONE,           /* no constraint (matches all) */
    NXE_CEDAR_SCOPE_EQ,             /* == entity_ref */
    NXE_CEDAR_SCOPE_IN,             /* in entity_ref | set */
    NXE_CEDAR_SCOPE_IS,             /* is type_name (Phase 4) */
    NXE_CEDAR_SCOPE_IS_IN           /* is type_name in entity_ref
                                       (Phase 4) */
} nxe_cedar_scope_constraint_t;

/* scope constraint */
typedef struct {
    nxe_cedar_scope_constraint_t  constraint;
    nxe_cedar_node_t             *target;   /* entity_ref or set
                                               (NULL if NONE, IS) */
    ngx_str_t                     entity_type;  /* type_name (IS, IS_IN
                                                   only; empty otherwise) */
} nxe_cedar_scope_t;

/* annotation (Phase 4) */
typedef struct {
    ngx_str_t  key;                     /* annotation name (e.g. "id", "advice") */
    ngx_str_t  value;                   /* annotation value; empty if valueless */
} nxe_cedar_annotation_t;

/* condition clause */
typedef struct {
    unsigned          is_unless:1;          /* 0 = when, 1 = unless */
    nxe_cedar_node_t *expr;
} nxe_cedar_condition_t;

/* single policy */
typedef struct {
    unsigned           is_forbid:1;         /* 0 = permit, 1 = forbid */
    ngx_array_t       *annotations;         /* array of nxe_cedar_annotation_t
                                               (Phase 4, NULL if none) */
    nxe_cedar_scope_t  principal;
    nxe_cedar_scope_t  action;
    nxe_cedar_scope_t  resource;
    ngx_array_t       *conditions;          /* array of
                                               nxe_cedar_condition_t */
} nxe_cedar_policy_t;

/* policy set */
typedef struct {
    ngx_array_t *policies;                  /* array of nxe_cedar_policy_t */
} nxe_cedar_policy_set_t;


/* --- evaluation context --- */

/*
 * Parser member-chain limit. Caps `expr.a.b.c...` to this many `.ident`
 * or `["key"]` steps. Shared with the record-value nesting limit below
 * so the parser's reachable depth and the writable record depth stay
 * in sync (bumping one automatically bumps the other).
 */
#define NXE_CEDAR_MAX_MEMBER_CHAIN 16

/*
 * Record-value nesting limit. Defined as NXE_CEDAR_MAX_MEMBER_CHAIN so
 * no record value can be created at a depth that policy text cannot
 * reference: a depth-N record is the value returned by an N-step member
 * chain, and the parser caps that chain at NXE_CEDAR_MAX_MEMBER_CHAIN.
 * Note that reading a scalar (or sub-record) inside a depth-N record
 * takes N+1 steps, so scalar fields placed directly inside a
 * depth-NXE_CEDAR_MAX_RECORD_DEPTH record are writable but unreachable
 * from any policy. Keep deep scalars one level above the limit.
 */
#define NXE_CEDAR_MAX_RECORD_DEPTH NXE_CEDAR_MAX_MEMBER_CHAIN


/* --- runtime values --- */

/* runtime value types */
#define NXE_CEDAR_RVAL_STRING   0
#define NXE_CEDAR_RVAL_LONG     1
#define NXE_CEDAR_RVAL_BOOL     2
#define NXE_CEDAR_RVAL_ENTITY   3
#define NXE_CEDAR_RVAL_SET      4
#define NXE_CEDAR_RVAL_ERROR    5
#define NXE_CEDAR_RVAL_IP       6
#define NXE_CEDAR_RVAL_RECORD   7


typedef struct {
    ngx_uint_t  type;       /* NXE_CEDAR_RVAL_* */
    union {
        ngx_str_t    str_val;
        int64_t      long_val;   /* Cedar i64 runtime value */
        ngx_flag_t   bool_val;
        struct {
            ngx_str_t  type;
            ngx_str_t  id;
        } entity;
        ngx_array_t *set_elts;     /* array of nxe_cedar_value_t */
        ngx_array_t *record_attrs; /* array of nxe_cedar_attr_t */
        struct {
            u_char      addr[16];    /* network byte order */
            ngx_uint_t  prefix_len;  /* /prefix; single=32(v4)/128(v6) */
            unsigned    is_ipv6   :1;
        } ip_addr;
    } v;
} nxe_cedar_value_t;


/*
 * Named runtime value used both as an entity attribute (principal /
 * action / resource / context) and as a record entry.
 */
typedef struct {
    ngx_str_t          name;
    nxe_cedar_value_t  value;
} nxe_cedar_attr_t;

/* evaluation context (built per-request) */
typedef struct {
    ngx_pool_t  *pool;

    /* principal */
    ngx_str_t    principal_type;
    ngx_str_t    principal_id;
    ngx_array_t *principal_attrs;            /* array of nxe_cedar_attr_t */

    /* action */
    ngx_str_t    action_type;
    ngx_str_t    action_id;
    ngx_array_t *action_attrs;              /* array of nxe_cedar_attr_t */

    /* resource */
    ngx_str_t    resource_type;
    ngx_str_t    resource_id;
    ngx_array_t *resource_attrs;             /* array of nxe_cedar_attr_t */

    /* context */
    ngx_array_t *context_attrs;              /* array of nxe_cedar_attr_t */
} nxe_cedar_eval_ctx_t;


#endif /* NXE_CEDAR_TYPES_H */
