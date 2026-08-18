/* §2.3: deferred expression evaluation engine.
 * Expression-valued fields hold AST nodes; evaluation happens during
 * layout in plan order against a live location counter. */
#include "ccwld_expr.h"
#include "../plan/ccwld_plan.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

/* --- constructors --- */

ccwld_expr *ccwld_expr_int(uint64_t value) {
    ccwld_expr *e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->kind = CCWLD_EXPR_INT;
    e->ival = value;
    return e;
}

ccwld_expr *ccwld_expr_symbol(const char *name) {
    ccwld_expr *e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->kind = CCWLD_EXPR_SYMBOL;
    e->name = name ? strdup(name) : NULL;
    return e;
}

ccwld_expr *ccwld_expr_dot(void) {
    ccwld_expr *e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->kind = CCWLD_EXPR_DOT;
    return e;
}

ccwld_expr *ccwld_expr_binary(ccwld_op_tag op, ccwld_expr *a, ccwld_expr *b) {
    ccwld_expr *e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->kind = CCWLD_EXPR_BINARY;
    e->op = op;
    e->a = a;
    e->b = b;
    return e;
}

ccwld_expr *ccwld_expr_unary(ccwld_op_tag op, ccwld_expr *a) {
    ccwld_expr *e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->kind = CCWLD_EXPR_UNARY;
    e->op = op;
    e->a = a;
    return e;
}

ccwld_expr *ccwld_expr_align(ccwld_expr *a, uint64_t boundary) {
    ccwld_expr *e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->kind = CCWLD_EXPR_ALIGN;
    e->a = a;
    e->ival = boundary;
    return e;
}

ccwld_expr *ccwld_expr_max(ccwld_expr *a, ccwld_expr *b) {
    ccwld_expr *e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->kind = CCWLD_EXPR_MAX;
    e->a = a;
    e->b = b;
    return e;
}

ccwld_expr *ccwld_expr_min(ccwld_expr *a, ccwld_expr *b) {
    ccwld_expr *e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->kind = CCWLD_EXPR_MIN;
    e->a = a;
    e->b = b;
    return e;
}

ccwld_expr *ccwld_expr_cond(ccwld_expr *test, ccwld_expr *then_e, ccwld_expr *else_e) {
    ccwld_expr *e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->kind = CCWLD_EXPR_COND;
    e->a = test;
    e->b = then_e;
    e->c = else_e;
    return e;
}

ccwld_expr *ccwld_expr_defined(const char *symbol_name) {
    ccwld_expr *e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->kind = CCWLD_EXPR_DEFINED;
    e->name = symbol_name ? strdup(symbol_name) : NULL;
    return e;
}

ccwld_expr *ccwld_expr_region_origin(const char *region_name) {
    ccwld_expr *e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->kind = CCWLD_EXPR_REGION_ORIGIN;
    e->name = region_name ? strdup(region_name) : NULL;
    return e;
}

ccwld_expr *ccwld_expr_region_length(const char *region_name) {
    ccwld_expr *e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->kind = CCWLD_EXPR_REGION_LENGTH;
    e->name = region_name ? strdup(region_name) : NULL;
    return e;
}

ccwld_expr *ccwld_expr_sizeof(const char *section_name) {
    ccwld_expr *e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->kind = CCWLD_EXPR_SIZEOF;
    e->name = section_name ? strdup(section_name) : NULL;
    return e;
}

ccwld_expr *ccwld_expr_addr(const char *section_name) {
    ccwld_expr *e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->kind = CCWLD_EXPR_ADDR;
    e->name = section_name ? strdup(section_name) : NULL;
    return e;
}

ccwld_expr *ccwld_expr_loadaddr(const char *section_name) {
    ccwld_expr *e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->kind = CCWLD_EXPR_LOADADDR;
    e->name = section_name ? strdup(section_name) : NULL;
    return e;
}

ccwld_expr *ccwld_expr_sizeof_headers(void) {
    ccwld_expr *e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->kind = CCWLD_EXPR_SIZEOF_HEADERS;
    return e;
}

ccwld_expr *ccwld_expr_segment_start(const char *segment_name) {
    ccwld_expr *e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->kind = CCWLD_EXPR_SEGMENT_START;
    e->name = segment_name ? strdup(segment_name) : NULL;
    return e;
}

/* --- deep copy --- */

ccwld_expr *ccwld_expr_clone(const ccwld_expr *e) {
    if (!e) return NULL;
    ccwld_expr *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->kind = e->kind;
    c->op   = e->op;
    c->ival = e->ival;
    c->name = e->name ? strdup(e->name) : NULL;
    c->a = ccwld_expr_clone(e->a);
    c->b = ccwld_expr_clone(e->b);
    c->c = ccwld_expr_clone(e->c);
    c->visited = 0;
    return c;
}

/* --- free --- */

void ccwld_expr_free(ccwld_expr *e) {
    if (!e) return;
    ccwld_expr_free(e->a);
    ccwld_expr_free(e->b);
    ccwld_expr_free(e->c);
    free(e->name);
    free(e);
}

/* --- reset visited flags --- */

void ccwld_expr_reset_visited(ccwld_expr *e) {
    if (!e) return;
    e->visited = 0;
    ccwld_expr_reset_visited(e->a);
    ccwld_expr_reset_visited(e->b);
    ccwld_expr_reset_visited(e->c);
}

/* --- structure equality --- */

int ccwld_expr_equal(const ccwld_expr *a, const ccwld_expr *b) {
    if (!a && !b) return 1;
    if (!a || !b) return 0;
    if (a->kind != b->kind) return 0;
    if (a->op != b->op) return 0;
    if (a->kind == CCWLD_EXPR_INT) return a->ival == b->ival;
    if (a->name || b->name) {
        if (!a->name || !b->name) return 0;
        if (strcmp(a->name, b->name) != 0) return 0;
    }
    if (!ccwld_expr_equal(a->a, b->a)) return 0;
    if (!ccwld_expr_equal(a->b, b->b)) return 0;
    if (!ccwld_expr_equal(a->c, b->c)) return 0;
    return 1;
}

/* --- helper: look up a symbol in the plan --- */

static const struct ccwld_sym_rec *plan_find_symbol(const struct ccwld_plan *p,
                                                     const char *name) {
    if (!p || !name) return NULL;
    for (size_t i = 0; i < p->nsyms; i++) {
        if (p->syms[i].name && strcmp(p->syms[i].name, name) == 0)
            return &p->syms[i];
    }
    return NULL;
}

static const struct ccwld_mem_rec *plan_find_region(const struct ccwld_plan *p,
                                                     const char *name) {
    if (!p || !name) return NULL;
    for (size_t i = 0; i < p->nmems; i++) {
        if (p->mems[i].name && strcmp(p->mems[i].name, name) == 0)
            return &p->mems[i];
    }
    return NULL;
}

static const struct ccwld_sec_rec *plan_find_section(const struct ccwld_plan *p,
                                                      const char *name) {
    if (!p || !name) return NULL;
    for (size_t i = 0; i < p->nsecs; i++) {
        if (p->secs[i].name && strcmp(p->secs[i].name, name) == 0)
            return &p->secs[i];
    }
    return NULL;
}

/* --- evaluation --- */

static int eval_inner(const ccwld_expr *e,
                      const struct ccwld_plan *plan,
                      uint64_t dot,
                      uint64_t *out,
                      char **error_message);

static int eval_inner(const ccwld_expr *e,
                      const struct ccwld_plan *plan,
                      uint64_t dot,
                      uint64_t *out,
                      char **error_message)
{
    if (!e) {
        if (error_message) {
            char buf[256];
            snprintf(buf, sizeof(buf), "null expression node");
            *error_message = strdup(buf);
        }
        return 0;
    }

    /* Cycle detection: if visited, we have a cyclic dependency */
    if (e->visited) {
        if (error_message) {
            char buf[256];
            snprintf(buf, sizeof(buf), "cyclic dependency in expression");
            *error_message = strdup(buf);
        }
        return 0;
    }
    /* Mark visited (we are casting away const because this is internal
     * evaluation state; the caller resets it after each layout pass). */
    ((ccwld_expr *)e)->visited = 1;

    uint64_t a_val = 0, b_val = 0, c_val = 0;
    int ok = 1;

    switch (e->kind) {
    case CCWLD_EXPR_INT:
        *out = e->ival;
        ok = 1;
        break;

    case CCWLD_EXPR_DOT:
        *out = dot;
        ok = 1;
        break;

    case CCWLD_EXPR_SYMBOL: {
        const struct ccwld_sym_rec *sym = plan_find_symbol(plan, e->name);
        if (!sym) {
            if (error_message) {
                char buf[256];
                snprintf(buf, sizeof(buf), "undefined symbol '%s'", e->name);
                *error_message = strdup(buf);
            }
            ok = 0;
        } else {
            ok = eval_inner(sym->expr, plan, dot, out, error_message);
        }
        break;
    }

    case CCWLD_EXPR_DEFINED: {
        const struct ccwld_sym_rec *sym = plan_find_symbol(plan, e->name);
        *out = sym ? 1 : 0;
        ok = 1;
        break;
    }

    case CCWLD_EXPR_REGION_ORIGIN: {
        const struct ccwld_mem_rec *r = plan_find_region(plan, e->name);
        if (!r) {
            if (error_message) {
                char buf[256];
                snprintf(buf, sizeof(buf), "undefined region '%s'", e->name);
                *error_message = strdup(buf);
            }
            ok = 0;
        } else {
            *out = r->origin;
            ok = 1;
        }
        break;
    }

    case CCWLD_EXPR_REGION_LENGTH: {
        const struct ccwld_mem_rec *r = plan_find_region(plan, e->name);
        if (!r) {
            if (error_message) {
                char buf[256];
                snprintf(buf, sizeof(buf), "undefined region '%s'", e->name);
                *error_message = strdup(buf);
            }
            ok = 0;
        } else {
            *out = r->length;
            ok = 1;
        }
        break;
    }

    case CCWLD_EXPR_SIZEOF_HEADERS:
        /* §2.3: sizeof_headers — platform-dependent; we use a conservative
         * default that the LTO/emit phase can override. */
        *out = 64; /* minimal ELF header size */
        ok = 1;
        break;

    case CCWLD_EXPR_SIZEOF: {
        const struct ccwld_sec_rec *s = plan_find_section(plan, e->name);
        if (!s) {
            if (error_message) {
                char buf[256];
                snprintf(buf, sizeof(buf), "undefined section '%s'", e->name);
                *error_message = strdup(buf);
            }
            ok = 0;
        } else {
            *out = s->size;
            ok = 1;
        }
        break;
    }

    case CCWLD_EXPR_ADDR: {
        const struct ccwld_sec_rec *s = plan_find_section(plan, e->name);
        if (!s) {
            if (error_message) {
                char buf[256];
                snprintf(buf, sizeof(buf), "undefined section '%s'", e->name);
                *error_message = strdup(buf);
            }
            ok = 0;
        } else {
            *out = s->vma;
            ok = 1;
        }
        break;
    }

    case CCWLD_EXPR_LOADADDR: {
        const struct ccwld_sec_rec *s = plan_find_section(plan, e->name);
        if (!s) {
            if (error_message) {
                char buf[256];
                snprintf(buf, sizeof(buf), "undefined section '%s'", e->name);
                *error_message = strdup(buf);
            }
            ok = 0;
        } else {
            *out = s->lma;
            ok = 1;
        }
        break;
    }

    case CCWLD_EXPR_SEGMENT_START: {
        /* §2.3: segment_start — lookup the named segment/phdr */
        if (!plan || !e->name) {
            ok = 0;
        } else {
            int found = 0;
            for (size_t i = 0; i < plan->nphdrs; i++) {
                if (plan->phdrs[i].name &&
                    strcmp(plan->phdrs[i].name, e->name) == 0) {
                    *out = plan->phdrs[i].vaddr;
                    found = 1;
                    break;
                }
            }
            if (!found) {
                if (error_message) {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "undefined segment '%s'", e->name);
                    *error_message = strdup(buf);
                }
                ok = 0;
            }
        }
        break;
    }

    case CCWLD_EXPR_ALIGN:
        if (!eval_inner(e->a, plan, dot, &a_val, error_message)) { ok = 0; break; }
        /* align up: (a + boundary-1) & ~(boundary-1) */
        if (e->ival == 0) {
            *out = a_val;
        } else {
            *out = (a_val + e->ival - 1) & ~(e->ival - 1);
        }
        ok = 1;
        break;

    case CCWLD_EXPR_UNARY:
        if (!eval_inner(e->a, plan, dot, &a_val, error_message)) { ok = 0; break; }
        switch (e->op) {
        case CCWLD_OP_NEG: *out = (uint64_t)(-(int64_t)a_val); break;
        case CCWLD_OP_NOT: *out = !a_val; break;
        case CCWLD_OP_ABS: *out = (int64_t)a_val < 0 ? (uint64_t)(-(int64_t)a_val) : a_val; break;
        default:
            if (error_message) {
                char buf[256];
                snprintf(buf, sizeof(buf), "unknown unary operator '%c'", (char)e->op);
                *error_message = strdup(buf);
            }
            ok = 0;
            break;
        }
        break;

    case CCWLD_EXPR_BINARY:
        if (!eval_inner(e->a, plan, dot, &a_val, error_message)) { ok = 0; break; }
        if (!eval_inner(e->b, plan, dot, &b_val, error_message)) { ok = 0; break; }
        switch (e->op) {
        case CCWLD_OP_ADD: *out = a_val + b_val; break;
        case CCWLD_OP_SUB: *out = a_val - b_val; break;
        case CCWLD_OP_MUL: *out = a_val * b_val; break;
        case CCWLD_OP_DIV:
            if (b_val == 0) {
                if (error_message) *error_message = strdup("division by zero");
                ok = 0;
            } else {
                *out = a_val / b_val;
            }
            break;
        case CCWLD_OP_MOD:
            if (b_val == 0) {
                if (error_message) *error_message = strdup("modulo by zero");
                ok = 0;
            } else {
                *out = a_val % b_val;
            }
            break;
        case CCWLD_OP_AND: *out = a_val & b_val; break;
        case CCWLD_OP_OR:  *out = a_val | b_val; break;
        case CCWLD_OP_XOR: *out = a_val ^ b_val; break;
        case CCWLD_OP_SHL: *out = a_val << (b_val & 63); break;
        case CCWLD_OP_SHR: *out = a_val >> (b_val & 63); break;
        default:
            if (error_message) {
                char buf[256];
                snprintf(buf, sizeof(buf), "unknown binary operator '%c'", (char)e->op);
                *error_message = strdup(buf);
            }
            ok = 0;
            break;
        }
        break;

    case CCWLD_EXPR_MAX:
        if (!eval_inner(e->a, plan, dot, &a_val, error_message)) { ok = 0; break; }
        if (!eval_inner(e->b, plan, dot, &b_val, error_message)) { ok = 0; break; }
        *out = a_val > b_val ? a_val : b_val;
        ok = 1;
        break;

    case CCWLD_EXPR_MIN:
        if (!eval_inner(e->a, plan, dot, &a_val, error_message)) { ok = 0; break; }
        if (!eval_inner(e->b, plan, dot, &b_val, error_message)) { ok = 0; break; }
        *out = a_val < b_val ? a_val : b_val;
        ok = 1;
        break;

    case CCWLD_EXPR_COND:
        if (!eval_inner(e->a, plan, dot, &a_val, error_message)) { ok = 0; break; }
        if (a_val) {
            ok = eval_inner(e->b, plan, dot, out, error_message);
        } else {
            ok = eval_inner(e->c, plan, dot, out, error_message);
        }
        break;

    default:
        if (error_message) {
            char buf[256];
            snprintf(buf, sizeof(buf), "unknown expression kind %d", e->kind);
            *error_message = strdup(buf);
        }
        ok = 0;
        break;
    }

    return ok;
}

int ccwld_expr_eval(const ccwld_expr *e,
                    const struct ccwld_plan *plan,
                    uint64_t dot,
                    uint64_t *out,
                    char **error_message)
{
    if (!e || !plan || !out) {
        if (error_message) *error_message = strdup("invalid arguments to eval");
        return 0;
    }
    /* Reset all visited flags before evaluation */
    ccwld_expr_reset_visited((ccwld_expr *)e);
    int ok = eval_inner(e, plan, dot, out, error_message);
    /* Reset again after */
    ccwld_expr_reset_visited((ccwld_expr *)e);
    return ok;
}

/* --- serialization --- */

#define APPEND_BUF(s, len, cap, ...) do { \
    char _b[256]; \
    int _z = snprintf(_b, sizeof(_b), __VA_ARGS__); \
    if (_z < 0) break; \
    if ((len) + (size_t)_z + 1 > (cap)) { \
        size_t _nc = (cap) ? (cap) * 2 : 1024; \
        while (_nc < (len) + (size_t)_z + 1) _nc *= 2; \
        char *_ns = realloc(s, _nc); \
        if (!_ns) break; \
        (s) = _ns; (cap) = _nc; \
    } \
    memcpy((s) + (len), _b, (size_t)_z); \
    (len) += (size_t)_z; \
    (s)[len] = 0; \
} while(0)

void ccwld_expr_to_string(const ccwld_expr *e, char **out, size_t *len, size_t *cap) {
    if (!e) { APPEND_BUF(*out, *len, *cap, "null"); return; }

    switch (e->kind) {
    case CCWLD_EXPR_INT:
        APPEND_BUF(*out, *len, *cap, "%" PRIu64, e->ival);
        break;
    case CCWLD_EXPR_SYMBOL:
        APPEND_BUF(*out, *len, *cap, "sym(%s)", e->name ? e->name : "");
        break;
    case CCWLD_EXPR_DOT:
        APPEND_BUF(*out, *len, *cap, "dot");
        break;
    case CCWLD_EXPR_DEFINED:
        APPEND_BUF(*out, *len, *cap, "defined(%s)", e->name ? e->name : "");
        break;
    case CCWLD_EXPR_REGION_ORIGIN:
        APPEND_BUF(*out, *len, *cap, "origin(%s)", e->name ? e->name : "");
        break;
    case CCWLD_EXPR_REGION_LENGTH:
        APPEND_BUF(*out, *len, *cap, "length(%s)", e->name ? e->name : "");
        break;
    case CCWLD_EXPR_SIZEOF:
        APPEND_BUF(*out, *len, *cap, "sizeof(%s)", e->name ? e->name : "");
        break;
    case CCWLD_EXPR_ADDR:
        APPEND_BUF(*out, *len, *cap, "addr(%s)", e->name ? e->name : "");
        break;
    case CCWLD_EXPR_LOADADDR:
        APPEND_BUF(*out, *len, *cap, "loadaddr(%s)", e->name ? e->name : "");
        break;
    case CCWLD_EXPR_SIZEOF_HEADERS:
        APPEND_BUF(*out, *len, *cap, "sizeof_headers");
        break;
    case CCWLD_EXPR_SEGMENT_START:
        APPEND_BUF(*out, *len, *cap, "segment_start(%s)", e->name ? e->name : "");
        break;
    case CCWLD_EXPR_ALIGN:
        APPEND_BUF(*out, *len, *cap, "align(");
        ccwld_expr_to_string(e->a, out, len, cap);
        APPEND_BUF(*out, *len, *cap, ",%" PRIu64 ")", e->ival);
        break;
    case CCWLD_EXPR_UNARY:
        APPEND_BUF(*out, *len, *cap, "(%c", (char)e->op);
        ccwld_expr_to_string(e->a, out, len, cap);
        APPEND_BUF(*out, *len, *cap, ")");
        break;
    case CCWLD_EXPR_MAX:
        APPEND_BUF(*out, *len, *cap, "max(");
        ccwld_expr_to_string(e->a, out, len, cap);
        APPEND_BUF(*out, *len, *cap, ",");
        ccwld_expr_to_string(e->b, out, len, cap);
        APPEND_BUF(*out, *len, *cap, ")");
        break;
    case CCWLD_EXPR_MIN:
        APPEND_BUF(*out, *len, *cap, "min(");
        ccwld_expr_to_string(e->a, out, len, cap);
        APPEND_BUF(*out, *len, *cap, ",");
        ccwld_expr_to_string(e->b, out, len, cap);
        APPEND_BUF(*out, *len, *cap, ")");
        break;
    case CCWLD_EXPR_COND:
        APPEND_BUF(*out, *len, *cap, "cond(");
        ccwld_expr_to_string(e->a, out, len, cap);
        APPEND_BUF(*out, *len, *cap, ",");
        ccwld_expr_to_string(e->b, out, len, cap);
        APPEND_BUF(*out, *len, *cap, ",");
        ccwld_expr_to_string(e->c, out, len, cap);
        APPEND_BUF(*out, *len, *cap, ")");
        break;
    case CCWLD_EXPR_BINARY:
        APPEND_BUF(*out, *len, *cap, "(");
        ccwld_expr_to_string(e->a, out, len, cap);
        APPEND_BUF(*out, *len, *cap, "%c", (char)e->op);
        ccwld_expr_to_string(e->b, out, len, cap);
        APPEND_BUF(*out, *len, *cap, ")");
        break;
    default:
        APPEND_BUF(*out, *len, *cap, "?");
        break;
    }
}
