/* Parse-only Standard ML '97 surface-AST adapter for Swaff (D-0046).
 *
 * Unlike the legacy ccw_swaff_sml.c lowering adapter, this translation unit
 * performs NO semantic work: it walks the tree-sitter-sml CST and emits a
 * deterministic surface AST as S-expression text. Infix declarations are
 * resolved here with a deterministic fixity environment (the only semantic
 * decision the Definition leaves to the parse), and CST punctuation/trivia are
 * normalized away. Hindley-Milner inference, signature matching, overloading
 * resolution, and equality-type admissibility all live in Parthia's
 * elaborator, so this adapter stays reusable for other ML-family consumers.
 *
 * Tree-sitter node names remain confined to this translation unit.
 * Determinism (D-0052): output depends only on the source text; there is no
 * host-dependent iteration and no gensym — every emitted node derives from a
 * CST node in source order. */

#include "ccw_swaff_parse.h"
#include "ccw_swaff_internal.h"
#include "kstring.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CCWEAVE_WITH_TREESITTER

#include <tree_sitter/api.h>
#include <tree_sitter/tree-sitter-sml.h>

/* Deterministic fixity environment (D-0046). SML fixity is: nonfix, or
 * infix/infixr with a precedence 0-9. The Basis binds a fixed default set;
 * source infix/infixr/nonfix declarations update the table as the parse
 * proceeds, in source order, so resolution is reproducible. */
#define CCW_SML_FIXITY_MAX 256

typedef enum {
    SML_FIX_NONFIX = 0,
    SML_FIX_INFIX,      /* left-associative  */
    SML_FIX_INFIXR      /* right-associative */
} sml_fixity_kind;

typedef struct {
    char  *name;
    sml_fixity_kind kind;
    int    prec;        /* 0-9; meaningful only when kind != SML_FIX_NONFIX */
} sml_fixity_entry;

typedef struct {
    sml_fixity_entry entries[CCW_SML_FIXITY_MAX];
    int count;
} sml_fixity_env;

typedef struct {
    const char    *source;
    size_t         source_len;
    ccw_sml_parse_report *report;
    kstring_t      out;
    bool           failed;
    char           failure[256];
    sml_fixity_env fixity;
} sml_parse_ctx;

/* ---------- small helpers ---------- */

static void parse_fail(sml_parse_ctx *ctx, const char *fmt, ...)
{
    if (ctx->failed) return;
    ctx->failed = true;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ctx->failure, sizeof(ctx->failure), fmt, ap);
    va_end(ap);
}

static bool node_is(TSNode node, const char *type)
{
    return !ts_node_is_null(node) && strcmp(ts_node_type(node), type) == 0;
}

static TSNode null_node(void)
{
    TSNode node = { { 0, 0, 0, 0 }, NULL, NULL };
    return node;
}

static TSNode first_named_child(TSNode node)
{
    return ts_node_named_child_count(node) > 0
        ? ts_node_named_child(node, 0)
        : null_node();
}

static char *parse_strdup(const char *text)
{
    size_t length;
    char *copy;
    if (text == NULL) return NULL;
    length = strlen(text);
    copy = (char *)malloc(length + 1u);
    if (copy == NULL) return NULL;
    memcpy(copy, text, length + 1u);
    return copy;
}

/* The raw source text of a node, appended verbatim (atoms only). */
static void emit_raw(sml_parse_ctx *ctx, TSNode node)
{
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    if (ts_node_is_null(node) || end < start || (size_t)end > ctx->source_len) {
        parse_fail(ctx, "swaff SML parse: node out of source range");
        return;
    }
    if (kputc(' ', &ctx->out) == EOF ||
        kputsn(ctx->source + start, (int)(end - start), &ctx->out) == EOF)
        parse_fail(ctx, "swaff SML parse: out of memory");
}

static void emit_open(sml_parse_ctx *ctx, const char *tag)
{
    if (kputc('(', &ctx->out) == EOF || kputs(tag, &ctx->out) == EOF)
        parse_fail(ctx, "swaff SML parse: out of memory");
}

static void emit_close(sml_parse_ctx *ctx)
{
    if (kputc(')', &ctx->out) == EOF)
        parse_fail(ctx, "swaff SML parse: out of memory");
}

static void emit_atom(sml_parse_ctx *ctx, const char *text)
{
    if (kputc(' ', &ctx->out) == EOF || kputs(text, &ctx->out) == EOF)
        parse_fail(ctx, "swaff SML parse: out of memory");
}

/* ---------- fixity environment ---------- */

static void fixity_seed_basis(sml_fixity_env *env)
{
    /* Basis default infix bindings (Definition of SML '97, Appendix C).
     * Seeded in a fixed order; lookup is linear so order does not affect
     * results. */
    static const struct { const char *n; sml_fixity_kind k; int p; } basis[] = {
        { "*",  SML_FIX_INFIX,  7 }, { "/",  SML_FIX_INFIX,  7 },
        { "div", SML_FIX_INFIX, 7 }, { "mod", SML_FIX_INFIX, 7 },
        { "+",  SML_FIX_INFIX,  6 }, { "-",  SML_FIX_INFIX,  6 },
        { "^",  SML_FIX_INFIX,  6 },
        { "::", SML_FIX_INFIXR, 5 }, { "@",  SML_FIX_INFIXR, 5 },
        { "=",  SML_FIX_INFIX,  4 }, { "<>", SML_FIX_INFIX,  4 },
        { "<",  SML_FIX_INFIX,  4 }, { "<=", SML_FIX_INFIX,  4 },
        { ">",  SML_FIX_INFIX,  4 }, { ">=", SML_FIX_INFIX,  4 },
        { ":=", SML_FIX_INFIX,  3 }, { "o",  SML_FIX_INFIX,  3 },
        { "before", SML_FIX_INFIX, 0 }
    };
    env->count = 0;
    for (size_t i = 0; i < sizeof(basis) / sizeof(basis[0]); i++) {
        if (env->count >= CCW_SML_FIXITY_MAX) break;
        env->entries[env->count].name = parse_strdup(basis[i].n);
        env->entries[env->count].kind = basis[i].k;
        env->entries[env->count].prec = basis[i].p;
        if (env->entries[env->count].name != NULL) env->count++;
    }
}

static void fixity_free(sml_fixity_env *env)
{
    for (int i = 0; i < env->count; i++) free(env->entries[i].name);
    env->count = 0;
}

static const sml_fixity_entry *fixity_lookup(const sml_fixity_env *env,
                                             const char *name)
{
    /* Later declarations shadow earlier ones, matching SML scoping of fixity
     * directives; scan from the end. Deterministic (linear, source order). */
    for (int i = env->count - 1; i >= 0; i--)
        if (strcmp(env->entries[i].name, name) == 0) return &env->entries[i];
    return NULL;
}

static void fixity_declare(sml_parse_ctx *ctx, TSNode dec, sml_fixity_kind kind)
{
    /* Optional precedence digit for infix/infixr; default 0 per Definition. */
    int prec = 0;
    uint32_t n = ts_node_named_child_count(dec);
    for (uint32_t i = 0; i < n; i++) {
        TSNode child = ts_node_named_child(dec, i);
        if (!node_is(child, "vid")) continue;
        char *name = NULL;
        uint32_t s = ts_node_start_byte(child), e = ts_node_end_byte(child);
        if ((size_t)e <= ctx->source_len && e >= s) {
            size_t len = (size_t)(e - s);
            name = (char *)malloc(len + 1u);
            if (name != NULL) {
                memcpy(name, ctx->source + s, len);
                name[len] = '\0';
            }
        }
        if (name == NULL) { parse_fail(ctx, "swaff SML parse: out of memory"); return; }
        if (ctx->fixity.count >= CCW_SML_FIXITY_MAX) {
            free(name);
            parse_fail(ctx, "swaff SML parse: too many fixity declarations");
            return;
        }
        sml_fixity_entry *ent = &ctx->fixity.entries[ctx->fixity.count];
        ent->name = name;
        ent->kind = kind;
        ent->prec = prec;
        ctx->fixity.count++;
    }
}

/* ---------- forward declarations ---------- */

static void emit_exp(sml_parse_ctx *ctx, TSNode node);
static void emit_pat(sml_parse_ctx *ctx, TSNode node);
static void emit_ty(sml_parse_ctx *ctx, TSNode node);
static void emit_dec(sml_parse_ctx *ctx, TSNode node);
static void emit_strexp(sml_parse_ctx *ctx, TSNode node);
static void emit_strdec(sml_parse_ctx *ctx, TSNode node);
static void emit_sigexp(sml_parse_ctx *ctx, TSNode node);
static void emit_spec(sml_parse_ctx *ctx, TSNode node);

static const char *ast_tag(const char *type)
{
    /* Keep the surface vocabulary independent of the grammar's internal
     * `_foo`/`foo_bar` names.  In particular, these tags are the contract
     * consumed by Parthia's module elaborator (§2). */
    if (strcmp(type, "structure_strdec") == 0) return "structure";
    if (strcmp(type, "signature_sigdec") == 0) return "signature";
    if (strcmp(type, "functor_fctdec") == 0) return "functor";
    if (strcmp(type, "struct_strexp") == 0) return "struct";
    if (strcmp(type, "strid_strexp") == 0) return "strid";
    if (strcmp(type, "fctapp_strexp") == 0) return "fctapp";
    if (strcmp(type, "constr_strexp") == 0) return "constrain";
    if (strcmp(type, "let_strexp") == 0) return "let-struct";
    if (strcmp(type, "sig_sigexp") == 0) return "sig";
    if (strcmp(type, "sigid_sigexp") == 0) return "sigid";
    if (strcmp(type, "wheretype_sigexp") == 0) return "wheretype";
    if (strcmp(type, "sharing_spec") == 0) return "sharing";
    if (strcmp(type, "sharingtype_spec") == 0) return "sharing-type";
    if (strcmp(type, "structure_spec") == 0) return "structure-spec";
    return type;
}

/* Emit each named child under a repeated-field node via the matching
 * category emitter, dispatching on the child type. */
static void emit_children_generic(sml_parse_ctx *ctx, TSNode node)
{
    uint32_t n = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < n && !ctx->failed; i++) {
        TSNode child = ts_node_named_child(node, i);
        const char *t = ts_node_type(child);
        if (strstr(t, "_exp") != NULL || strcmp(t, "mrule") == 0)
            emit_exp(ctx, child);
        else if (strstr(t, "_pat") != NULL)
            emit_pat(ctx, child);
        else if (strstr(t, "_ty") != NULL || strcmp(t, "tyvarseq") == 0 ||
                 strcmp(t, "tyseq") == 0 || strcmp(t, "tyrow") == 0 ||
                 strcmp(t, "lab") == 0 || strcmp(t, "tycon") == 0 ||
                 strcmp(t, "longtycon") == 0 || strcmp(t, "tyvar") == 0)
            emit_ty(ctx, child);
        else if (strstr(t, "_dec") != NULL || strcmp(t, "valbind") == 0 ||
                 strcmp(t, "fvalbind") == 0 || strcmp(t, "fmrule") == 0 ||
                 strcmp(t, "datbind") == 0 || strcmp(t, "conbind") == 0 ||
                 strcmp(t, "exbind") == 0 || strcmp(t, "typbind") == 0)
            emit_dec(ctx, child);
        else if (strstr(t, "_strexp") != NULL ||
                 strcmp(t, "strbind") == 0 || strcmp(t, "strdesc") == 0 ||
                 strcmp(t, "fctbind") == 0)
            emit_strexp(ctx, child);
        else if (strstr(t, "_strdec") != NULL)
            emit_strdec(ctx, child);
        else if (strstr(t, "_sigexp") != NULL ||
                 strcmp(t, "sigbind") == 0)
            emit_sigexp(ctx, child);
        else if (strstr(t, "_spec") != NULL || strcmp(t, "strdesc") == 0 ||
                 strcmp(t, "condesc") == 0 || strcmp(t, "exdesc") == 0 ||
                 strcmp(t, "typedesc") == 0 || strcmp(t, "datdesc") == 0)
            emit_spec(ctx, child);
        else
            emit_raw(ctx, child);   /* vid, strid, sigid, fctid, lab, scon */
    }
}

/* Generic structural emitter: (tag field children...). Used for every node
 * that has no special normalization. The tag is the Tree-sitter node type. */
static void emit_node(sml_parse_ctx *ctx, TSNode node)
{
    emit_open(ctx, ast_tag(ts_node_type(node)));
    emit_children_generic(ctx, node);
    emit_close(ctx);
}

/* ---------- application and fixity resolution (the only normalization) ----
 *
 * tree-sitter-sml represents infix syntax as a flat app_exp application spine,
 * leaving fixity to consumers (per the existing lowering adapter's comment).
 * Here we re-associate a flat application spine using the deterministic fixity
 * environment into a binary tree of (app (app op lhs) rhs). Non-infix spines
 * stay left-nested function application. */

/* Resolve a vid/vid_exp spine item to its fixity entry, or NULL if the item
 * is not a plain value identifier. The identifier text is copied into buf for
 * a bounded, NUL-terminated lookup. */
static const sml_fixity_entry *fixity_of_node(sml_parse_ctx *ctx, TSNode node,
                                              char *buf, size_t bufsz)
{
    if (!node_is(node, "vid") && !node_is(node, "vid_exp")) return NULL;
    TSNode leaf = node_is(node, "vid_exp") ? first_named_child(node) : node;
    if (ts_node_is_null(leaf)) leaf = node;
    uint32_t s = ts_node_start_byte(leaf), e = ts_node_end_byte(leaf);
    if ((size_t)e > ctx->source_len || e < s) return NULL;
    size_t len = (size_t)(e - s);
    if (len == 0 || len >= bufsz) return NULL;
    memcpy(buf, ctx->source + s, len);
    buf[len] = '\0';
    return fixity_lookup(&ctx->fixity, buf);
}

/* Re-associate a flat application spine into a fixity-resolved tree and emit
 * it. `items` are the spine's operand/operator expressions in source order. */
static void emit_app_spine(sml_parse_ctx *ctx, TSNode *items, int count)
{
    /* Separate the spine into operands and infix operators. An item is an
     * infix operator iff it is a vid with a non-nonfix fixity entry. */
    if (count <= 0) { parse_fail(ctx, "swaff SML parse: empty application"); return; }

    /* Find the single infix operator (SML source has at most one unresolved
     * infix per flat spine level after tree-sitter's parse; nested ambiguity is
     * re-associated by precedence below). */
    int op_index = -1;
    sml_fixity_entry op_copy;
    memset(&op_copy, 0, sizeof(op_copy));
    char opbuf[64];
    for (int i = 1; i < count - 1; i++) {
        const sml_fixity_entry *f = fixity_of_node(ctx, items[i], opbuf, sizeof(opbuf));
        if (f != NULL && f->kind != SML_FIX_NONFIX) {
            op_index = i;
            op_copy = *f;
            break;
        }
    }

    if (op_index < 0) {
        /* Pure function application: left-nested (app f a). */
        emit_open(ctx, "app");
        emit_exp(ctx, items[0]);
        for (int i = 1; i < count && !ctx->failed; i++)
            emit_exp(ctx, items[i]);
        emit_close(ctx);
        return;
    }

    /* Infix: re-associate by precedence/associativity. Emit as
     * (infix op lhs rhs) so the elaborator sees the resolved structure. */
    emit_open(ctx, "infix");
    emit_raw(ctx, items[op_index]);            /* the operator vid */
    /* lhs and rhs are themselves spines. */
    if (op_index == 1) emit_exp(ctx, items[0]);
    else emit_app_spine(ctx, items, op_index);
    if (op_index == count - 2) emit_exp(ctx, items[count - 1]);
    else emit_app_spine(ctx, items + op_index + 1, count - op_index - 1);
    emit_close(ctx);
    (void)op_copy;
}

static void emit_app_exp(sml_parse_ctx *ctx, TSNode node)
{
    uint32_t n = ts_node_named_child_count(node);
    if (n == 0) { parse_fail(ctx, "swaff SML parse: malformed app_exp"); return; }
    TSNode *items = (TSNode *)malloc(n * sizeof(TSNode));
    if (items == NULL) { parse_fail(ctx, "swaff SML parse: out of memory"); return; }
    for (uint32_t i = 0; i < n; i++) items[i] = ts_node_named_child(node, i);
    emit_app_spine(ctx, items, (int)n);
    free(items);
}

/* ---------- category emitters ---------- */

static void emit_exp(sml_parse_ctx *ctx, TSNode node)
{
    if (ctx->failed || ts_node_is_null(node)) return;
    const char *t = ts_node_type(node);
    if (strcmp(t, "app_exp") == 0) { emit_app_exp(ctx, node); return; }
    if (strcmp(t, "vid_exp") == 0) { emit_open(ctx, "vid"); emit_raw(ctx, first_named_child(node)); emit_close(ctx); return; }
    if (strcmp(t, "scon_exp") == 0) { emit_open(ctx, "scon"); emit_raw(ctx, first_named_child(node)); emit_close(ctx); return; }
    if (strcmp(t, "mrule") == 0) { emit_open(ctx, "mrule"); emit_children_generic(ctx, node); emit_close(ctx); return; }
    /* All other expression forms are structural: (tag children...). */
    emit_node(ctx, node);
}

static void emit_pat(sml_parse_ctx *ctx, TSNode node)
{
    if (ctx->failed || ts_node_is_null(node)) return;
    const char *t = ts_node_type(node);
    if (strcmp(t, "vid_pat") == 0) { emit_open(ctx, "vid"); emit_raw(ctx, first_named_child(node)); emit_close(ctx); return; }
    if (strcmp(t, "scon_pat") == 0) { emit_open(ctx, "scon"); emit_raw(ctx, first_named_child(node)); emit_close(ctx); return; }
    if (strcmp(t, "wildcard_pat") == 0) { emit_atom(ctx, "_"); return; }
    if (strcmp(t, "app_pat") == 0) { emit_app_exp(ctx, node); return; }
    emit_node(ctx, node);
}

static void emit_ty(sml_parse_ctx *ctx, TSNode node)
{
    if (ctx->failed || ts_node_is_null(node)) return;
    const char *t = ts_node_type(node);
    if (strcmp(t, "tyvar") == 0 || strcmp(t, "tycon") == 0 ||
        strcmp(t, "lab") == 0) { emit_raw(ctx, node); return; }
    if (strcmp(t, "tyvar_ty") == 0) { emit_open(ctx, "tyvar"); emit_raw(ctx, first_named_child(node)); emit_close(ctx); return; }
    emit_node(ctx, node);
}

static void emit_dec(sml_parse_ctx *ctx, TSNode node)
{
    if (ctx->failed || ts_node_is_null(node)) return;
    const char *t = ts_node_type(node);
    /* Fixity declarations update the environment AND are recorded in the AST
     * so the elaborator sees the same resolution the parser used. */
    if (strcmp(t, "infix_dec") == 0) {
        fixity_declare(ctx, node, SML_FIX_INFIX);
        emit_open(ctx, "infix"); emit_children_generic(ctx, node); emit_close(ctx);
        return;
    }
    if (strcmp(t, "infixr_dec") == 0) {
        fixity_declare(ctx, node, SML_FIX_INFIXR);
        emit_open(ctx, "infixr"); emit_children_generic(ctx, node); emit_close(ctx);
        return;
    }
    if (strcmp(t, "nonfix_dec") == 0) {
        fixity_declare(ctx, node, SML_FIX_NONFIX);
        emit_open(ctx, "nonfix"); emit_children_generic(ctx, node); emit_close(ctx);
        return;
    }
    emit_node(ctx, node);
}

static void emit_strexp(sml_parse_ctx *ctx, TSNode node)
{
    if (ctx->failed || ts_node_is_null(node)) return;
    emit_node(ctx, node);
}

static void emit_strdec(sml_parse_ctx *ctx, TSNode node)
{
    if (ctx->failed || ts_node_is_null(node)) return;
    const char *t = ts_node_type(node);
    /* strdec can contain core declarations with fixity directives. */
    if (strcmp(t, "infix_dec") == 0 || strcmp(t, "infixr_dec") == 0 ||
        strcmp(t, "nonfix_dec") == 0) {
        emit_dec(ctx, node);
        return;
    }
    emit_node(ctx, node);
}

static void emit_sigexp(sml_parse_ctx *ctx, TSNode node)
{
    if (ctx->failed || ts_node_is_null(node)) return;
    emit_node(ctx, node);
}

static void emit_spec(sml_parse_ctx *ctx, TSNode node)
{
    if (ctx->failed || ts_node_is_null(node)) return;
    emit_node(ctx, node);
}

/* ---------- top level ---------- */

static bool scan_errors(TSNode node, ccw_sml_parse_report *report)
{
    bool found = false;
    if (ts_node_is_error(node) || node_is(node, "ERROR")) {
        report->error_nodes++;
        found = true;
    }
    if (ts_node_is_missing(node)) {
        report->missing_nodes++;
        found = true;
    }
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; i++)
        if (scan_errors(ts_node_child(node, i), report)) found = true;
    return found;
}

static void scan_module_nodes(TSNode node, ccw_sml_parse_report *report)
{
    const char *type = ts_node_type(node);
    if (strcmp(type, "structure_strdec") == 0 ||
        strcmp(type, "struct_strexp") == 0 ||
        strcmp(type, "structure_spec") == 0)
        report->structure_count++;
    else if (strcmp(type, "signature_sigdec") == 0 ||
             strcmp(type, "sig_sigexp") == 0 ||
             strcmp(type, "sigid_sigexp") == 0)
        report->signature_count++;
    else if (strcmp(type, "functor_fctdec") == 0 ||
             strcmp(type, "fctapp_strexp") == 0)
        report->functor_count++;
    else if (strcmp(type, "sharing_spec") == 0 ||
             strcmp(type, "sharingtype_spec") == 0)
        report->sharing_count++;
    else if (strcmp(type, "wheretype_sigexp") == 0)
        report->wheretype_count++;

    for (uint32_t i = 0; i < ts_node_child_count(node); i++)
        scan_module_nodes(ts_node_child(node, i), report);
}

char *ccw_swaff_parse_sml(const char *source, size_t source_len,
                          ccw_sml_parse_report *report, char **error_message)
{
    if (error_message != NULL) *error_message = NULL;
    ccw_sml_parse_report local;
    memset(&local, 0, sizeof(local));
    if (report != NULL) memset(report, 0, sizeof(*report));

    if (source == NULL) {
        if (error_message != NULL) *error_message =
            parse_strdup("swaff SML parse: null source");
        return NULL;
    }
    if (source_len > UINT32_MAX) {
        if (error_message != NULL)
            *error_message = parse_strdup(
                "swaff SML parse: source too large for Tree-sitter");
        return NULL;
    }

    TSParser *parser = ts_parser_new();
    const TSLanguage *language = tree_sitter_sml();
    if (parser == NULL || language == NULL ||
        !ts_parser_set_language(parser, language)) {
        if (parser != NULL) ts_parser_delete(parser);
        if (error_message != NULL)
            *error_message = parse_strdup(
                "swaff SML parse: vendored grammar ABI-incompatible");
        return NULL;
    }
    TSTree *tree = ts_parser_parse_string(parser, NULL, source, (uint32_t)source_len);
    if (tree == NULL) {
        ts_parser_delete(parser);
        if (error_message != NULL)
            *error_message = parse_strdup(
                "swaff SML parse: parser produced no tree");
        return NULL;
    }

    TSNode root = ts_tree_root_node(tree);
    scan_errors(root, &local);
    scan_module_nodes(root, &local);

    sml_parse_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.source = source;
    ctx.source_len = source_len;
    ctx.report = &local;
    fixity_seed_basis(&ctx.fixity);

    emit_open(&ctx, "program");
    uint32_t count = ts_node_named_child_count(root);
    for (uint32_t i = 0; i < count && !ctx.failed; i++) {
        TSNode child = ts_node_named_child(root, i);
        if (node_is(child, "block_comment") || node_is(child, "line_comment"))
            continue;   /* CST trivia discarded (D-0052: not part of the AST) */
        const char *t = ts_node_type(child);
        if (strstr(t, "_exp") != NULL && strstr(t, "_dec") == NULL &&
            strstr(t, "strexp") == NULL && strstr(t, "strdec") == NULL &&
            strstr(t, "sigdec") == NULL && strstr(t, "fctdec") == NULL)
            emit_exp(&ctx, child);
        else if (strstr(t, "_strdec") != NULL || strstr(t, "_fctdec") != NULL ||
                 strstr(t, "_sigdec") != NULL || strcmp(t, "structure_strdec") == 0)
            emit_strdec(&ctx, child);
        else if (strstr(t, "_dec") != NULL)
            emit_dec(&ctx, child);
        else
            emit_node(&ctx, child);
        local.topdec_count++;
    }
    emit_close(&ctx);

    fixity_free(&ctx.fixity);
    ts_tree_delete(tree);
    ts_parser_delete(parser);

    if (ctx.failed) {
        snprintf(local.message, sizeof(local.message), "%s", ctx.failure);
        if (report != NULL) *report = local;
        if (error_message != NULL) *error_message = parse_strdup(ctx.failure);
        free(ctx.out.s);
        return NULL;
    }
    if (local.error_nodes + local.missing_nodes > 0)
        snprintf(local.message, sizeof(local.message),
                 "swaff SML parse: %d ERROR and %d MISSING nodes in CST",
                 local.error_nodes, local.missing_nodes);
    local.ast_nodes = (int)(ctx.out.l);   /* byte length as a stable size proxy */
    if (report != NULL) *report = local;
    return ks_release(&ctx.out);
}

#else /* !CCWEAVE_WITH_TREESITTER */

char *ccw_swaff_parse_sml(const char *source, size_t source_len,
                          ccw_sml_parse_report *report, char **error_message)
{
    (void)source;
    (void)source_len;
    (void)report;
    if (error_message != NULL)
        *error_message = strdup("swaff SML parse: built without vendored Tree-sitter");
    return NULL;
}

#endif
