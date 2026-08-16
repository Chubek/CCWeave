/* Lowering adapter for the C frontend (§6.2).
 *
 * This file is the only place in CCWeave that includes a Tree-sitter
 * header or names CST node types. It walks the concrete syntax tree,
 * discards trivia, decides explicitly what to do with ERROR/MISSING
 * nodes, and emits calls into the imperative Kliche stereotype. */

#include "../ccw_swaff.h"
#include "ccw_kliche.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ccw_swaff_frontend {
    const char *name;
};

static const ccw_swaff_frontend g_frontend_c = { "c" };

const ccw_swaff_frontend *ccw_swaff_frontend_c(void)
{
    return &g_frontend_c;
}

const char *ccw_swaff_frontend_name(const ccw_swaff_frontend *fe)
{
    return fe ? fe->name : "";
}

#ifdef CCWEAVE_WITH_TREESITTER

#include <tree_sitter/api.h>

const TSLanguage *tree_sitter_c(void);

bool ccw_swaff_available(void) { return true; }

static void set_error(char **error_message, const char *msg)
{
    if (error_message == NULL) return;
    size_t n = strlen(msg) + 1u;
    char *p = (char *)malloc(n);
    if (p != NULL) memcpy(p, msg, n);
    *error_message = p;
}

/* CST text for a node, as a fresh NUL-terminated string. */
static char *node_text(TSNode node, const char *source)
{
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    if (end < start) return NULL;
    size_t n = (size_t)(end - start);
    char *out = (char *)malloc(n + 1u);
    if (out == NULL) return NULL;
    memcpy(out, source + start, n);
    out[n] = '\0';
    return out;
}

/* The declarator carries the function name; unwrap pointers/parens. */
static char *function_name_of(TSNode declarator, const char *source)
{
    for (;;) {
        const char *type = ts_node_type(declarator);
        if (strcmp(type, "identifier") == 0) return node_text(declarator, source);
        TSNode child = ts_node_child_by_field_name(declarator, "declarator", 10);
        if (ts_node_is_null(child)) {
            uint32_t n = ts_node_named_child_count(declarator);
            for (uint32_t i = 0; i < n; i++) {
                TSNode c = ts_node_named_child(declarator, i);
                if (strcmp(ts_node_type(c), "identifier") == 0)
                    return node_text(c, source);
            }
            return NULL;
        }
        declarator = child;
    }
}

/* Walks a function body, emitting imperative-stereotype constructs.
 * Trivia (comments) is discarded; only named nodes are considered. */
static void lower_body(ccw_ir *ir, ccw_node blk, TSNode body,
                       const char *source, ccw_swaff_report *report,
                       ccw_swaff_error_policy policy, bool *rejected)
{
    uint32_t n = ts_node_named_child_count(body);
    for (uint32_t i = 0; i < n && !*rejected; i++) {
        TSNode child = ts_node_named_child(body, i);
        const char *type = ts_node_type(child);

        if (strcmp(type, "comment") == 0) continue;   /* trivia: discarded */

        if (ts_node_is_error(child) || strcmp(type, "ERROR") == 0) {
            report->error_nodes++;
            if (policy == CCW_SWAFF_REJECT_ON_ERROR) { *rejected = true; return; }
            report->recovered_subtrees++;
            continue;
        }
        if (ts_node_is_missing(child)) {
            report->missing_nodes++;
            if (policy == CCW_SWAFF_REJECT_ON_ERROR) { *rejected = true; return; }
            report->recovered_subtrees++;
            continue;
        }

        if (strcmp(type, "declaration") == 0) {
            TSNode decl = ts_node_child_by_field_name(child, "declarator", 10);
            char *name = ts_node_is_null(decl) ? NULL : function_name_of(decl, source);
            if (name != NULL) {
                ccw_kliche_local_alloc(ir, blk, name, CCW_TY_I64);
                free(name);
            }
        } else if (strcmp(type, "return_statement") == 0) {
            ccw_kliche_jump(ir, blk, "exit");
        } else if (strcmp(type, "if_statement") == 0) {
            ccw_kliche_branch_if(ir, blk, "cond", "then", "else");
        } else if (strcmp(type, "compound_statement") == 0) {
            lower_body(ir, blk, child, source, report, policy, rejected);
        }
    }
}

ccw_ir *ccw_swaff_lower(const ccw_swaff_frontend *fe,
                        const char *source, size_t source_len,
                        const char *module_name, ccw_profile profile,
                        ccw_swaff_error_policy policy,
                        ccw_swaff_report *report, char **error_message)
{
    if (error_message) *error_message = NULL;
    ccw_swaff_report local;
    memset(&local, 0, sizeof(local));
    if (fe == NULL || source == NULL) {
        set_error(error_message, "swaff: no frontend or source");
        return NULL;
    }

    TSParser *parser = ts_parser_new();
    if (parser == NULL || !ts_parser_set_language(parser, tree_sitter_c())) {
        if (parser) ts_parser_delete(parser);
        set_error(error_message, "swaff: could not initialize the C grammar");
        return NULL;
    }
    TSTree *tree = ts_parser_parse_string(parser, NULL, source, (uint32_t)source_len);
    if (tree == NULL) {
        ts_parser_delete(parser);
        set_error(error_message, "swaff: parse produced no tree");
        return NULL;
    }

    TSNode root = ts_tree_root_node(tree);
    ccw_ir *ir = ccw_ir_module_create(module_name, profile);
    bool rejected = false;

    uint32_t n = ts_node_named_child_count(root);
    for (uint32_t i = 0; i < n && !rejected; i++) {
        TSNode child = ts_node_named_child(root, i);
        const char *type = ts_node_type(child);

        if (strcmp(type, "comment") == 0) continue;
        if (ts_node_is_error(child) || strcmp(type, "ERROR") == 0) {
            local.error_nodes++;
            if (policy == CCW_SWAFF_REJECT_ON_ERROR) { rejected = true; break; }
            local.recovered_subtrees++;
            continue;
        }
        if (ts_node_is_missing(child)) {
            local.missing_nodes++;
            if (policy == CCW_SWAFF_REJECT_ON_ERROR) { rejected = true; break; }
            local.recovered_subtrees++;
            continue;
        }
        if (strcmp(type, "function_definition") != 0) continue;

        TSNode declarator = ts_node_child_by_field_name(child, "declarator", 10);
        char *name = ts_node_is_null(declarator) ? NULL
                                                 : function_name_of(declarator, source);
        if (name == NULL) continue;

        ccw_node fn = ccw_ir_function_add(ir, name, CCW_TY_I64);
        free(name);
        ccw_node entry = ccw_ir_block_add(ir, fn, "entry");
        local.functions_lowered++;

        TSNode body = ts_node_child_by_field_name(child, "body", 4);
        if (!ts_node_is_null(body))
            lower_body(ir, entry, body, source, &local, policy, &rejected);

        /* Every block ends with a terminator, even for an empty body. */
        if (ccw_ir_block_instr_count(ir, entry) == 0)
            ccw_kliche_jump(ir, entry, "exit");
    }

    ts_tree_delete(tree);
    ts_parser_delete(parser);

    if (rejected) {
        snprintf(local.message, sizeof(local.message),
                 "rejected: %d ERROR and %d MISSING nodes in the CST",
                 local.error_nodes, local.missing_nodes);
        if (report != NULL) *report = local;
        set_error(error_message, local.message);
        ccw_ir_module_destroy(ir);
        return NULL;
    }
    if (report != NULL) *report = local;
    return ir;
}

#else  /* Tree-sitter frontends not compiled in */

bool ccw_swaff_available(void) { return false; }

ccw_ir *ccw_swaff_lower(const ccw_swaff_frontend *fe,
                        const char *source, size_t source_len,
                        const char *module_name, ccw_profile profile,
                        ccw_swaff_error_policy policy,
                        ccw_swaff_report *report, char **error_message)
{
    (void)fe; (void)source; (void)source_len; (void)module_name;
    (void)profile; (void)policy; (void)report;
    if (error_message != NULL) {
        static const char msg[] =
            "swaff: built without Tree-sitter (-DCCWEAVE_ENABLE_TREESITTER=ON)";
        char *p = (char *)malloc(sizeof(msg));
        if (p != NULL) memcpy(p, msg, sizeof(msg));
        *error_message = p;
    }
    return NULL;
}

#endif
