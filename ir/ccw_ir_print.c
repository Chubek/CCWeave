/* §5.4: text is a serialization of the canonical in-memory module.
 * The printer's output is exactly what ir/ccw_ir_parse.c accepts. */

#include "ccw_ir_internal.h"
#include "kstring.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    kstring_t text;
    bool   failed;
} ccw_sb;

static void sb_append(ccw_sb *sb, const char *s)
{
    if (sb->failed || s == NULL) return;
    if (kputs(s, &sb->text) == EOF) sb->failed = true;
}

static void sb_printf(ccw_sb *sb, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = kvsprintf(&sb->text, fmt, ap);
    va_end(ap);
    if (n < 0) sb->failed = true;
}

static void sb_indent(ccw_sb *sb, int depth)
{
    for (int i = 0; i < depth; i++) sb_append(sb, "  ");
}

static void sb_string(ccw_sb *sb, const char *s)
{
    sb_append(sb, "\"");
    for (const char *p = s ? s : ""; *p; p++) {
        if (*p == '"' || *p == '\\') {
            char esc[3] = { '\\', *p, 0 };
            sb_append(sb, esc);
        } else if (*p == '\n') {
            sb_append(sb, "\\n");
        } else {
            char one[2] = { *p, 0 };
            sb_append(sb, one);
        }
    }
    sb_append(sb, "\"");
}

static void print_attrs(ccw_sb *sb, const ccw_attrs *a, int depth)
{
    for (int i = 0; i < a->count; i++) {
        sb_indent(sb, depth);
        sb_append(sb, "(attr ");
        sb_string(sb, a->items[i].key);
        sb_append(sb, " ");
        sb_string(sb, a->items[i].value);
        sb_append(sb, ")\n");
    }
}

static void print_operand(ccw_sb *sb, const ccw_ir *ir, ccw_node id)
{
    ccw_ir_node *o = ccw_ir_node_get_kind(ir, id, CCW_NODE_OPERAND);
    if (o == NULL) { sb_append(sb, "nil"); return; }
    switch (o->okind) {
    case CCW_OPND_REG:   sb_printf(sb, "%%%s", o->name ? o->name : ""); break;
    case CCW_OPND_FUNC:  sb_printf(sb, "@%s", o->name ? o->name : ""); break;
    case CCW_OPND_BLOCK: sb_printf(sb, "^%s", o->name ? o->name : ""); break;
    case CCW_OPND_CONST_INT:
        sb_printf(sb, "(iconst %s %lld)", ccw_ir_type_name(o->type),
                  (long long)o->ival);
        break;
    case CCW_OPND_CONST_FLOAT:
        /* %.17g so the decimal form re-reads to the same double. */
        sb_printf(sb, "(fconst %s %.17g)", ccw_ir_type_name(o->type), o->fval);
        break;
    }
}

static void print_instr(ccw_sb *sb, const ccw_ir *ir, ccw_node id, int depth)
{
    ccw_ir_node *n = ccw_ir_node_get_kind(ir, id, CCW_NODE_INSTR);
    if (n == NULL) return;
    sb_indent(sb, depth);
    sb_printf(sb, "(instr %s %s", n->opcode ? n->opcode : "nop",
              ccw_ir_type_name(n->type));
    if (n->name != NULL) sb_printf(sb, " (dest %%%s)", n->name);
    for (int i = 0; i < n->children.count; i++) {
        sb_append(sb, " ");
        print_operand(sb, ir, n->children.items[i]);
    }
    if (n->attrs.count > 0) {
        sb_append(sb, "\n");
        print_attrs(sb, &n->attrs, depth + 1);
        sb_indent(sb, depth);
    }
    sb_append(sb, ")\n");
}

char *ccw_ir_print(const ccw_ir *ir)
{
    if (ir == NULL) return NULL;
    ccw_sb sb = { { 0, 0, NULL }, false };
    sb_append(&sb, "(module ");
    sb_string(&sb, ir->name);
    sb_append(&sb, "\n");
    sb_indent(&sb, 1);
    sb_printf(&sb, "(profile %s)\n", ccw_profile_name(ir->profile));
    print_attrs(&sb, &ir->attrs, 1);

    for (int fi = 0; fi < ir->functions.count; fi++) {
        ccw_ir_node *f = ccw_ir_node_get(ir, ir->functions.items[fi]);
        if (f == NULL) continue;
        sb_indent(&sb, 1);
        sb_printf(&sb, "(function @%s %s\n", f->name ? f->name : "",
                  ccw_ir_type_name(f->type));
        sb_indent(&sb, 2);
        sb_append(&sb, "(params");
        for (int pi = 0; pi < f->param_types.count; pi++) {
            ccw_ir_node *p = ccw_ir_node_get(ir, f->param_types.items[pi]);
            if (p == NULL) continue;
            sb_printf(&sb, " (%%%s %s)", p->name ? p->name : "",
                      ccw_ir_type_name(p->type));
        }
        sb_append(&sb, ")\n");
        print_attrs(&sb, &f->attrs, 2);
        for (int bi = 0; bi < f->children.count; bi++) {
            ccw_ir_node *b = ccw_ir_node_get(ir, f->children.items[bi]);
            if (b == NULL) continue;
            sb_indent(&sb, 2);
            sb_printf(&sb, "(block ^%s\n", b->name ? b->name : "");
            print_attrs(&sb, &b->attrs, 3);
            for (int ii = 0; ii < b->children.count; ii++)
                print_instr(&sb, ir, b->children.items[ii], 3);
            sb_indent(&sb, 2);
            sb_append(&sb, ")\n");
        }
        sb_indent(&sb, 1);
        sb_append(&sb, ")\n");
    }
    sb_append(&sb, ")\n");

    if (sb.failed) {
        free(sb.text.s);
        return NULL;
    }
    if (sb.text.s == NULL) return ccw_ir_strdup("");
    return ks_release(&sb.text);
}
