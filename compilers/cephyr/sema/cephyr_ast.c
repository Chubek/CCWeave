/* Cephyr AST construction and dump. */

#include "cephyr_ast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

cephyr_ast_node *cephyr_ast_node_alloc(cephyr_ast_node_kind kind)
{
    cephyr_ast_node *node = calloc(1, sizeof(cephyr_ast_node));
    node->kind = kind;
    return node;
}

void cephyr_ast_node_free(cephyr_ast_node *node)
{
    if (!node) return;
    free((void *)node->name);
    cephyr_type_free(node->type);
    /* Free children based on kind */
    switch (node->kind) {
    case CEPHYR_NODE_EXPR_BINARY:
    case CEPHYR_NODE_EXPR_ASSIGN:
        cephyr_ast_free_recursive(node->data.binop.lhs);
        cephyr_ast_free_recursive(node->data.binop.rhs);
        break;
    case CEPHYR_NODE_EXPR_UNARY:
    case CEPHYR_NODE_STMT_RETURN:
    case CEPHYR_NODE_STMT_EXPR:
        cephyr_ast_free_recursive(node->data.unop.operand);
        break;
    case CEPHYR_NODE_EXPR_CALL:
        cephyr_ast_free_recursive(node->data.call.callee);
        for (int i = 0; i < node->data.call.arg_count; i++)
            cephyr_ast_free_recursive(node->data.call.args[i]);
        free(node->data.call.args);
        break;
    case CEPHYR_NODE_EXPR_CAST:
        cephyr_type_free(node->data.cast.target_type);
        cephyr_ast_free_recursive(node->data.cast.expr);
        break;
    case CEPHYR_NODE_STMT_IF:
    case CEPHYR_NODE_EXPR_CONDITIONAL:
        cephyr_ast_free_recursive(node->data.if_stmt.cond);
        cephyr_ast_free_recursive(node->data.if_stmt.then_branch);
        cephyr_ast_free_recursive(node->data.if_stmt.else_branch);
        break;
    case CEPHYR_NODE_STMT_WHILE:
    case CEPHYR_NODE_STMT_DO_WHILE:
        cephyr_ast_free_recursive(node->data.while_stmt.cond);
        cephyr_ast_free_recursive(node->data.while_stmt.body);
        break;
    case CEPHYR_NODE_STMT_FOR:
        cephyr_ast_free_recursive(node->data.for_stmt.init);
        cephyr_ast_free_recursive(node->data.for_stmt.cond);
        cephyr_ast_free_recursive(node->data.for_stmt.incr);
        cephyr_ast_free_recursive(node->data.for_stmt.body);
        break;
    case CEPHYR_NODE_FUNC_DEF:
        cephyr_ast_free_recursive(node->data.func_def.body);
        break;
    case CEPHYR_NODE_BLOCK:
        for (int i = 0; i < node->data.block.stmt_count; i++)
            cephyr_ast_free_recursive(node->data.block.stmts[i]);
        free(node->data.block.stmts);
        break;
    case CEPHYR_NODE_EXPR_MEMBER:
        cephyr_ast_free_recursive(node->data.member.object);
        break;
    case CEPHYR_NODE_EXPR_ARRAY:
        cephyr_ast_free_recursive(node->data.array_access.array);
        cephyr_ast_free_recursive(node->data.array_access.index);
        break;
    case CEPHYR_NODE_STMT_SWITCH:
        cephyr_ast_free_recursive(node->data.switch_stmt.expr);
        cephyr_ast_free_recursive(node->data.switch_stmt.body);
        break;
    case CEPHYR_NODE_EXPR_COMPOUND_LITERAL:
        cephyr_type_free(node->data.compound_literal.lit_type);
        cephyr_ast_free_recursive(node->data.compound_literal.init);
        break;
    case CEPHYR_NODE_EXPR_INIT_LIST:
        for (int i = 0; i < node->data.init_list.init_count; i++)
            cephyr_ast_free_recursive(node->data.init_list.inits[i]);
        free(node->data.init_list.inits);
        break;
    case CEPHYR_NODE_EXPR_DESIGNATED_INIT:
        cephyr_ast_free_recursive(node->data.designated_init.init);
        break;
    case CEPHYR_NODE_EXPR_STRING_CONST:
        free(node->data.string_value);
        break;
    default:
        break;
    }
    /* Free attributes */
    for (int i = 0; i < node->attr_count; i++) {
        free((void *)node->attr_names[i]);
        if (node->attr_args[i]) {
            for (int j = 0; node->attr_args[i][j]; j++)
                free((void *)node->attr_args[i][j]);
            free(node->attr_args[i]);
        }
    }
    free((void *)node->attr_names);
    free(node->attr_args);
    free(node);
}

void cephyr_ast_free_recursive(cephyr_ast_node *node)
{
    if (!node) return;
    /* Free siblings first */
    cephyr_ast_node *next = node->next;
    cephyr_ast_node_free(node);
    cephyr_ast_free_recursive(next);
}

/* ---------- dump ---------- */

static const char *kind_name(cephyr_ast_node_kind k)
{
    switch (k) {
    case CEPHYR_NODE_PROGRAM: return "program";
    case CEPHYR_NODE_FUNC_DECL: return "func_decl";
    case CEPHYR_NODE_FUNC_DEF: return "func_def";
    case CEPHYR_NODE_VAR_DECL: return "var_decl";
    case CEPHYR_NODE_STRUCT_DECL: return "struct_decl";
    case CEPHYR_NODE_UNION_DECL: return "union_decl";
    case CEPHYR_NODE_ENUM_DECL: return "enum_decl";
    case CEPHYR_NODE_TYPEDEF_DECL: return "typedef_decl";
    case CEPHYR_NODE_BLOCK: return "block";
    case CEPHYR_NODE_STMT_RETURN: return "return";
    case CEPHYR_NODE_STMT_IF: return "if";
    case CEPHYR_NODE_STMT_WHILE: return "while";
    case CEPHYR_NODE_STMT_FOR: return "for";
    case CEPHYR_NODE_STMT_EXPR: return "expr_stmt";
    case CEPHYR_NODE_STMT_DECL: return "decl_stmt";
    case CEPHYR_NODE_EXPR_BINARY: return "binary";
    case CEPHYR_NODE_EXPR_UNARY: return "unary";
    case CEPHYR_NODE_EXPR_CALL: return "call";
    case CEPHYR_NODE_EXPR_CAST: return "cast";
    case CEPHYR_NODE_EXPR_IDENT: return "ident";
    case CEPHYR_NODE_EXPR_INT_CONST: return "int_const";
    case CEPHYR_NODE_EXPR_FLOAT_CONST: return "float_const";
    case CEPHYR_NODE_EXPR_STRING_CONST: return "string_const";
    case CEPHYR_NODE_EXPR_CHAR_CONST: return "char_const";
    case CEPHYR_NODE_EXPR_ASSIGN: return "assign";
    case CEPHYR_NODE_EXPR_CONDITIONAL: return "conditional";
    case CEPHYR_NODE_EXPR_MEMBER: return "member";
    case CEPHYR_NODE_EXPR_ARRAY: return "array_access";
    case CEPHYR_NODE_EXPR_SIZEOF: return "sizeof";
    default: return "?";
    }
}

static void dump_type(FILE *out, const cephyr_type *t)
{
    if (!t) { fputs("(null)", out); return; }
    const char *names[] = {
        "void","char","schar","uchar","short","ushort","int","uint",
        "long","ulong","llong","ullong","float","double","ldouble",
        "_Bool","ptr","array","func","struct","union","enum","typedef"
    };
    fprintf(out, "%s", (t->kind < 24) ? names[t->kind] : "?");
    if (t->name) fprintf(out, ":%s", t->name);
    if (t->inner) { fputs("(", out); dump_type(out, t->inner); fputs(")", out); }
}

void cephyr_ast_dump(FILE *out, const cephyr_ast_node *root)
{
    if (!out || !root) return;
    fprintf(out, "%s", kind_name(root->kind));
    if (root->name) fprintf(out, " '%s'", root->name);
    fputs(" : ", out);
    dump_type(out, root->type);
    fputc('\n', out);

    /* Dump children */
    switch (root->kind) {
    case CEPHYR_NODE_EXPR_BINARY:
    case CEPHYR_NODE_EXPR_ASSIGN:
        fprintf(out, "  op: %s\n", root->data.binop.op);
        cephyr_ast_dump(out, root->data.binop.lhs);
        cephyr_ast_dump(out, root->data.binop.rhs);
        break;
    case CEPHYR_NODE_EXPR_INT_CONST:
        fprintf(out, "  value: %ld\n", (long)root->data.int_value);
        break;
    case CEPHYR_NODE_EXPR_IDENT:
        fprintf(out, "  name: %s\n", root->name);
        break;
    default:
        break;
    }
}
