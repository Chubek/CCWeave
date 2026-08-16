/* §5.2/§5.4: MPC-based text parser. Parsing yields exactly the same
 * in-memory structures the C API builds (round-trip is required). */

#include "ccw_ir_internal.h"
#include "mpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The grammar is the normative surface syntax of Weave IR. */
static const char *const CCW_IR_GRAMMAR =
    " str      : /\"(\\\\\\\\.|[^\"])*\"/ ;                                  \n"
    " ident    : /[a-zA-Z_][a-zA-Z0-9_.\\-]*/ ;                              \n"
    " inum     : /-?[0-9]+/ ;                                                \n"
    " fnum     : /-?([0-9]+\\.[0-9]+([eE][-+]?[0-9]+)?|[0-9]+([eE][-+]?[0-9]+)?|inf|nan)/ ; \n"
    " reg      : '%' <ident> ;                                               \n"
    " fref     : '@' <ident> ;                                               \n"
    " bref     : '^' <ident> ;                                               \n"
    " iconst   : '(' \"iconst\" <ident> <inum> ')' ;                         \n"
    " fconst   : '(' \"fconst\" <ident> <fnum> ')' ;                         \n"
    " operand  : <iconst> | <fconst> | <reg> | <fref> | <bref> ;             \n"
    " attr     : '(' \"attr\" <str> <str> ')' ;                              \n"
    " dest     : '(' \"dest\" <reg> ')' ;                                    \n"
    " instr    : '(' \"instr\" <ident> <ident> <dest>? <operand>* <attr>* ')' ; \n"
    " block    : '(' \"block\" <bref> <attr>* <instr>* ')' ;                 \n"
    " param    : '(' <reg> <ident> ')' ;                                     \n"
    " params   : '(' \"params\" <param>* ')' ;                               \n"
    " function : '(' \"function\" <fref> <ident> <params> <attr>* <block>* ')' ; \n"
    " profile  : '(' \"profile\" <ident> ')' ;                               \n"
    " module   : /^/ '(' \"module\" <str> <profile> <attr>* <function>* ')' /$/ ; \n";

/* ---------- AST helpers ---------- */

/* mpc collapses single-child rules, so tags are '|'-joined token lists. */
static bool tag_has(const mpc_ast_t *ast, const char *token)
{
    const char *t = ast->tag;
    size_t n = strlen(token);
    while (*t) {
        const char *end = strchr(t, '|');
        size_t len = end ? (size_t)(end - t) : strlen(t);
        if (len == n && strncmp(t, token, n) == 0) return true;
        if (!end) break;
        t = end + 1;
    }
    return false;
}

static mpc_ast_t *child_nth(mpc_ast_t *ast, const char *token, int nth)
{
    int seen = 0;
    for (int i = 0; i < ast->children_num; i++) {
        if (tag_has(ast->children[i], token)) {
            if (seen == nth) return ast->children[i];
            seen++;
        }
    }
    return NULL;
}

static mpc_ast_t *child_first(mpc_ast_t *ast, const char *token)
{
    return child_nth(ast, token, 0);
}

/* Unquote a printer-produced string literal. Caller frees. */
static char *unquote(const char *lit)
{
    size_t n = strlen(lit);
    if (n < 2) return ccw_ir_strdup("");
    char *out = (char *)malloc(n);
    if (out == NULL) return NULL;
    size_t w = 0;
    for (size_t i = 1; i + 1 < n; i++) {
        char c = lit[i];
        if (c == '\\' && i + 2 < n) {
            char e = lit[++i];
            out[w++] = (e == 'n') ? '\n' : e;
        } else {
            out[w++] = c;
        }
    }
    out[w] = '\0';
    return out;
}

static const char *sym_name(mpc_ast_t *node)
{
    /* reg/fref/bref = sigil + ident */
    mpc_ast_t *id = child_first(node, "ident");
    if (id != NULL) return id->contents;
    /* Collapsed form: contents holds the whole token including the sigil. */
    const char *c = node->contents;
    return (c != NULL && *c) ? c + 1 : "";
}

/* ---------- AST -> canonical module ---------- */

static void build_attrs(ccw_ir *ir, ccw_node owner, mpc_ast_t *ast)
{
    for (int i = 0; i < ast->children_num; i++) {
        mpc_ast_t *a = ast->children[i];
        if (!tag_has(a, "attr")) continue;
        mpc_ast_t *k = child_nth(a, "str", 0);
        mpc_ast_t *v = child_nth(a, "str", 1);
        if (k == NULL || v == NULL) continue;
        char *ks = unquote(k->contents);
        char *vs = unquote(v->contents);
        if (ks != NULL && vs != NULL) ccw_ir_attr_set(ir, owner, ks, vs);
        free(ks);
        free(vs);
    }
}

static ccw_node build_operand(ccw_ir *ir, mpc_ast_t *o)
{
    if (tag_has(o, "iconst")) {
        mpc_ast_t *ty = child_first(o, "ident");
        mpc_ast_t *v  = child_first(o, "inum");
        ccw_ir_type t = CCW_TY_I64;
        if (ty != NULL) ccw_ir_type_parse(ty->contents, &t);
        return ccw_ir_operand_const_int(ir, t,
                   v ? (int64_t)strtoll(v->contents, NULL, 10) : 0);
    }
    if (tag_has(o, "fconst")) {
        mpc_ast_t *ty = child_first(o, "ident");
        mpc_ast_t *v  = child_first(o, "fnum");
        ccw_ir_type t = CCW_TY_F64;
        if (ty != NULL) ccw_ir_type_parse(ty->contents, &t);
        return ccw_ir_operand_const_float(ir, t,
                   v ? strtod(v->contents, NULL) : 0.0);
    }
    if (tag_has(o, "fref"))  return ccw_ir_operand_func(ir, sym_name(o));
    if (tag_has(o, "bref"))  return ccw_ir_operand_block(ir, sym_name(o));
    return ccw_ir_operand_reg(ir, sym_name(o));
}

static bool build_instr(ccw_ir *ir, ccw_node blk, mpc_ast_t *in)
{
    mpc_ast_t *op = child_nth(in, "ident", 0);
    mpc_ast_t *ty = child_nth(in, "ident", 1);
    if (op == NULL || ty == NULL) return false;
    ccw_ir_type t = CCW_TY_VOID;
    ccw_ir_type_parse(ty->contents, &t);
    ccw_node ins = ccw_ir_instr_build(ir, op->contents, t);
    if (ins == 0) return false;

    mpc_ast_t *d = child_first(in, "dest");
    if (d != NULL) {
        mpc_ast_t *r = child_first(d, "reg");
        ccw_ir_instr_set_dest(ir, ins, r ? sym_name(r) : NULL);
    }
    for (int i = 0; i < in->children_num; i++) {
        mpc_ast_t *c = in->children[i];
        if (tag_has(c, "dest") || tag_has(c, "attr")) continue;
        if (tag_has(c, "operand") || tag_has(c, "iconst") || tag_has(c, "fconst") ||
            tag_has(c, "reg") || tag_has(c, "fref") || tag_has(c, "bref")) {
            ccw_node o = build_operand(ir, c);
            if (o != 0) ccw_ir_instr_add_operand(ir, ins, o);
        }
    }
    build_attrs(ir, ins, in);
    return ccw_ir_block_append_instr(ir, blk, ins) == CCW_OK;
}

static bool build_block(ccw_ir *ir, ccw_node fn, mpc_ast_t *b)
{
    mpc_ast_t *nm = child_first(b, "bref");
    ccw_node blk = ccw_ir_block_add(ir, fn, nm ? sym_name(nm) : "entry");
    if (blk == 0) return false;
    build_attrs(ir, blk, b);
    for (int i = 0; i < b->children_num; i++)
        if (tag_has(b->children[i], "instr"))
            if (!build_instr(ir, blk, b->children[i])) return false;
    return true;
}

static bool build_function(ccw_ir *ir, mpc_ast_t *f)
{
    mpc_ast_t *nm = child_first(f, "fref");
    mpc_ast_t *ty = child_first(f, "ident");
    ccw_ir_type rt = CCW_TY_VOID;
    if (ty != NULL) ccw_ir_type_parse(ty->contents, &rt);
    ccw_node fn = ccw_ir_function_add(ir, nm ? sym_name(nm) : "anon", rt);
    if (fn == 0) return false;

    mpc_ast_t *ps = child_first(f, "params");
    if (ps != NULL) {
        for (int i = 0; i < ps->children_num; i++) {
            mpc_ast_t *p = ps->children[i];
            if (!tag_has(p, "param")) continue;
            mpc_ast_t *r  = child_first(p, "reg");
            mpc_ast_t *pt = child_first(p, "ident");
            ccw_ir_type t = CCW_TY_VOID;
            if (pt != NULL) ccw_ir_type_parse(pt->contents, &t);
            ccw_ir_function_add_param(ir, fn, t, r ? sym_name(r) : "p");
        }
    }
    build_attrs(ir, fn, f);
    for (int i = 0; i < f->children_num; i++)
        if (tag_has(f->children[i], "block"))
            if (!build_block(ir, fn, f->children[i])) return false;
    return true;
}

static ccw_ir *build_module(mpc_ast_t *ast, char **error_message)
{
    /* mpc collapses the top-level rule into the root node (tag ">"), so
     * the module may be either the root itself or a child of it. */
    mpc_ast_t *root = ast;
    if (!tag_has(root, "module")) {
        mpc_ast_t *inner = child_first(root, "module");
        if (inner != NULL) root = inner;
    }
    mpc_ast_t *nm = child_first(root, "str");
    mpc_ast_t *pf = child_first(root, "profile");
    mpc_ast_t *pfid = pf ? child_first(pf, "ident") : NULL;
    ccw_profile profile = CCW_PROFILE_TILLY;
    if (pfid == NULL || !ccw_profile_parse(pfid->contents, &profile)) {
        if (error_message)
            *error_message = ccw_ir_strdup("module header must declare (profile tilly|on1x)");
        return NULL;
    }
    char *name = nm ? unquote(nm->contents) : ccw_ir_strdup("module");
    ccw_ir *ir = ccw_ir_module_create(name ? name : "module", profile);
    free(name);
    if (ir == NULL) {
        if (error_message) *error_message = ccw_ir_strdup("out of memory");
        return NULL;
    }
    build_attrs(ir, 0, root);
    for (int i = 0; i < root->children_num; i++) {
        if (!tag_has(root->children[i], "function")) continue;
        if (!build_function(ir, root->children[i])) {
            ccw_ir_module_destroy(ir);
            if (error_message) *error_message = ccw_ir_strdup("malformed function");
            return NULL;
        }
    }
    return ir;
}

ccw_ir *ccw_ir_parse(const char *text, char **error_message)
{
    if (error_message) *error_message = NULL;
    if (text == NULL) return NULL;

    mpc_parser_t *str_p = mpc_new("str"), *ident = mpc_new("ident");
    mpc_parser_t *inum = mpc_new("inum"), *fnum = mpc_new("fnum");
    mpc_parser_t *reg = mpc_new("reg"), *fref = mpc_new("fref"), *bref = mpc_new("bref");
    mpc_parser_t *iconst = mpc_new("iconst"), *fconst = mpc_new("fconst");
    mpc_parser_t *operand = mpc_new("operand"), *attr = mpc_new("attr");
    mpc_parser_t *dest = mpc_new("dest"), *instr = mpc_new("instr");
    mpc_parser_t *block = mpc_new("block"), *param = mpc_new("param");
    mpc_parser_t *params = mpc_new("params"), *function = mpc_new("function");
    mpc_parser_t *profile = mpc_new("profile"), *module = mpc_new("module");

    mpc_err_t *gerr = mpca_lang(MPCA_LANG_DEFAULT, CCW_IR_GRAMMAR,
                                str_p, ident, inum, fnum, reg, fref, bref,
                                iconst, fconst, operand, attr, dest, instr,
                                block, param, params, function, profile,
                                module, NULL);
    if (gerr != NULL) {
        if (error_message) {
            char *m = mpc_err_string(gerr);
            *error_message = ccw_ir_strdup(m ? m : "grammar error");
            free(m);
        }
        mpc_err_delete(gerr);
        mpc_cleanup(19, str_p, ident, inum, fnum, reg, fref, bref, iconst,
                    fconst, operand, attr, dest, instr, block, param, params,
                    function, profile, module);
        return NULL;
    }

    mpc_result_t r;
    ccw_ir *ir = NULL;
    if (mpc_parse("<weave-ir>", text, module, &r)) {
        ir = build_module((mpc_ast_t *)r.output, error_message);
        mpc_ast_delete((mpc_ast_t *)r.output);
    } else {
        if (error_message) {
            char *m = mpc_err_string(r.error);
            *error_message = ccw_ir_strdup(m ? m : "parse error");
            free(m);
        }
        mpc_err_delete(r.error);
    }

    mpc_cleanup(19, str_p, ident, inum, fnum, reg, fref, bref, iconst,
                fconst, operand, attr, dest, instr, block, param, params,
                function, profile, module);
    return ir;
}

ccw_ir *ccw_ir_parse_file(const char *path, char **error_message)
{
    if (error_message) *error_message = NULL;
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        if (error_message) *error_message = ccw_ir_strdup("cannot open file");
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    long size = ftell(fp);
    if (size < 0) { fclose(fp); return NULL; }
    rewind(fp);
    char *buf = (char *)malloc((size_t)size + 1u);
    if (buf == NULL) {
        fclose(fp);
        if (error_message) *error_message = ccw_ir_strdup("out of memory");
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)size, fp);
    buf[got] = '\0';
    fclose(fp);
    ccw_ir *ir = ccw_ir_parse(buf, error_message);
    free(buf);
    return ir;
}
