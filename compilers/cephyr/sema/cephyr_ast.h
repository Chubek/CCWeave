/* Cephyr typed AST — §5.
 *
 * Semantic analysis produces a typed AST from the Swaff C adapter
 * output. This AST carries C-specific type information (structs,
 * unions, enums, pointers, arrays, function types) and is the input
 * to lowering. */

#ifndef CEPHYR_AST_H
#define CEPHYR_AST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- C type system ---------- */

typedef enum {
    CEPHYR_TY_VOID = 0,
    CEPHYR_TY_CHAR,
    CEPHYR_TY_SCHAR,
    CEPHYR_TY_UCHAR,
    CEPHYR_TY_SHORT,
    CEPHYR_TY_USHORT,
    CEPHYR_TY_INT,
    CEPHYR_TY_UINT,
    CEPHYR_TY_LONG,
    CEPHYR_TY_ULONG,
    CEPHYR_TY_LONGLONG,
    CEPHYR_TY_ULONGLONG,
    CEPHYR_TY_FLOAT,
    CEPHYR_TY_DOUBLE,
    CEPHYR_TY_LONGDOUBLE,
    CEPHYR_TY_BOOL,
    CEPHYR_TY_PTR,        /* pointer to another type */
    CEPHYR_TY_ARRAY,      /* array of another type */
    CEPHYR_TY_FUNC,       /* function type */
    CEPHYR_TY_STRUCT,     /* struct (named) */
    CEPHYR_TY_UNION,      /* union (named) */
    CEPHYR_TY_ENUM,       /* enum (named) */
    CEPHYR_TY_TYPEDEF     /* typedef alias */
} cephyr_type_kind;

typedef struct cephyr_type cephyr_type;

struct cephyr_type {
    cephyr_type_kind kind;
    const char       *name;       /* for struct/union/enum/typedef */
    cephyr_type      *inner;      /* for PTR, ARRAY, FUNC */
    int64_t           array_size; /* for ARRAY (0 = unsized) */
    int               is_const;
    int               is_volatile;
    int               is_restrict;
    /* struct/union fields */
    int               field_count;
    const char       **field_names;
    cephyr_type      **field_types;
    int               *field_bit_widths; /* -1 = no bit-field */
    /* enum constants */
    int               enum_value_count;
    const char       **enum_value_names;
    int64_t           *enum_values;
    /* function params */
    int               param_count;
    cephyr_type      **param_types;
    const char       **param_names;
    cephyr_type       *return_type;
    int               is_variadic;
};

/* ---------- AST node kinds ---------- */

typedef enum {
    CEPHYR_NODE_PROGRAM = 0,
    CEPHYR_NODE_FUNC_DECL,
    CEPHYR_NODE_FUNC_DEF,
    CEPHYR_NODE_VAR_DECL,
    CEPHYR_NODE_STRUCT_DECL,
    CEPHYR_NODE_UNION_DECL,
    CEPHYR_NODE_ENUM_DECL,
    CEPHYR_NODE_TYPEDEF_DECL,
    CEPHYR_NODE_BLOCK,
    CEPHYR_NODE_STMT_RETURN,
    CEPHYR_NODE_STMT_IF,
    CEPHYR_NODE_STMT_WHILE,
    CEPHYR_NODE_STMT_DO_WHILE,
    CEPHYR_NODE_STMT_FOR,
    CEPHYR_NODE_STMT_SWITCH,
    CEPHYR_NODE_STMT_BREAK,
    CEPHYR_NODE_STMT_CONTINUE,
    CEPHYR_NODE_STMT_EXPR,
    CEPHYR_NODE_STMT_DECL,
    CEPHYR_NODE_EXPR_BINARY,
    CEPHYR_NODE_EXPR_UNARY,
    CEPHYR_NODE_EXPR_CALL,
    CEPHYR_NODE_EXPR_CAST,
    CEPHYR_NODE_EXPR_IDENT,
    CEPHYR_NODE_EXPR_INT_CONST,
    CEPHYR_NODE_EXPR_FLOAT_CONST,
    CEPHYR_NODE_EXPR_STRING_CONST,
    CEPHYR_NODE_EXPR_CHAR_CONST,
    CEPHYR_NODE_EXPR_ASSIGN,
    CEPHYR_NODE_EXPR_CONDITIONAL,
    CEPHYR_NODE_EXPR_MEMBER,
    CEPHYR_NODE_EXPR_ARRAY,
    CEPHYR_NODE_EXPR_SIZEOF,
    CEPHYR_NODE_EXPR_COMPOUND_LITERAL,
    CEPHYR_NODE_EXPR_INIT_LIST,
    CEPHYR_NODE_EXPR_DESIGNATED_INIT,
    CEPHYR_NODE_LABEL,
    CEPHYR_NODE_GOTO,
    CEPHYR_NODE_CASE,
    CEPHYR_NODE_DEFAULT
} cephyr_ast_node_kind;

/* ---------- AST node ---------- */

typedef struct cephyr_ast_node cephyr_ast_node;

struct cephyr_ast_node {
    cephyr_ast_node_kind kind;
    const char           *name;         /* identifier name (if any) */
    cephyr_type          *type;         /* resolved type */
    const char           *source_file;  /* original source location */
    int                   source_line;
    int                   source_column;

    /* payload (union-like, but C11 style) */
    union {
        /* for binary/unary */
        struct { const char *op; struct cephyr_ast_node *lhs; struct cephyr_ast_node *rhs; } binop;
        struct { const char *op; struct cephyr_ast_node *operand; } unop;
        /* for call */
        struct { struct cephyr_ast_node *callee; int arg_count; struct cephyr_ast_node **args; } call;
        /* for cast */
        struct { cephyr_type *target_type; struct cephyr_ast_node *expr; } cast;
        /* for constants */
        int64_t   int_value;
        double    float_value;
        char     *string_value;
        int       char_value;
        /* for func decl/def */
        struct { cephyr_type *ret_type; int param_count; cephyr_type **params; const char **param_names; int is_variadic; } func_type;
        /* for func def */
        struct { struct cephyr_ast_node *body; int is_static; int is_inline; } func_def;
        /* for struct/union */
        struct { int field_count; struct cephyr_ast_node **fields; } record;
        /* for enum */
        struct { int value_count; const char **names; int64_t *values; } enum_def;
        /* for block */
        struct { int stmt_count; struct cephyr_ast_node **stmts; } block;
        /* for if/while/for */
        struct { struct cephyr_ast_node *cond; struct cephyr_ast_node *then_branch; struct cephyr_ast_node *else_branch; } if_stmt;
        struct { struct cephyr_ast_node *cond; struct cephyr_ast_node *body; } while_stmt;
        struct { struct cephyr_ast_node *init; struct cephyr_ast_node *cond; struct cephyr_ast_node *incr; struct cephyr_ast_node *body; } for_stmt;
        /* for switch */
        struct { struct cephyr_ast_node *expr; struct cephyr_ast_node *body; } switch_stmt;
        /* for member access */
        struct { struct cephyr_ast_node *object; const char *member; int is_arrow; } member;
        /* for array access */
        struct { struct cephyr_ast_node *array; struct cephyr_ast_node *index; } array_access;
        /* for sizeof */
        struct { int is_type; cephyr_type *sizeof_type; struct cephyr_ast_node *sizeof_expr; } sizeof_expr;
        /* for init list */
        struct { int init_count; struct cephyr_ast_node **inits; } init_list;
        /* for designated init */
        struct { const char *designator; struct cephyr_ast_node *init; } designated_init;
        /* for compound literal */
        struct { cephyr_type *lit_type; struct cephyr_ast_node *init; } compound_literal;
        /* for var decl */
        struct { cephyr_type *var_type; struct cephyr_ast_node *init; int is_static; int is_extern; } var_decl;
    } data;

    /* linked list (siblings in a block/scope) */
    cephyr_ast_node *next;
    /* attributes */
    int               attr_count;
    const char       **attr_names;
    const char       ***attr_args;
};

/* ---------- AST construction ---------- */

cephyr_ast_node *cephyr_ast_node_alloc(cephyr_ast_node_kind kind);
void             cephyr_ast_node_free(cephyr_ast_node *node);
void             cephyr_ast_free_recursive(cephyr_ast_node *node);
void             cephyr_type_free(cephyr_type *t);

/* Dump the typed AST to a file (for debugging/golden tests). */
void cephyr_ast_dump(FILE *out, const cephyr_ast_node *root);

#ifdef __cplusplus
}
#endif
#endif /* CEPHYR_AST_H */
