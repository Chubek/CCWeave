/* Lua lowering adapter for Swaff (§6.2).
 *
 * Tree-sitter node kinds are intentionally confined to this file.  The
 * adapter handles the scalar/control-flow subset that has a direct mapping
 * to the core imperative Kliche stereotype; tables, methods, varargs, and
 * multiple-result expressions are rejected explicitly because the v0.1 core
 * IR has no aggregate or tuple value representation.
 */

#include "ccw_swaff_internal.h"
#include "ccw_kliche.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const ccw_swaff_frontend g_frontend_lua = { "lua" };

const ccw_swaff_frontend *ccw_swaff_frontend_lua(void)
{
    return &g_frontend_lua;
}

static char *lua_strdup(const char *s)
{
    if (s == NULL) return NULL;
    size_t n = strlen(s) + 1u;
    char *copy = (char *)malloc(n);
    if (copy != NULL) memcpy(copy, s, n);
    return copy;
}

static void lua_set_error(char **error_message, const char *message)
{
    if (error_message != NULL) *error_message = lua_strdup(message);
}

#ifdef CCWEAVE_WITH_TREESITTER

#include <tree_sitter/api.h>
#include <tree_sitter/tree-sitter-lua.h>

#define CCW_LUA_MAX_NAMES 128
#define CCW_LUA_MAX_ARGS   32

typedef struct {
    ccw_ir *ir;
    ccw_node fn;
    const char *source;
    size_t source_len;
    ccw_swaff_error_policy policy;
    ccw_swaff_report *report;
    bool failed;
    bool rejected;
    char failure[256];
    unsigned temp_index;
    unsigned block_index;
    char *locals[CCW_LUA_MAX_NAMES];
    int local_count;
} ccw_lua_lower;

static TSNode null_node(void)
{
    TSNode node = { { 0, 0, 0, 0 }, NULL, NULL };
    return node;
}

static bool node_is(TSNode node, const char *type)
{
    return !ts_node_is_null(node) && strcmp(ts_node_type(node), type) == 0;
}

static TSNode field(TSNode node, const char *name)
{
    return ts_node_child_by_field_name(node, name, (uint32_t)strlen(name));
}

static TSNode first_named_child(TSNode node)
{
    return ts_node_named_child_count(node) > 0
        ? ts_node_named_child(node, 0) : null_node();
}

static char *node_text(TSNode node, const char *source, size_t source_len)
{
    uint32_t start, end;
    size_t n;
    char *text;
    if (ts_node_is_null(node)) return NULL;
    start = ts_node_start_byte(node);
    end = ts_node_end_byte(node);
    if (end < start || (size_t)end > source_len) return NULL;
    n = (size_t)(end - start);
    text = (char *)malloc(n + 1u);
    if (text == NULL) return NULL;
    memcpy(text, source + start, n);
    text[n] = '\0';
    return text;
}

static void lower_fail(ccw_lua_lower *ctx, const char *message)
{
    if (ctx->failed) return;
    ctx->failed = true;
    snprintf(ctx->failure, sizeof(ctx->failure), "%s", message);
}

static bool malformed_node(ccw_lua_lower *ctx, TSNode node)
{
    if (!ts_node_is_error(node) && !ts_node_is_missing(node) &&
        !node_is(node, "ERROR"))
        return false;
    if (ctx->policy == CCW_SWAFF_REJECT_ON_ERROR)
        ctx->rejected = true;
    else
        ctx->report->recovered_subtrees++;
    return true;
}

static bool scan_errors(TSNode node, ccw_swaff_report *report)
{
    bool found = false;
    uint32_t count;
    if (ts_node_is_error(node) || ts_node_is_missing(node) ||
        node_is(node, "ERROR")) {
        if (ts_node_is_missing(node)) report->missing_nodes++;
        else report->error_nodes++;
        found = true;
    }
    count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; i++)
        if (scan_errors(ts_node_child(node, i), report)) found = true;
    return found;
}

static char *new_temp(ccw_lua_lower *ctx)
{
    char name[40];
    snprintf(name, sizeof(name), "lua.tmp.%u", ctx->temp_index++);
    return lua_strdup(name);
}

static char *new_block_name(ccw_lua_lower *ctx, const char *stem)
{
    char name[48];
    snprintf(name, sizeof(name), "lua.%s.%u", stem, ctx->block_index++);
    return lua_strdup(name);
}

static bool block_terminated(const ccw_ir *ir, ccw_node block)
{
    int count = ccw_ir_block_instr_count(ir, block);
    const char *opcode;
    if (count == 0) return false;
    opcode = ccw_ir_instr_opcode(
        ir, ccw_ir_block_instr_ref(ir, block, count - 1));
    return opcode != NULL &&
           (strcmp(opcode, "ret") == 0 || strcmp(opcode, "br") == 0 ||
            strcmp(opcode, "br.cond") == 0);
}

static void clear_locals(ccw_lua_lower *ctx)
{
    for (int i = 0; i < ctx->local_count; i++) free(ctx->locals[i]);
    ctx->local_count = 0;
}

static bool is_local(const ccw_lua_lower *ctx, const char *name)
{
    for (int i = ctx->local_count - 1; i >= 0; i--)
        if (strcmp(ctx->locals[i], name) == 0) return true;
    return false;
}

static bool add_local(ccw_lua_lower *ctx, const char *name)
{
    if (ctx->local_count >= CCW_LUA_MAX_NAMES) {
        lower_fail(ctx, "swaff Lua: too many local declarations");
        return false;
    }
    ctx->locals[ctx->local_count] = lua_strdup(name);
    if (ctx->locals[ctx->local_count] == NULL) {
        lower_fail(ctx, "swaff Lua: out of memory");
        return false;
    }
    ctx->local_count++;
    return true;
}

static char *lower_expression(ccw_lua_lower *ctx, ccw_node block, TSNode expr);
static void lower_statement(ccw_lua_lower *ctx, ccw_node *block, TSNode node);

static char *lower_identifier(ccw_lua_lower *ctx, ccw_node block, TSNode node)
{
    char *name = node_text(node, ctx->source, ctx->source_len);
    char *temp;
    if (name == NULL) {
        lower_fail(ctx, "swaff Lua: could not read identifier");
        return NULL;
    }
    if (!is_local(ctx, name)) return name;
    temp = new_temp(ctx);
    if (temp == NULL ||
        ccw_kliche_local_load(ctx->ir, block, temp, name, CCW_TY_I64) == 0) {
        free(name);
        free(temp);
        lower_fail(ctx, "swaff Lua: could not lower local load");
        return NULL;
    }
    free(name);
    return temp;
}

static char *lower_number(ccw_lua_lower *ctx, ccw_node block, TSNode node)
{
    char *text = node_text(node, ctx->source, ctx->source_len);
    char *end = NULL;
    long long value;
    char *temp;
    if (text == NULL) {
        lower_fail(ctx, "swaff Lua: could not read number");
        return NULL;
    }
    errno = 0;
    value = strtoll(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0') {
        free(text);
        lower_fail(ctx, "swaff Lua: only integer numeric literals are supported");
        return NULL;
    }
    free(text);
    temp = new_temp(ctx);
    if (temp == NULL ||
        ccw_kliche_int_const(ctx->ir, block, temp, (int64_t)value) == 0) {
        free(temp);
        lower_fail(ctx, "swaff Lua: could not lower number");
        return NULL;
    }
    return temp;
}

static const char *binary_opcode(const char *op)
{
    static const struct { const char *source; const char *ir; } map[] = {
        { "+", "iadd" }, { "-", "isub" }, { "*", "imul" },
        { "/", "idiv" }, { "//", "idiv" }, { "%", "irem" },
        { "..", "str.concat" }, { "==", "icmp.eq" },
        { "~=", "icmp.ne" }, { "<", "icmp.lt" }, { "<=", "icmp.le" },
        { ">", "icmp.gt" }, { ">=", "icmp.ge" }, { "&", "iand" },
        { "|", "ior" }, { "~", "ixor" }, { "<<", "shl" }, { ">>", "shr" },
        { "and", "logic.and" }, { "or", "logic.or" }
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (strcmp(op, map[i].source) == 0) return map[i].ir;
    return NULL;
}

static char *operator_between(ccw_lua_lower *ctx, TSNode left, TSNode right)
{
    uint32_t start = ts_node_end_byte(left);
    uint32_t end = ts_node_start_byte(right);
    size_t begin;
    size_t finish;
    char *op;
    while (start < end && (ctx->source[start] == ' ' ||
                           ctx->source[start] == '\t' ||
                           ctx->source[start] == '\r' ||
                           ctx->source[start] == '\n')) start++;
    while (end > start && (ctx->source[end - 1] == ' ' ||
                           ctx->source[end - 1] == '\t' ||
                           ctx->source[end - 1] == '\r' ||
                           ctx->source[end - 1] == '\n')) end--;
    begin = (size_t)start;
    finish = (size_t)end;
    if (finish <= begin || finish > ctx->source_len) return NULL;
    op = (char *)malloc(finish - begin + 1u);
    if (op == NULL) return NULL;
    memcpy(op, ctx->source + begin, finish - begin);
    op[finish - begin] = '\0';
    return op;
}

static char *lower_binary(ccw_lua_lower *ctx, ccw_node block, TSNode expr)
{
    TSNode left = null_node(), right = null_node();
    uint32_t count = ts_node_named_child_count(expr);
    char *op_text;
    const char *opcode;
    char *lhs;
    char *rhs;
    char *dest;
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(expr, i);
        if (node_is(child, "left_paren") || node_is(child, "right_paren"))
            continue;
        if (ts_node_is_null(left)) left = child;
        else if (ts_node_is_null(right)) { right = child; break; }
    }
    if (ts_node_is_null(left) || ts_node_is_null(right)) {
        lower_fail(ctx, "swaff Lua: malformed binary operation");
        return NULL;
    }
    op_text = operator_between(ctx, left, right);
    opcode = op_text != NULL ? binary_opcode(op_text) : NULL;
    if (opcode == NULL || strcmp(opcode, "str.concat") == 0) {
        free(op_text);
        lower_fail(ctx, "swaff Lua: unsupported binary operator");
        return NULL;
    }
    lhs = lower_expression(ctx, block, left);
    rhs = lower_expression(ctx, block, right);
    dest = new_temp(ctx);
    if (lhs == NULL || rhs == NULL || dest == NULL ||
        ccw_kliche_binary(ctx->ir, block, opcode, dest, lhs, rhs,
            strncmp(opcode, "icmp.", 5) == 0 ? CCW_TY_I1 : CCW_TY_I64) == 0) {
        free(lhs); free(rhs); free(dest); free(op_text);
        if (!ctx->failed) lower_fail(ctx, "swaff Lua: could not lower binary operation");
        return NULL;
    }
    free(lhs); free(rhs); free(op_text);
    return dest;
}

static char *lower_unary(ccw_lua_lower *ctx, ccw_node block, TSNode expr)
{
    TSNode argument = first_named_child(expr);
    uint32_t start;
    uint32_t end;
    char op[8];
    size_t n;
    const char *opcode = NULL;
    char *operand;
    char *dest;
    if (ts_node_is_null(argument)) {
        lower_fail(ctx, "swaff Lua: malformed unary operation");
        return NULL;
    }
    start = ts_node_start_byte(expr);
    end = ts_node_start_byte(argument);
    while (start < end && (ctx->source[start] == ' ' ||
                           ctx->source[start] == '\t')) start++;
    while (end > start && (ctx->source[end - 1] == ' ' ||
                           ctx->source[end - 1] == '\t')) end--;
    n = end > start ? (size_t)(end - start) : 0;
    if (n >= sizeof(op)) n = sizeof(op) - 1u;
    memcpy(op, ctx->source + start, n);
    op[n] = '\0';
    if (strcmp(op, "-") == 0) opcode = "ineg";
    else if (strcmp(op, "~") == 0) opcode = "inot";
    else if (strcmp(op, "not") == 0) opcode = "logic.not";
    else if (strcmp(op, "#") == 0) opcode = "iabs";
    if (opcode == NULL) {
        lower_fail(ctx, "swaff Lua: unsupported unary operator");
        return NULL;
    }
    operand = lower_expression(ctx, block, argument);
    dest = new_temp(ctx);
    if (operand == NULL || dest == NULL ||
        ccw_kliche_unary(ctx->ir, block, opcode, dest, operand,
            strcmp(opcode, "logic.not") == 0 ? CCW_TY_I1 : CCW_TY_I64) == 0) {
        free(operand); free(dest);
        if (!ctx->failed) lower_fail(ctx, "swaff Lua: could not lower unary operation");
        return NULL;
    }
    free(operand);
    return dest;
}

static char *lower_call(ccw_lua_lower *ctx, ccw_node block, TSNode expr)
{
    TSNode prefix = field(expr, "prefix");
    TSNode args_node = field(expr, "args");
    char *callee = NULL;
    const char *args[CCW_LUA_MAX_ARGS];
    char *owned[CCW_LUA_MAX_ARGS];
    int arg_count = 0;
    char *dest;
    uint32_t count;
    memset(owned, 0, sizeof(owned));

    if (!ts_node_is_null(prefix)) {
        TSNode child = first_named_child(prefix);
        if (node_is(child, "identifier"))
            callee = node_text(child, ctx->source, ctx->source_len);
    }
    if (callee == NULL) {
        lower_fail(ctx, "swaff Lua: only direct identifier calls are supported");
        return NULL;
    }
    if (node_is(args_node, "function_arguments")) {
        count = ts_node_named_child_count(args_node);
        for (uint32_t i = 0; i < count; i++) {
            if (arg_count >= CCW_LUA_MAX_ARGS) {
                lower_fail(ctx, "swaff Lua: too many call arguments");
                break;
            }
            owned[arg_count] = lower_expression(
                ctx, block, ts_node_named_child(args_node, i));
            if (owned[arg_count] == NULL) break;
            args[arg_count] = owned[arg_count];
            arg_count++;
        }
    } else if (!ts_node_is_null(args_node)) {
        lower_fail(ctx, "swaff Lua: string/table call arguments are unsupported");
    }
    dest = ctx->failed ? NULL : new_temp(ctx);
    if (dest == NULL ||
        ccw_kliche_call(ctx->ir, block, dest, callee, args, arg_count,
                        CCW_TY_I64) == 0) {
        free(dest);
        dest = NULL;
        if (!ctx->failed) lower_fail(ctx, "swaff Lua: could not lower function call");
    }
    for (int i = 0; i < arg_count; i++) free(owned[i]);
    free(callee);
    return dest;
}

static char *lower_expression(ccw_lua_lower *ctx, ccw_node block, TSNode expr)
{
    const char *type;
    if (ctx->failed || ctx->rejected || ts_node_is_null(expr)) return NULL;
    if (malformed_node(ctx, expr)) return NULL;
    type = ts_node_type(expr);
    if (strcmp(type, "identifier") == 0) return lower_identifier(ctx, block, expr);
    if (strcmp(type, "number") == 0) return lower_number(ctx, block, expr);
    if (strcmp(type, "boolean") == 0 || strcmp(type, "nil") == 0) {
        char *text = node_text(expr, ctx->source, ctx->source_len);
        char *dest = new_temp(ctx);
        int64_t value = text != NULL && strcmp(text, "true") == 0 ? 1 : 0;
        free(text);
        if (dest == NULL ||
            ccw_kliche_int_const(ctx->ir, block, dest, value) == 0) {
            free(dest);
            lower_fail(ctx, "swaff Lua: could not lower boolean/nil");
            return NULL;
        }
        return dest;
    }
    if (strcmp(type, "binary_operation") == 0) return lower_binary(ctx, block, expr);
    if (strcmp(type, "unary_operation") == 0) return lower_unary(ctx, block, expr);
    if (strcmp(type, "function_call") == 0) return lower_call(ctx, block, expr);
    if (strcmp(type, "left_paren") == 0 || strcmp(type, "right_paren") == 0)
        return lower_expression(ctx, block, first_named_child(expr));
    ctx->report->unsupported_nodes++;
    lower_fail(ctx, "swaff Lua: unsupported expression");
    return NULL;
}

static void lower_return(ccw_lua_lower *ctx, ccw_node block, TSNode node)
{
    TSNode value = first_named_child(node);
    char *reg = ts_node_is_null(value) ? NULL : lower_expression(ctx, block, value);
    if (!ctx->failed && ccw_kliche_return(ctx->ir, block, reg) == 0)
        lower_fail(ctx, "swaff Lua: could not lower return");
    free(reg);
}

static void lower_declaration(ccw_lua_lower *ctx, ccw_node block, TSNode node)
{
    uint32_t count = ts_node_child_count(node);
    int names = 0;
    int values = 0;
    TSNode name_nodes[CCW_LUA_MAX_ARGS];
    TSNode value_nodes[CCW_LUA_MAX_ARGS];
    memset(name_nodes, 0, sizeof(name_nodes));
    memset(value_nodes, 0, sizeof(value_nodes));
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(node, i);
        const char *field_name = ts_node_field_name_for_child(node, i);
        if (field_name != NULL && strcmp(field_name, "name") == 0 &&
            names < CCW_LUA_MAX_ARGS)
            name_nodes[names++] = child;
        else if (field_name != NULL && strcmp(field_name, "value") == 0 &&
                 values < CCW_LUA_MAX_ARGS)
            value_nodes[values++] = child;
    }
    for (int i = 0; i < names && !ctx->failed; i++) {
        TSNode id = node_is(name_nodes[i], "variable_declarator")
            ? first_named_child(name_nodes[i]) : name_nodes[i];
        char *name = node_text(id, ctx->source, ctx->source_len);
        char *initial = NULL;
        if (name == NULL || !add_local(ctx, name) ||
            ccw_kliche_local_alloc(ctx->ir, block, name, CCW_TY_I64) == 0) {
            free(name);
            lower_fail(ctx, "swaff Lua: could not lower local declaration");
            break;
        }
        if (i < values) initial = lower_expression(ctx, block, value_nodes[i]);
        if (initial != NULL &&
            ccw_kliche_local_store(ctx->ir, block, name, initial) == 0)
            lower_fail(ctx, "swaff Lua: could not lower local initializer");
        free(initial);
        free(name);
        ctx->report->declarations_lowered++;
    }
}

static void lower_body(ccw_lua_lower *ctx, ccw_node *block, TSNode body)
{
    uint32_t count = ts_node_named_child_count(body);
    for (uint32_t i = 0; i < count && !ctx->failed && !ctx->rejected; i++)
        lower_statement(ctx, block, ts_node_named_child(body, i));
}

static void lower_if(ccw_lua_lower *ctx, ccw_node *block, TSNode node)
{
    TSNode condition = null_node();
    char *cond;
    char *then_name;
    char *else_name;
    char *merge_name;
    ccw_node then_block, else_block, merge_block;
    uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        if (ts_node_is_null(condition) &&
            (node_is(child, "identifier") || node_is(child, "binary_operation") ||
             node_is(child, "unary_operation") || node_is(child, "number") ||
             node_is(child, "boolean") || node_is(child, "nil")))
            condition = child;
    }
    cond = lower_expression(ctx, *block, condition);
    then_name = new_block_name(ctx, "then");
    else_name = new_block_name(ctx, "else");
    merge_name = new_block_name(ctx, "merge");
    if (cond == NULL || then_name == NULL || else_name == NULL ||
        merge_name == NULL) {
        free(cond); free(then_name); free(else_name); free(merge_name);
        return;
    }
    then_block = ccw_ir_block_add(ctx->ir, ctx->fn, then_name);
    else_block = ccw_ir_block_add(ctx->ir, ctx->fn, else_name);
    merge_block = ccw_ir_block_add(ctx->ir, ctx->fn, merge_name);
    if (then_block == 0 || else_block == 0 || merge_block == 0 ||
        ccw_kliche_branch_if(ctx->ir, *block, cond, then_name, else_name) == 0) {
        lower_fail(ctx, "swaff Lua: could not construct if blocks");
    } else {
        bool in_then = false;
        bool in_else = false;
        for (uint32_t i = 0; i < count && !ctx->failed; i++) {
            TSNode child = ts_node_named_child(node, i);
            if (node_is(child, "if_then")) {
                in_then = true;
                in_else = false;
                continue;
            }
            if (node_is(child, "if_else")) {
                in_then = false;
                in_else = true;
                continue;
            }
            if (node_is(child, "if_elseif")) {
                ctx->report->unsupported_nodes++;
                lower_fail(ctx, "swaff Lua: elseif chains are unsupported");
                continue;
            }
            if (node_is(child, "if_end")) {
                in_then = false;
                in_else = false;
                continue;
            }
            if (in_then && child.id != condition.id)
                lower_statement(ctx, &then_block, child);
            else if (in_else)
                lower_statement(ctx, &else_block, child);
        }
        if (!block_terminated(ctx->ir, then_block))
            ccw_kliche_jump(ctx->ir, then_block, merge_name);
        if (!block_terminated(ctx->ir, else_block))
            ccw_kliche_jump(ctx->ir, else_block, merge_name);
        *block = merge_block;
    }
    free(cond); free(then_name); free(else_name); free(merge_name);
}

static void lower_statement(ccw_lua_lower *ctx, ccw_node *block, TSNode node)
{
    const char *type;
    if (ctx->failed || ctx->rejected || ts_node_is_null(node)) return;
    if (malformed_node(ctx, node)) return;
    if (node_is(node, "comment") || node_is(node, "shebang")) return;
    type = ts_node_type(node);
    if (strcmp(type, "variable_declaration") == 0)
        lower_declaration(ctx, *block, node);
    else if (strcmp(type, "return_statement") == 0)
        lower_return(ctx, *block, node);
    else if (strcmp(type, "function_call") == 0) {
        char *unused = lower_expression(ctx, *block, node);
        free(unused);
    } else if (strcmp(type, "if_statement") == 0)
        lower_if(ctx, block, node);
    else if (strcmp(type, "do_statement") == 0)
        lower_body(ctx, block, node);
    else {
        ctx->report->unsupported_nodes++;
        lower_fail(ctx, "swaff Lua: unsupported statement");
    }
    ctx->report->statements_lowered++;
}

static void lower_function(ccw_lua_lower *ctx, TSNode node)
{
    TSNode name_node = field(node, "name");
    TSNode params = null_node();
    TSNode body = null_node();
    char *name = node_text(name_node, ctx->source, ctx->source_len);
    uint32_t count;
    if (name == NULL) {
        lower_fail(ctx, "swaff Lua: function has no name");
        return;
    }
    ctx->fn = ccw_ir_function_add(ctx->ir, name, CCW_TY_I64);
    free(name);
    if (ctx->fn == 0) {
        lower_fail(ctx, "swaff Lua: could not create function");
        return;
    }
    clear_locals(ctx);
    ctx->temp_index = 0;
    ctx->block_index = 0;
    count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        if (node_is(child, "parameter_list")) params = child;
        if (node_is(child, "function_body")) body = child;
    }
    if (!ts_node_is_null(params)) {
        count = ts_node_named_child_count(params);
        for (uint32_t i = 0; i < count; i++) {
            TSNode parameter = ts_node_named_child(params, i);
            char *parameter_name;
            if (!node_is(parameter, "identifier")) continue;
            parameter_name = node_text(parameter, ctx->source, ctx->source_len);
            if (parameter_name == NULL || !add_local(ctx, parameter_name) ||
                ccw_ir_function_add_param(ctx->ir, ctx->fn, CCW_TY_I64,
                                           parameter_name) != CCW_OK)
                lower_fail(ctx, "swaff Lua: could not lower function parameter");
            free(parameter_name);
        }
    }
    {
        ccw_node block = ccw_ir_block_add(ctx->ir, ctx->fn, "entry");
        if (block == 0) lower_fail(ctx, "swaff Lua: could not create entry block");
        else {
            lower_body(ctx, &block, body);
            if (!ctx->failed && !block_terminated(ctx->ir, block))
                ccw_kliche_return(ctx->ir, block, NULL);
        }
    }
    if (!ctx->failed) ctx->report->functions_lowered++;
    clear_locals(ctx);
}

ccw_ir *ccw_swaff_lower_lua(const ccw_swaff_frontend *fe,
                            const char *source, size_t source_len,
                            const char *module_name, ccw_profile profile,
                            ccw_swaff_error_policy policy,
                            ccw_swaff_report *report, char **error_message)
{
    ccw_swaff_report local;
    TSParser *parser;
    TSTree *tree;
    TSNode root;
    bool has_errors;
    ccw_ir *ir;
    ccw_lua_lower ctx;
    if (error_message != NULL) *error_message = NULL;
    memset(&local, 0, sizeof(local));
    if (report != NULL) memset(report, 0, sizeof(*report));
    if (fe != &g_frontend_lua || source == NULL || module_name == NULL) {
        lua_set_error(error_message, "swaff Lua: invalid frontend, source, or module name");
        return NULL;
    }
    if (source_len > UINT32_MAX) {
        lua_set_error(error_message, "swaff Lua: source is too large for Tree-sitter");
        return NULL;
    }
    parser = ts_parser_new();
    if (parser == NULL || !ts_parser_set_language(parser, tree_sitter_lua())) {
        if (parser != NULL) ts_parser_delete(parser);
        lua_set_error(error_message, "swaff Lua: vendored grammar is ABI-incompatible");
        return NULL;
    }
    tree = ts_parser_parse_string(parser, NULL, source, (uint32_t)source_len);
    if (tree == NULL) {
        ts_parser_delete(parser);
        lua_set_error(error_message, "swaff Lua: parser produced no syntax tree");
        return NULL;
    }
    root = ts_tree_root_node(tree);
    has_errors = scan_errors(root, &local);
    if (has_errors && policy == CCW_SWAFF_REJECT_ON_ERROR) {
        snprintf(local.message, sizeof(local.message),
                 "swaff Lua: rejected CST with %d ERROR and %d MISSING nodes",
                 local.error_nodes, local.missing_nodes);
        if (report != NULL) *report = local;
        lua_set_error(error_message, local.message);
        ts_tree_delete(tree);
        ts_parser_delete(parser);
        return NULL;
    }
    ir = ccw_ir_module_create(module_name, profile);
    if (ir == NULL) {
        ts_tree_delete(tree); ts_parser_delete(parser);
        lua_set_error(error_message, "swaff Lua: out of memory");
        return NULL;
    }
    memset(&ctx, 0, sizeof(ctx));
    ctx.ir = ir; ctx.source = source; ctx.source_len = source_len;
    ctx.policy = policy; ctx.report = &local;
    for (uint32_t i = 0; i < ts_node_named_child_count(root); i++) {
        TSNode child = ts_node_named_child(root, i);
        if (ctx.failed || ctx.rejected) break;
        if (node_is(child, "shebang") || node_is(child, "comment")) continue;
        if (malformed_node(&ctx, child)) continue;
        if (node_is(child, "function_statement")) lower_function(&ctx, child);
        else if (node_is(child, "variable_declaration") ||
                 node_is(child, "function_call") ||
                 node_is(child, "if_statement") ||
                 node_is(child, "do_statement"))
            local.unsupported_nodes++;
        else if (!node_is(child, "documentation_brief") &&
                 !node_is(child, "documentation_class") &&
                 !node_is(child, "documentation_command") &&
                 !node_is(child, "documentation_config") &&
                 !node_is(child, "documentation_tag"))
            local.unsupported_nodes++;
    }
    clear_locals(&ctx);
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    if (ctx.failed || ctx.rejected) {
        const char *message = ctx.failed ? ctx.failure :
            "swaff Lua: rejected malformed subtree";
        snprintf(local.message, sizeof(local.message), "%s", message);
        if (report != NULL) *report = local;
        lua_set_error(error_message, message);
        ccw_ir_module_destroy(ir);
        return NULL;
    }
    if (has_errors)
        snprintf(local.message, sizeof(local.message),
                 "swaff Lua: recovered %d malformed subtrees",
                 local.recovered_subtrees);
    if (report != NULL) *report = local;
    return ir;
}

#else

ccw_ir *ccw_swaff_lower_lua(const ccw_swaff_frontend *fe,
                            const char *source, size_t source_len,
                            const char *module_name, ccw_profile profile,
                            ccw_swaff_error_policy policy,
                            ccw_swaff_report *report, char **error_message)
{
    (void)fe; (void)source; (void)source_len; (void)module_name;
    (void)profile; (void)policy; (void)report;
    lua_set_error(error_message,
                  "swaff Lua: built without vendored Tree-sitter support");
    return NULL;
}

#endif
