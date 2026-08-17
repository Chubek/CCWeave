/* Cephyr semantic analysis — §5.
 *
 * Performs C type checking, integer promotions, usual arithmetic
 * conversions, and constant evaluation. Three phases:
 *   1. Declare: build symbol table
 *   2. Resolve types: resolve typedef/struct/union/enum references
 *   3. Check: type-check expressions and statements
 *
 * Excluded features (v0.1) produce CE0010 "not supported in this phase". */

#include "cephyr_sema.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "khash.h"

/* ---------- symbol table ---------- */

typedef struct symbol_entry {
    const char        *name;
    cephyr_ast_node   *node;   /* declaration node */
    cephyr_type       *type;   /* resolved type */
} symbol_entry;

KHASH_MAP_INIT_STR(cephyr_symbols, symbol_entry *)

typedef struct scope {
    khash_t(cephyr_symbols) *symbols;
    struct scope      *parent;
} scope;

struct cephyr_sema_ctx {
    cephyr_diagnostic  diagnostics[CEPHYR_MAX_DIAGNOSTICS];
    int                diag_count;
    int                error_count;
    scope             *current_scope;
};

static char *cephyr_strdup(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1u;
    char *copy = malloc(n);
    if (copy) memcpy(copy, s, n);
    return copy;
}

/* ---------- diagnostic helpers ---------- */

static void add_diag(cephyr_sema_ctx *ctx, cephyr_severity sev,
                     const char *id, const char *msg,
                     const cephyr_ast_node *node)
{
    if (ctx->diag_count >= CEPHYR_MAX_DIAGNOSTICS) return;
    cephyr_diagnostic *d = &ctx->diagnostics[ctx->diag_count++];
    d->severity = sev;
    d->id = id;
    d->message = msg;
    d->source_file = node ? node->source_file : NULL;
    d->source_line = node ? node->source_line : 0;
    d->source_column = node ? node->source_column : 0;
    if (sev == CEPHYR_SEV_ERROR || sev == CEPHYR_SEV_FATAL)
        ctx->error_count++;
}

/* ---------- scope management ---------- */

static scope *scope_create(scope *parent)
{
    scope *s = calloc(1, sizeof(scope));
    if (s) {
        s->symbols = kh_init(cephyr_symbols);
        s->parent = parent;
    }
    return s;
}

static void scope_free(scope *s)
{
    if (!s) return;
    if (s->symbols) {
        for (khint_t i = kh_begin(s->symbols);
             i != kh_end(s->symbols); ++i) {
            if (kh_exist(s->symbols, i))
                free(kh_val(s->symbols, i));
        }
        kh_destroy(cephyr_symbols, s->symbols);
    }
    free(s);
}

static void scope_insert(scope *s, const char *name, cephyr_ast_node *node, cephyr_type *type)
{
    symbol_entry *sym = calloc(1, sizeof(symbol_entry));
    if (!s || !s->symbols || !name || !sym) {
        free(sym);
        return;
    }
    sym->name = name;
    sym->node = node;
    sym->type = type;
    int ret = 0;
    khint_t k = kh_put(cephyr_symbols, s->symbols, name, &ret);
    if (k == kh_end(s->symbols)) {
        free(sym);
        return;
    }
    if (ret == 0)
        free(kh_val(s->symbols, k));
    kh_val(s->symbols, k) = sym;
}

static symbol_entry *scope_lookup(scope *s, const char *name)
{
    for (; s; s = s->parent) {
        if (!s->symbols || !name) continue;
        khint_t k = kh_get(cephyr_symbols, s->symbols, name);
        if (k != kh_end(s->symbols))
            return kh_val(s->symbols, k);
    }
    return NULL;
}

/* ---------- type utilities ---------- */

bool cephyr_type_is_integer(const cephyr_type *t)
{
    if (!t) return false;
    switch (t->kind) {
    case CEPHYR_TY_CHAR: case CEPHYR_TY_SCHAR: case CEPHYR_TY_UCHAR:
    case CEPHYR_TY_SHORT: case CEPHYR_TY_USHORT:
    case CEPHYR_TY_INT: case CEPHYR_TY_UINT:
    case CEPHYR_TY_LONG: case CEPHYR_TY_ULONG:
    case CEPHYR_TY_LONGLONG: case CEPHYR_TY_ULONGLONG:
    case CEPHYR_TY_BOOL:
    case CEPHYR_TY_ENUM:
        return true;
    default:
        return false;
    }
}

bool cephyr_type_is_float(const cephyr_type *t)
{
    if (!t) return false;
    return t->kind == CEPHYR_TY_FLOAT || t->kind == CEPHYR_TY_DOUBLE ||
           t->kind == CEPHYR_TY_LONGDOUBLE;
}

bool cephyr_type_is_arithmetic(const cephyr_type *t)
{
    return cephyr_type_is_integer(t) || cephyr_type_is_float(t);
}

bool cephyr_type_is_scalar(const cephyr_type *t)
{
    return cephyr_type_is_arithmetic(t) || (t && t->kind == CEPHYR_TY_PTR);
}

bool cephyr_type_compatible(const cephyr_type *a, const cephyr_type *b)
{
    if (!a || !b) return false;
    /* Same kind */
    if (a->kind != b->kind) {
        /* Integer types are compatible with each other */
        if (cephyr_type_is_integer(a) && cephyr_type_is_integer(b))
            return true;
        /* Float types are compatible with each other */
        if (cephyr_type_is_float(a) && cephyr_type_is_float(b))
            return true;
        /* Pointer to void is compatible with any pointer */
        if (a->kind == CEPHYR_TY_PTR && b->kind == CEPHYR_TY_PTR)
            return true;
        return false;
    }
    /* Pointer compatibility: same inner type or void* */
    if (a->kind == CEPHYR_TY_PTR) {
        if (a->inner && a->inner->kind == CEPHYR_TY_VOID) return true;
        if (b->inner && b->inner->kind == CEPHYR_TY_VOID) return true;
        return cephyr_type_compatible(a->inner, b->inner);
    }
    /* Array compatibility */
    if (a->kind == CEPHYR_TY_ARRAY)
        return cephyr_type_compatible(a->inner, b->inner);
    /* Struct/union: same name */
    if (a->kind == CEPHYR_TY_STRUCT || a->kind == CEPHYR_TY_UNION) {
        if (!a->name || !b->name) return false;
        return strcmp(a->name, b->name) == 0;
    }
    return true;
}

cephyr_type *cephyr_type_integer_promote(cephyr_type *t)
{
    if (!t) return NULL;
    if (!cephyr_type_is_integer(t)) return t;
    /* Integer promotion: types smaller than int promote to int */
    switch (t->kind) {
    case CEPHYR_TY_CHAR: case CEPHYR_TY_SCHAR: case CEPHYR_TY_UCHAR:
    case CEPHYR_TY_SHORT: case CEPHYR_TY_USHORT:
    case CEPHYR_TY_BOOL: {
        cephyr_type *promoted = calloc(1, sizeof(cephyr_type));
        promoted->kind = CEPHYR_TY_INT;
        return promoted;
    }
    default:
        return t;
    }
}

cephyr_type *cephyr_type_usual_arithmetic(cephyr_type *a, cephyr_type *b)
{
    if (!a || !b) return NULL;
    if (!cephyr_type_is_arithmetic(a) || !cephyr_type_is_arithmetic(b))
        return NULL;

    /* If either is long double, result is long double */
    if (a->kind == CEPHYR_TY_LONGDOUBLE || b->kind == CEPHYR_TY_LONGDOUBLE) {
        cephyr_type *r = calloc(1, sizeof(cephyr_type));
        r->kind = CEPHYR_TY_LONGDOUBLE;
        return r;
    }
    /* If either is double, result is double */
    if (a->kind == CEPHYR_TY_DOUBLE || b->kind == CEPHYR_TY_DOUBLE) {
        cephyr_type *r = calloc(1, sizeof(cephyr_type));
        r->kind = CEPHYR_TY_DOUBLE;
        return r;
    }
    /* If either is float, result is float */
    if (a->kind == CEPHYR_TY_FLOAT || b->kind == CEPHYR_TY_FLOAT) {
        cephyr_type *r = calloc(1, sizeof(cephyr_type));
        r->kind = CEPHYR_TY_FLOAT;
        return r;
    }
    /* Integer promotions */
    cephyr_type *pa = cephyr_type_integer_promote(a);
    cephyr_type *pb = cephyr_type_integer_promote(b);
    /* If same after promotion, return that type */
    if (pa->kind == pb->kind) return pa;
    /* Pick the larger type (simplified: prefer unsigned long long) */
    cephyr_type *r = calloc(1, sizeof(cephyr_type));
    r->kind = CEPHYR_TY_ULONGLONG;
    return r;
}

size_t cephyr_type_size(const cephyr_type *t)
{
    if (!t) return 0;
    switch (t->kind) {
    case CEPHYR_TY_VOID:    return 0;
    case CEPHYR_TY_BOOL:    return 1;
    case CEPHYR_TY_CHAR:    case CEPHYR_TY_SCHAR: case CEPHYR_TY_UCHAR:
        return 1;
    case CEPHYR_TY_SHORT:   case CEPHYR_TY_USHORT:  return 2;
    case CEPHYR_TY_INT:     case CEPHYR_TY_UINT:    return 4;
    case CEPHYR_TY_LONG:    case CEPHYR_TY_ULONG:   return 8;
    case CEPHYR_TY_LONGLONG: case CEPHYR_TY_ULONGLONG: return 8;
    case CEPHYR_TY_FLOAT:   return 4;
    case CEPHYR_TY_DOUBLE:  return 8;
    case CEPHYR_TY_LONGDOUBLE: return 16;
    case CEPHYR_TY_PTR:     return 8;
    case CEPHYR_TY_ENUM:    return 4;
    case CEPHYR_TY_ARRAY: {
        if (t->inner) {
            size_t es = cephyr_type_size(t->inner);
            return es * (t->array_size > 0 ? (size_t)t->array_size : 0);
        }
        return 0;
    }
    case CEPHYR_TY_STRUCT: case CEPHYR_TY_UNION: {
        /* Sum fields (struct) or max field (union), simplified to 8-aligned sum */
        size_t total = 0;
        for (int i = 0; i < t->field_count; i++) {
            size_t fs = cephyr_type_size(t->field_types[i]);
            if (t->kind == CEPHYR_TY_UNION) {
                if (fs > total) total = fs;
            } else {
                /* Simple alignment: align to field alignment */
                size_t align = cephyr_type_align(t->field_types[i]);
                total = (total + align - 1) & ~(align - 1);
                total += fs;
            }
        }
        return total > 0 ? total : 1;
    }
    case CEPHYR_TY_FUNC:    return 8; /* function pointer */
    case CEPHYR_TY_TYPEDEF: return t->inner ? cephyr_type_size(t->inner) : 0;
    default: return 0;
    }
}

size_t cephyr_type_align(const cephyr_type *t)
{
    if (!t) return 1;
    if (t->kind == CEPHYR_TY_ARRAY)
        return cephyr_type_align(t->inner);
    if (t->kind == CEPHYR_TY_STRUCT || t->kind == CEPHYR_TY_UNION) {
        size_t alignment = 1;
        for (int i = 0; i < t->field_count; i++) {
            size_t field_alignment = cephyr_type_align(t->field_types[i]);
            if (field_alignment > alignment) alignment = field_alignment;
        }
        return alignment;
    }
    size_t sz = cephyr_type_size(t);
    /* Alignment is the size, capped at 8 */
    if (sz > 8) return 8;
    if (sz == 0) return 1;
    return sz;
}

cephyr_type *cephyr_type_dup(const cephyr_type *t)
{
    if (!t) return NULL;
    cephyr_type *copy = calloc(1, sizeof(cephyr_type));
    memcpy(copy, t, sizeof(cephyr_type));
    if (t->inner) copy->inner = cephyr_type_dup(t->inner);
    if (t->return_type) copy->return_type = cephyr_type_dup(t->return_type);
    if (t->name) copy->name = cephyr_strdup(t->name);
    /* Deep copy arrays */
    if (t->field_count > 0) {
        copy->field_names = calloc((size_t)t->field_count, sizeof(char *));
        copy->field_types = calloc((size_t)t->field_count, sizeof(cephyr_type *));
        copy->field_bit_widths = calloc((size_t)t->field_count, sizeof(int));
        for (int i = 0; i < t->field_count; i++) {
            if (t->field_names[i]) copy->field_names[i] = cephyr_strdup(t->field_names[i]);
            copy->field_types[i] = cephyr_type_dup(t->field_types[i]);
            copy->field_bit_widths[i] = t->field_bit_widths[i];
        }
    }
    if (t->param_count > 0) {
        copy->param_types = calloc((size_t)t->param_count, sizeof(cephyr_type *));
        copy->param_names = calloc((size_t)t->param_count, sizeof(char *));
        for (int i = 0; i < t->param_count; i++) {
            copy->param_types[i] = cephyr_type_dup(t->param_types[i]);
            if (t->param_names[i]) copy->param_names[i] = cephyr_strdup(t->param_names[i]);
        }
    }
    return copy;
}

void cephyr_type_free(cephyr_type *t)
{
    if (!t) return;
    free((void *)t->name);
    cephyr_type_free(t->inner);
    cephyr_type_free(t->return_type);
    for (int i = 0; i < t->field_count; i++) {
        free((void *)t->field_names[i]);
        cephyr_type_free(t->field_types[i]);
    }
    free((void *)t->field_names);
    free(t->field_types);
    free(t->field_bit_widths);
    for (int i = 0; i < t->param_count; i++) {
        free((void *)t->param_names[i]);
        cephyr_type_free(t->param_types[i]);
    }
    free(t->param_types);
    free((void *)t->param_names);
    free(t);
}

/* ---------- sema context ---------- */

cephyr_sema_ctx *cephyr_sema_create(void)
{
    cephyr_sema_ctx *ctx = calloc(1, sizeof(cephyr_sema_ctx));
    ctx->current_scope = scope_create(NULL);
    return ctx;
}

void cephyr_sema_destroy(cephyr_sema_ctx *ctx)
{
    if (!ctx) return;
    scope_free(ctx->current_scope);
    free(ctx);
}

/* ---------- phase 1: declare ---------- */

static void declare_node(cephyr_sema_ctx *ctx, cephyr_ast_node *node);

static void declare_func(cephyr_sema_ctx *ctx, cephyr_ast_node *node)
{
    if (!node->name) return;
    symbol_entry *existing = scope_lookup(ctx->current_scope, node->name);
    if (existing && existing->node->kind == CEPHYR_NODE_FUNC_DECL) {
        /* Redeclaration — check compatibility */
        if (node->kind == CEPHYR_NODE_FUNC_DEF) {
            /* Replace declaration with definition */
            existing->node = node;
        }
        return;
    }
    if (existing) {
        add_diag(ctx, CEPHYR_SEV_ERROR, CEPHYR_E0004,
                 "redefinition of symbol", node);
        return;
    }
    scope_insert(ctx->current_scope, node->name, node, node->type);
}

static void declare_var(cephyr_sema_ctx *ctx, cephyr_ast_node *node)
{
    if (!node->name) return;
    symbol_entry *existing = scope_lookup(ctx->current_scope, node->name);
    if (existing) {
        add_diag(ctx, CEPHYR_SEV_ERROR, CEPHYR_E0004,
                 "redefinition of symbol", node);
        return;
    }
    scope_insert(ctx->current_scope, node->name, node, node->type);
}

static void declare_node(cephyr_sema_ctx *ctx, cephyr_ast_node *node)
{
    if (!node) return;
    switch (node->kind) {
    case CEPHYR_NODE_FUNC_DECL:
    case CEPHYR_NODE_FUNC_DEF:
        declare_func(ctx, node);
        break;
    case CEPHYR_NODE_VAR_DECL:
        declare_var(ctx, node);
        break;
    case CEPHYR_NODE_STRUCT_DECL:
    case CEPHYR_NODE_UNION_DECL:
    case CEPHYR_NODE_ENUM_DECL:
    case CEPHYR_NODE_TYPEDEF_DECL:
        if (node->name) {
            scope_insert(ctx->current_scope, node->name, node, node->type);
        }
        break;
    case CEPHYR_NODE_PROGRAM:
        /* Walk children */
        for (cephyr_ast_node *child = node; child; child = child->next) {
            if (child != node) declare_node(ctx, child);
        }
        break;
    default:
        break;
    }
}

int cephyr_sema_declare(cephyr_sema_ctx *ctx, cephyr_ast_node *program)
{
    ctx->error_count = 0;
    ctx->diag_count = 0;

    /* Walk the program's children (top-level declarations) */
    cephyr_ast_node *child = program;
    if (child && child->kind == CEPHYR_NODE_PROGRAM) {
        child = child->data.block.stmts ? child->data.block.stmts[0] : NULL;
    }
    for (; child; child = child->next) {
        declare_node(ctx, child);
    }
    return ctx->error_count;
}

/* ---------- phase 2: resolve types ---------- */

static void resolve_node_type(cephyr_sema_ctx *ctx, cephyr_ast_node *node);

static cephyr_type *resolve_type_ref(cephyr_sema_ctx *ctx, cephyr_type *t)
{
    if (!t) return NULL;
    if (t->kind == CEPHYR_TY_TYPEDEF && t->name) {
        symbol_entry *sym = scope_lookup(ctx->current_scope, t->name);
        if (sym && sym->type) {
            return cephyr_type_dup(sym->type);
        }
    }
    if (t->kind == CEPHYR_TY_STRUCT && t->name) {
        symbol_entry *sym = scope_lookup(ctx->current_scope, t->name);
        if (sym && sym->type && sym->type->kind == CEPHYR_TY_STRUCT) {
            return cephyr_type_dup(sym->type);
        }
    }
    if (t->kind == CEPHYR_TY_UNION && t->name) {
        symbol_entry *sym = scope_lookup(ctx->current_scope, t->name);
        if (sym && sym->type && sym->type->kind == CEPHYR_TY_UNION) {
            return cephyr_type_dup(sym->type);
        }
    }
    return cephyr_type_dup(t);
}

static void resolve_node_type(cephyr_sema_ctx *ctx, cephyr_ast_node *node)
{
    if (!node) return;
    /* Resolve the node's type */
    if (node->type) {
        cephyr_type *resolved = resolve_type_ref(ctx, node->type);
        cephyr_type_free(node->type);
        node->type = resolved;
    }
}

int cephyr_sema_resolve_types(cephyr_sema_ctx *ctx, cephyr_ast_node *program)
{
    ctx->error_count = 0;
    ctx->diag_count = 0;
    /* Walk all nodes and resolve types */
    cephyr_ast_node *child = program;
    if (child && child->kind == CEPHYR_NODE_PROGRAM) {
        child = child->data.block.stmts ? child->data.block.stmts[0] : NULL;
    }
    for (; child; child = child->next) {
        resolve_node_type(ctx, child);
    }
    return ctx->error_count;
}

/* ---------- phase 3: check ---------- */

static void check_node(cephyr_sema_ctx *ctx, cephyr_ast_node *node);

static void check_expr(cephyr_sema_ctx *ctx, cephyr_ast_node *node)
{
    if (!node) return;
    switch (node->kind) {
    case CEPHYR_NODE_EXPR_BINARY:
        check_expr(ctx, node->data.binop.lhs);
        check_expr(ctx, node->data.binop.rhs);
        if (node->data.binop.lhs && node->data.binop.rhs) {
            cephyr_type *common = cephyr_type_usual_arithmetic(
                node->data.binop.lhs->type, node->data.binop.rhs->type);
            if (common) {
                node->type = common;
            } else {
                add_diag(ctx, CEPHYR_SEV_ERROR, CEPHYR_E0002,
                         "incompatible operand types for binary operator", node);
            }
        }
        break;
    case CEPHYR_NODE_EXPR_UNARY:
        check_expr(ctx, node->data.unop.operand);
        if (node->data.unop.operand && node->data.unop.operand->type) {
            node->type = cephyr_type_dup(node->data.unop.operand->type);
        }
        break;
    case CEPHYR_NODE_EXPR_CALL:
        check_expr(ctx, node->data.call.callee);
        for (int i = 0; i < node->data.call.arg_count; i++)
            check_expr(ctx, node->data.call.args[i]);
        /* Result type is the function's return type */
        if (node->data.call.callee && node->data.call.callee->type) {
            cephyr_type *ft = node->data.call.callee->type;
            if (ft->kind == CEPHYR_TY_FUNC && ft->return_type) {
                node->type = cephyr_type_dup(ft->return_type);
            } else if (ft->kind == CEPHYR_TY_PTR && ft->inner &&
                       ft->inner->kind == CEPHYR_TY_FUNC) {
                node->type = cephyr_type_dup(ft->inner->return_type);
            } else {
                node->type = calloc(1, sizeof(cephyr_type));
                node->type->kind = CEPHYR_TY_INT;
            }
        }
        break;
    case CEPHYR_NODE_EXPR_CAST:
        check_expr(ctx, node->data.cast.expr);
        if (node->data.cast.target_type) {
            node->type = cephyr_type_dup(node->data.cast.target_type);
        }
        break;
    case CEPHYR_NODE_EXPR_ASSIGN:
        check_expr(ctx, node->data.binop.lhs);
        check_expr(ctx, node->data.binop.rhs);
        if (node->data.binop.lhs && node->data.binop.lhs->type) {
            node->type = cephyr_type_dup(node->data.binop.lhs->type);
        }
        break;
    case CEPHYR_NODE_EXPR_CONDITIONAL:
        check_expr(ctx, node->data.if_stmt.cond);
        check_expr(ctx, node->data.if_stmt.then_branch);
        check_expr(ctx, node->data.if_stmt.else_branch);
        if (node->data.if_stmt.then_branch && node->data.if_stmt.then_branch->type) {
            node->type = cephyr_type_dup(node->data.if_stmt.then_branch->type);
        }
        break;
    case CEPHYR_NODE_EXPR_MEMBER:
        check_expr(ctx, node->data.member.object);
        /* Type comes from the struct field */
        if (node->data.member.object && node->data.member.object->type) {
            cephyr_type *st = node->data.member.object->type;
            if (st->kind == CEPHYR_TY_STRUCT || st->kind == CEPHYR_TY_UNION) {
                for (int i = 0; i < st->field_count; i++) {
                    if (st->field_names[i] &&
                        strcmp(st->field_names[i], node->data.member.member) == 0) {
                        node->type = cephyr_type_dup(st->field_types[i]);
                        break;
                    }
                }
            }
        }
        if (!node->type) {
            node->type = calloc(1, sizeof(cephyr_type));
            node->type->kind = CEPHYR_TY_INT;
        }
        break;
    case CEPHYR_NODE_EXPR_ARRAY:
        check_expr(ctx, node->data.array_access.array);
        check_expr(ctx, node->data.array_access.index);
        if (node->data.array_access.array && node->data.array_access.array->type) {
            cephyr_type *at = node->data.array_access.array->type;
            if (at->kind == CEPHYR_TY_ARRAY && at->inner) {
                node->type = cephyr_type_dup(at->inner);
            } else if (at->kind == CEPHYR_TY_PTR && at->inner) {
                node->type = cephyr_type_dup(at->inner);
            } else {
                node->type = calloc(1, sizeof(cephyr_type));
                node->type->kind = CEPHYR_TY_INT;
            }
        }
        break;
    case CEPHYR_NODE_EXPR_SIZEOF:
        node->type = calloc(1, sizeof(cephyr_type));
        node->type->kind = CEPHYR_TY_ULONG;
        break;
    default:
        break;
    }
}

static void check_stmt(cephyr_sema_ctx *ctx, cephyr_ast_node *node)
{
    if (!node) return;
    switch (node->kind) {
    case CEPHYR_NODE_BLOCK:
        if (node->data.block.stmts) {
            for (int i = 0; i < node->data.block.stmt_count; i++)
                check_node(ctx, node->data.block.stmts[i]);
        }
        break;
    case CEPHYR_NODE_STMT_RETURN:
        /* Check return expression vs function return type */
        check_expr(ctx, node->data.unop.operand);
        break;
    case CEPHYR_NODE_STMT_IF:
        check_expr(ctx, node->data.if_stmt.cond);
        check_node(ctx, node->data.if_stmt.then_branch);
        check_node(ctx, node->data.if_stmt.else_branch);
        break;
    case CEPHYR_NODE_STMT_WHILE:
        check_expr(ctx, node->data.while_stmt.cond);
        check_node(ctx, node->data.while_stmt.body);
        break;
    case CEPHYR_NODE_STMT_FOR:
        check_node(ctx, node->data.for_stmt.init);
        check_expr(ctx, node->data.for_stmt.cond);
        check_expr(ctx, node->data.for_stmt.incr);
        check_node(ctx, node->data.for_stmt.body);
        break;
    case CEPHYR_NODE_STMT_EXPR:
        check_expr(ctx, node->data.unop.operand);
        break;
    case CEPHYR_NODE_STMT_DECL:
        break;
    case CEPHYR_NODE_STMT_SWITCH:
        check_expr(ctx, node->data.switch_stmt.expr);
        check_node(ctx, node->data.switch_stmt.body);
        break;
    default:
        break;
    }
}

static void check_node(cephyr_sema_ctx *ctx, cephyr_ast_node *node)
{
    if (!node) return;
    /* Classify node as expression or statement */
    switch (node->kind) {
    case CEPHYR_NODE_EXPR_BINARY:
    case CEPHYR_NODE_EXPR_UNARY:
    case CEPHYR_NODE_EXPR_CALL:
    case CEPHYR_NODE_EXPR_CAST:
    case CEPHYR_NODE_EXPR_IDENT:
    case CEPHYR_NODE_EXPR_INT_CONST:
    case CEPHYR_NODE_EXPR_FLOAT_CONST:
    case CEPHYR_NODE_EXPR_STRING_CONST:
    case CEPHYR_NODE_EXPR_CHAR_CONST:
    case CEPHYR_NODE_EXPR_ASSIGN:
    case CEPHYR_NODE_EXPR_CONDITIONAL:
    case CEPHYR_NODE_EXPR_MEMBER:
    case CEPHYR_NODE_EXPR_ARRAY:
    case CEPHYR_NODE_EXPR_SIZEOF:
    case CEPHYR_NODE_EXPR_COMPOUND_LITERAL:
    case CEPHYR_NODE_EXPR_INIT_LIST:
    case CEPHYR_NODE_EXPR_DESIGNATED_INIT:
        check_expr(ctx, node);
        break;
    case CEPHYR_NODE_FUNC_DEF:
        /* Enter function scope */
        {
            scope *func_scope = scope_create(ctx->current_scope);
            scope *old = ctx->current_scope;
            ctx->current_scope = func_scope;
            /* Add parameters to scope */
            if (node->type && node->type->param_count > 0) {
                for (int i = 0; i < node->type->param_count; i++) {
                    if (node->type->param_names && node->type->param_names[i]) {
                        scope_insert(func_scope, node->type->param_names[i], NULL,
                                     node->type->param_types[i]);
                    }
                }
            }
            check_node(ctx, node->data.func_def.body);
            ctx->current_scope = old;
            scope_free(func_scope);
        }
        break;
    default:
        check_stmt(ctx, node);
        break;
    }
}

int cephyr_sema_check(cephyr_sema_ctx *ctx, cephyr_ast_node *program)
{
    ctx->error_count = 0;
    ctx->diag_count = 0;
    check_node(ctx, program);
    return ctx->error_count;
}

int cephyr_sema_run(cephyr_sema_ctx *ctx, cephyr_ast_node *program)
{
    int errors = cephyr_sema_declare(ctx, program);
    if (errors > 0) return errors;
    errors += cephyr_sema_resolve_types(ctx, program);
    if (errors > 0) return errors;
    errors += cephyr_sema_check(ctx, program);
    return errors;
}

/* ---------- diagnostic access ---------- */

int cephyr_sema_diagnostic_count(const cephyr_sema_ctx *ctx)
{
    return ctx ? ctx->diag_count : 0;
}

const cephyr_diagnostic *cephyr_sema_diagnostic_ref(const cephyr_sema_ctx *ctx, int idx)
{
    if (!ctx || idx < 0 || idx >= ctx->diag_count) return NULL;
    return &ctx->diagnostics[idx];
}

void cephyr_sema_diagnostic_emit(FILE *out, const cephyr_diagnostic *diag)
{
    if (!out || !diag) return;
    const char *sev_str = "note";
    switch (diag->severity) {
    case CEPHYR_SEV_WARNING: sev_str = "warning"; break;
    case CEPHYR_SEV_ERROR:   sev_str = "error"; break;
    case CEPHYR_SEV_FATAL:   sev_str = "fatal error"; break;
    default: break;
    }
    if (diag->source_file && diag->source_line > 0) {
        fprintf(out, "%s:%d:%d: %s: %s [%s]\n",
                diag->source_file, diag->source_line, diag->source_column,
                sev_str, diag->message, diag->id);
    } else {
        fprintf(out, "%s: %s [%s]\n", sev_str, diag->message, diag->id);
    }
}

int cephyr_sema_error_count(const cephyr_sema_ctx *ctx)
{
    return ctx ? ctx->error_count : 0;
}
