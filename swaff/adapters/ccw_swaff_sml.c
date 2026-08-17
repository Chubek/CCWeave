/* Standard ML lowering adapter for Swaff (§6.2).
 *
 * tree-sitter-sml deliberately leaves fixity resolution to consumers, so
 * infix source forms arrive as application CSTs. This adapter normalizes the
 * supported Basis operators before emitting profile-independent functional
 * and imperative Kliche construction patterns. Tree-sitter node names remain
 * confined to this translation unit. */

#include "ccw_swaff_internal.h"
#include "ccw_kliche.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const ccw_swaff_frontend g_frontend_sml = { "sml" };

const ccw_swaff_frontend *ccw_swaff_frontend_sml(void)
{
    return &g_frontend_sml;
}

static char *sml_strdup(const char *s)
{
    if (s == NULL) return NULL;
    size_t size = strlen(s) + 1u;
    char *copy = (char *)malloc(size);
    if (copy != NULL) memcpy(copy, s, size);
    return copy;
}

static void sml_set_error(char **error_message, const char *message)
{
    if (error_message != NULL) *error_message = sml_strdup(message);
}

#ifdef CCWEAVE_WITH_TREESITTER

#include <tree_sitter/api.h>
#include <tree_sitter/tree-sitter-sml.h>

#define CCW_SML_MAX_NAMES 128
#define CCW_SML_MAX_ARGS   32

typedef struct {
    char *name;
    char *reg;
} ccw_sml_alias;

typedef struct {
    ccw_ir           *ir;
    ccw_node          fn;
    const char       *source;
    size_t            source_len;
    ccw_swaff_report *report;
    bool              failed;
    char              failure[256];
    unsigned          temp_index;
    unsigned          block_index;
    char             *parameters[CCW_SML_MAX_NAMES];
    int               parameter_count;
    ccw_sml_alias     aliases[CCW_SML_MAX_NAMES];
    int               alias_count;
} ccw_sml_lower;

static bool node_is(TSNode node, const char *type)
{
    return !ts_node_is_null(node) && strcmp(ts_node_type(node), type) == 0;
}

static TSNode null_node(void)
{
    TSNode node = { { 0, 0, 0, 0 }, NULL, NULL };
    return node;
}

static TSNode field(TSNode node, const char *name)
{
    return ts_node_child_by_field_name(node, name, (uint32_t)strlen(name));
}

static TSNode first_named_child(TSNode node)
{
    return ts_node_named_child_count(node) > 0
        ? ts_node_named_child(node, 0)
        : null_node();
}

static char *node_text(TSNode node, const char *source, size_t source_len)
{
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    if (ts_node_is_null(node) || end < start || (size_t)end > source_len)
        return NULL;
    size_t length = (size_t)(end - start);
    char *text = (char *)malloc(length + 1u);
    if (text == NULL) return NULL;
    memcpy(text, source + start, length);
    text[length] = '\0';
    return text;
}

static void lower_fail(ccw_sml_lower *ctx, const char *message)
{
    if (ctx->failed) return;
    ctx->failed = true;
    snprintf(ctx->failure, sizeof(ctx->failure), "%s", message);
}

static bool scan_errors(TSNode node, ccw_swaff_report *report)
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

static bool subtree_is_malformed(TSNode node)
{
    if (ts_node_is_error(node) || ts_node_is_missing(node) ||
        node_is(node, "ERROR"))
        return true;
    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; i++)
        if (subtree_is_malformed(ts_node_child(node, i))) return true;
    return false;
}

static char *new_temp(ccw_sml_lower *ctx)
{
    char name[40];
    snprintf(name, sizeof(name), "sml.tmp.%u", ctx->temp_index++);
    return sml_strdup(name);
}

static char *new_block_name(ccw_sml_lower *ctx, const char *stem)
{
    char name[48];
    snprintf(name, sizeof(name), "sml.%s.%u", stem, ctx->block_index++);
    return sml_strdup(name);
}

static bool block_terminated(const ccw_ir *ir, ccw_node block)
{
    int count = ccw_ir_block_instr_count(ir, block);
    if (count == 0) return false;
    const char *opcode =
        ccw_ir_instr_opcode(ir, ccw_ir_block_instr_ref(ir, block, count - 1));
    return opcode != NULL &&
           (strcmp(opcode, "ret") == 0 ||
            strcmp(opcode, "br") == 0 ||
            strcmp(opcode, "br.cond") == 0);
}

static void clear_function_names(ccw_sml_lower *ctx)
{
    for (int i = 0; i < ctx->parameter_count; i++)
        free(ctx->parameters[i]);
    for (int i = 0; i < ctx->alias_count; i++) {
        free(ctx->aliases[i].name);
        free(ctx->aliases[i].reg);
    }
    ctx->parameter_count = 0;
    ctx->alias_count = 0;
}

static bool add_parameter_name(ccw_sml_lower *ctx, const char *name)
{
    if (ctx->parameter_count >= CCW_SML_MAX_NAMES) {
        lower_fail(ctx, "swaff SML: too many parameters");
        return false;
    }
    ctx->parameters[ctx->parameter_count] = sml_strdup(name);
    if (ctx->parameters[ctx->parameter_count] == NULL) {
        lower_fail(ctx, "swaff SML: out of memory");
        return false;
    }
    ctx->parameter_count++;
    return true;
}

static bool is_parameter(const ccw_sml_lower *ctx, const char *name)
{
    for (int i = 0; i < ctx->parameter_count; i++)
        if (strcmp(ctx->parameters[i], name) == 0) return true;
    return false;
}

static const char *lookup_alias(const ccw_sml_lower *ctx, const char *name)
{
    for (int i = ctx->alias_count - 1; i >= 0; i--)
        if (strcmp(ctx->aliases[i].name, name) == 0)
            return ctx->aliases[i].reg;
    return NULL;
}

static bool push_alias(ccw_sml_lower *ctx, const char *name, const char *reg)
{
    if (ctx->alias_count >= CCW_SML_MAX_NAMES) {
        lower_fail(ctx, "swaff SML: too many nested value bindings");
        return false;
    }
    ccw_sml_alias *alias = &ctx->aliases[ctx->alias_count];
    alias->name = sml_strdup(name);
    alias->reg = sml_strdup(reg);
    if (alias->name == NULL || alias->reg == NULL) {
        free(alias->name);
        free(alias->reg);
        alias->name = NULL;
        alias->reg = NULL;
        lower_fail(ctx, "swaff SML: out of memory");
        return false;
    }
    ctx->alias_count++;
    return true;
}

static void pop_aliases(ccw_sml_lower *ctx, int saved_count)
{
    while (ctx->alias_count > saved_count) {
        ctx->alias_count--;
        free(ctx->aliases[ctx->alias_count].name);
        free(ctx->aliases[ctx->alias_count].reg);
    }
}

static TSNode simple_pattern_name(TSNode pattern)
{
    if (node_is(pattern, "vid_pat")) return pattern;
    if (node_is(pattern, "typed_pat") || node_is(pattern, "paren_pat"))
        return simple_pattern_name(first_named_child(pattern));
    return null_node();
}

static char *lower_expression(ccw_sml_lower *ctx, ccw_node *block,
                              TSNode expression);

static bool parse_integer_text(const char *text, int64_t *value)
{
    size_t length = strlen(text);
    char *normalized = (char *)malloc(length + 2u);
    if (normalized == NULL) return false;

    size_t out = 0;
    size_t in = 0;
    if (text[0] == '~') {
        normalized[out++] = '-';
        in++;
    }
    for (; in < length; in++)
        if (text[in] != '_') normalized[out++] = text[in];
    normalized[out] = '\0';

    bool negative = normalized[0] == '-';
    char *digits = normalized + (negative ? 1 : 0);
    if (digits[0] == '0' && digits[1] == 'w') {
        memmove(digits + 1, digits + 2, strlen(digits + 2) + 1u);
        if (digits[1] != 'x' && digits[1] != 'b')
            memmove(digits, digits + 1, strlen(digits + 1) + 1u);
    }
    int base = 10;
    if (digits[0] == '0' && digits[1] == 'b') {
        base = 2;
        digits += 2;
    } else if (digits[0] == '0' && digits[1] == 'x') {
        base = 16;
        digits += 2;
    }

    errno = 0;
    char *end = NULL;
    long long parsed = strtoll(digits, &end, base);
    bool valid = errno == 0 && end != digits && *end == '\0';
    if (valid) *value = (int64_t)(negative ? -parsed : parsed);
    free(normalized);
    return valid;
}

static char *lower_integer(ccw_sml_lower *ctx, ccw_node block, TSNode node)
{
    TSNode literal = node_is(node, "scon_exp") ? first_named_child(node) : node;
    char *text = node_text(literal, ctx->source, ctx->source_len);
    int64_t value;
    if (text == NULL || !parse_integer_text(text, &value)) {
        free(text);
        ctx->report->unsupported_nodes++;
        lower_fail(ctx, "swaff SML: unsupported integer literal");
        return NULL;
    }
    free(text);

    char *dest = new_temp(ctx);
    if (dest == NULL ||
        ccw_kliche_int_const(ctx->ir, block, dest, value) == 0) {
        free(dest);
        lower_fail(ctx, "swaff SML: could not lower integer literal");
        return NULL;
    }
    return dest;
}

static char *lower_boolean(ccw_sml_lower *ctx, ccw_node block, bool value)
{
    char *dest = new_temp(ctx);
    if (dest == NULL ||
        ccw_kliche_int_const(ctx->ir, block, dest, value ? 1 : 0) == 0) {
        free(dest);
        lower_fail(ctx, "swaff SML: could not lower boolean value");
        return NULL;
    }
    return dest;
}

static char *lower_value(ccw_sml_lower *ctx, ccw_node block, TSNode node)
{
    char *name = node_text(node, ctx->source, ctx->source_len);
    if (name == NULL) {
        lower_fail(ctx, "swaff SML: could not read value identifier");
        return NULL;
    }
    if (strcmp(name, "true") == 0 || strcmp(name, "false") == 0) {
        bool value = strcmp(name, "true") == 0;
        free(name);
        return lower_boolean(ctx, block, value);
    }
    const char *alias = lookup_alias(ctx, name);
    if (alias != NULL) {
        free(name);
        return sml_strdup(alias);
    }
    return name;
}

static const char *binary_opcode(const char *operator_text)
{
    struct operator_map { const char *sml; const char *ir; };
    static const struct operator_map operators[] = {
        { "+", "iadd" }, { "-", "isub" }, { "*", "imul" },
        { "div", "idiv" }, { "mod", "irem" },
        { "quot", "idiv" }, { "rem", "irem" },
        { "andb", "iand" }, { "orb", "ior" }, { "xorb", "ixor" },
        { "<<", "shl" }, { ">>", "shr" },
        { "=", "icmp.eq" }, { "<>", "icmp.ne" },
        { "<", "icmp.lt" }, { "<=", "icmp.le" },
        { ">", "icmp.gt" }, { ">=", "icmp.ge" }
    };
    for (size_t i = 0; i < sizeof(operators) / sizeof(operators[0]); i++)
        if (strcmp(operator_text, operators[i].sml) == 0)
            return operators[i].ir;
    return NULL;
}

static char *lower_binary_values(ccw_sml_lower *ctx, ccw_node *block,
                                 const char *opcode, TSNode left_node,
                                 TSNode right_node)
{
    char *left = lower_expression(ctx, block, left_node);
    char *right = lower_expression(ctx, block, right_node);
    char *dest = new_temp(ctx);
    ccw_ir_type type = strncmp(opcode, "icmp.", 5) == 0 ||
                       strncmp(opcode, "logic.", 6) == 0
        ? CCW_TY_I1 : CCW_TY_I64;
    if (left == NULL || right == NULL || dest == NULL ||
        ccw_kliche_binary(ctx->ir, *block, opcode, dest, left, right, type) ==
            0) {
        free(left);
        free(right);
        free(dest);
        if (!ctx->failed)
            lower_fail(ctx, "swaff SML: could not lower binary expression");
        return NULL;
    }
    free(left);
    free(right);
    return dest;
}

static char *lower_prefix(ccw_sml_lower *ctx, ccw_node *block,
                          const char *operator_text, TSNode operand_node)
{
    const char *opcode = NULL;
    ccw_ir_type type = CCW_TY_I64;
    if (strcmp(operator_text, "~") == 0)
        opcode = "ineg";
    else if (strcmp(operator_text, "not") == 0) {
        opcode = "logic.not";
        type = CCW_TY_I1;
    }
    if (opcode == NULL) return NULL;

    char *operand = lower_expression(ctx, block, operand_node);
    char *dest = new_temp(ctx);
    if (operand == NULL || dest == NULL ||
        ccw_kliche_unary(ctx->ir, *block, opcode, dest, operand, type) == 0) {
        free(operand);
        free(dest);
        if (!ctx->failed)
            lower_fail(ctx, "swaff SML: could not lower prefix expression");
        return NULL;
    }
    free(operand);
    return dest;
}

static char *lower_application(ccw_sml_lower *ctx, ccw_node *block,
                               TSNode node)
{
    uint32_t count = ts_node_named_child_count(node);
    if (count < 2) {
        lower_fail(ctx, "swaff SML: malformed application expression");
        return NULL;
    }

    if (count == 3) {
        TSNode operator_node = ts_node_named_child(node, 1);
        char *operator_text = node_is(operator_node, "vid_exp")
            ? node_text(operator_node, ctx->source, ctx->source_len)
            : NULL;
        const char *opcode =
            binary_opcode(operator_text != NULL ? operator_text : "");
        if (opcode != NULL) {
            char *result = lower_binary_values(
                ctx, block, opcode, ts_node_named_child(node, 0),
                ts_node_named_child(node, 2));
            free(operator_text);
            return result;
        }
        free(operator_text);
    }

    TSNode function_node = ts_node_named_child(node, 0);
    if (!node_is(function_node, "vid_exp")) {
        ctx->report->unsupported_nodes++;
        lower_fail(ctx,
                   "swaff SML: only named function application is supported");
        return NULL;
    }
    char *function_name =
        node_text(function_node, ctx->source, ctx->source_len);
    if (function_name == NULL) {
        lower_fail(ctx, "swaff SML: could not read applied function");
        return NULL;
    }

    if (count == 2) {
        char *prefix_result = lower_prefix(
            ctx, block, function_name, ts_node_named_child(node, 1));
        if (prefix_result != NULL || ctx->failed) {
            free(function_name);
            return prefix_result;
        }
    }

    for (uint32_t i = 1; i < count; i++) {
        TSNode child = ts_node_named_child(node, i);
        if (!node_is(child, "vid_exp")) continue;
        char *text = node_text(child, ctx->source, ctx->source_len);
        bool is_infix = text != NULL && binary_opcode(text) != NULL;
        free(text);
        if (is_infix) {
            free(function_name);
            ctx->report->unsupported_nodes++;
            lower_fail(
                ctx,
                "swaff SML: chained infix applications require fixity resolution");
            return NULL;
        }
    }

    if (count - 1u > CCW_SML_MAX_ARGS) {
        free(function_name);
        lower_fail(ctx, "swaff SML: too many application arguments");
        return NULL;
    }

    char *owned[CCW_SML_MAX_ARGS] = { 0 };
    const char *arguments[CCW_SML_MAX_ARGS];
    int argument_count = 0;
    for (uint32_t i = 1; i < count && !ctx->failed; i++) {
        owned[argument_count] =
            lower_expression(ctx, block, ts_node_named_child(node, i));
        if (owned[argument_count] == NULL) break;
        arguments[argument_count] = owned[argument_count];
        argument_count++;
    }

    const char *alias = lookup_alias(ctx, function_name);
    bool indirect = is_parameter(ctx, function_name) || alias != NULL;
    const char *callable = alias != NULL ? alias : function_name;
    char *result = NULL;
    if (!ctx->failed && indirect) {
        result = sml_strdup(callable);
        for (int i = 0; i < argument_count && result != NULL; i++) {
            char *next = new_temp(ctx);
            if (next == NULL ||
                ccw_kliche_closure_apply(ctx->ir, *block, next, result,
                                         arguments[i]) == 0) {
                free(next);
                free(result);
                result = NULL;
                lower_fail(ctx,
                           "swaff SML: could not lower higher-order application");
                break;
            }
            free(result);
            result = next;
        }
    } else if (!ctx->failed) {
        result = new_temp(ctx);
        if (result == NULL ||
            ccw_kliche_call(ctx->ir, *block, result, function_name, arguments,
                            argument_count, CCW_TY_I64) == 0) {
            free(result);
            result = NULL;
            lower_fail(ctx, "swaff SML: could not lower direct application");
        }
    }

    for (int i = 0; i < argument_count; i++) free(owned[i]);
    free(function_name);
    return result;
}

static char *lower_if(ccw_sml_lower *ctx, ccw_node *block, TSNode node)
{
    char *condition = lower_expression(ctx, block, field(node, "if_exp"));
    char *slot = new_temp(ctx);
    char *then_name = new_block_name(ctx, "then");
    char *else_name = new_block_name(ctx, "else");
    char *merge_name = new_block_name(ctx, "merge");
    if (condition == NULL || slot == NULL || then_name == NULL ||
        else_name == NULL || merge_name == NULL ||
        ccw_kliche_local_alloc(ctx->ir, *block, slot, CCW_TY_I64) == 0) {
        free(condition);
        free(slot);
        free(then_name);
        free(else_name);
        free(merge_name);
        if (!ctx->failed)
            lower_fail(ctx, "swaff SML: could not construct if expression");
        return NULL;
    }

    ccw_node then_block = ccw_ir_block_add(ctx->ir, ctx->fn, then_name);
    ccw_node else_block = ccw_ir_block_add(ctx->ir, ctx->fn, else_name);
    ccw_node merge_block = ccw_ir_block_add(ctx->ir, ctx->fn, merge_name);
    if (then_block == 0 || else_block == 0 || merge_block == 0 ||
        ccw_kliche_branch_if(ctx->ir, *block, condition,
                             then_name, else_name) == 0)
        lower_fail(ctx, "swaff SML: could not lower conditional branch");

    char *then_value = ctx->failed ? NULL :
        lower_expression(ctx, &then_block, field(node, "then_exp"));
    if (!ctx->failed &&
        (then_value == NULL ||
         ccw_kliche_local_store(ctx->ir, then_block, slot, then_value) == 0 ||
         (!block_terminated(ctx->ir, then_block) &&
          ccw_kliche_jump(ctx->ir, then_block, merge_name) == 0)))
        lower_fail(ctx, "swaff SML: could not lower then expression");

    TSNode else_expression = field(node, "else_exp");
    char *else_value = NULL;
    if (!ctx->failed && !ts_node_is_null(else_expression))
        else_value = lower_expression(ctx, &else_block, else_expression);
    else if (!ctx->failed)
        else_value = lower_boolean(ctx, else_block, false);
    if (!ctx->failed &&
        (else_value == NULL ||
         ccw_kliche_local_store(ctx->ir, else_block, slot, else_value) == 0 ||
         (!block_terminated(ctx->ir, else_block) &&
          ccw_kliche_jump(ctx->ir, else_block, merge_name) == 0)))
        lower_fail(ctx, "swaff SML: could not lower else expression");

    char *result = ctx->failed ? NULL : new_temp(ctx);
    if (!ctx->failed &&
        (result == NULL ||
         ccw_kliche_local_load(ctx->ir, merge_block, result, slot,
                               CCW_TY_I64) == 0)) {
        free(result);
        result = NULL;
        lower_fail(ctx, "swaff SML: could not merge if expression");
    }
    *block = merge_block;
    free(condition);
    free(slot);
    free(then_name);
    free(else_name);
    free(merge_name);
    free(then_value);
    free(else_value);
    return result;
}

static char *lower_short_circuit(ccw_sml_lower *ctx, ccw_node *block,
                                 TSNode node, bool conjunction)
{
    if (ts_node_named_child_count(node) != 2) {
        lower_fail(ctx, "swaff SML: malformed short-circuit expression");
        return NULL;
    }
    char *left =
        lower_expression(ctx, block, ts_node_named_child(node, 0));
    char *slot = new_temp(ctx);
    char *rhs_name = new_block_name(ctx, "bool.rhs");
    char *short_name = new_block_name(ctx, "bool.short");
    char *merge_name = new_block_name(ctx, "bool.merge");
    if (left == NULL || slot == NULL || rhs_name == NULL ||
        short_name == NULL || merge_name == NULL ||
        ccw_kliche_local_alloc(ctx->ir, *block, slot, CCW_TY_I1) == 0) {
        free(left);
        free(slot);
        free(rhs_name);
        free(short_name);
        free(merge_name);
        if (!ctx->failed)
            lower_fail(ctx,
                       "swaff SML: could not construct short-circuit expression");
        return NULL;
    }

    ccw_node rhs_block = ccw_ir_block_add(ctx->ir, ctx->fn, rhs_name);
    ccw_node short_block = ccw_ir_block_add(ctx->ir, ctx->fn, short_name);
    ccw_node merge_block = ccw_ir_block_add(ctx->ir, ctx->fn, merge_name);
    const char *then_name = conjunction ? rhs_name : short_name;
    const char *else_name = conjunction ? short_name : rhs_name;
    if (rhs_block == 0 || short_block == 0 || merge_block == 0 ||
        ccw_kliche_branch_if(ctx->ir, *block, left,
                             then_name, else_name) == 0)
        lower_fail(ctx,
                   "swaff SML: could not lower short-circuit branch");

    char *right = ctx->failed ? NULL :
        lower_expression(ctx, &rhs_block, ts_node_named_child(node, 1));
    if (!ctx->failed &&
        (right == NULL ||
         ccw_kliche_local_store(ctx->ir, rhs_block, slot, right) == 0 ||
         (!block_terminated(ctx->ir, rhs_block) &&
          ccw_kliche_jump(ctx->ir, rhs_block, merge_name) == 0)))
        lower_fail(ctx,
                   "swaff SML: could not lower short-circuit right operand");

    char *short_value = ctx->failed ? NULL :
        lower_boolean(ctx, short_block, !conjunction);
    if (!ctx->failed &&
        (short_value == NULL ||
         ccw_kliche_local_store(
             ctx->ir, short_block, slot, short_value) == 0 ||
         ccw_kliche_jump(ctx->ir, short_block, merge_name) == 0))
        lower_fail(ctx,
                   "swaff SML: could not lower short-circuit constant");

    char *result = ctx->failed ? NULL : new_temp(ctx);
    if (!ctx->failed &&
        (result == NULL ||
         ccw_kliche_local_load(ctx->ir, merge_block, result, slot,
                               CCW_TY_I1) == 0)) {
        free(result);
        result = NULL;
        lower_fail(ctx,
                   "swaff SML: could not merge short-circuit expression");
    }
    *block = merge_block;
    free(left);
    free(slot);
    free(rhs_name);
    free(short_name);
    free(merge_name);
    free(right);
    free(short_value);
    return result;
}

static char *lower_sequence(ccw_sml_lower *ctx, ccw_node *block, TSNode node)
{
    char *result = NULL;
    uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count && !ctx->failed; i++) {
        free(result);
        result = lower_expression(ctx, block, ts_node_named_child(node, i));
        ctx->report->statements_lowered++;
    }
    return result;
}

static bool lower_local_val_dec(ccw_sml_lower *ctx, ccw_node *block,
                                TSNode declaration)
{
    TSNode binding = null_node();
    int binding_count = 0;
    uint32_t count = ts_node_named_child_count(declaration);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_named_child(declaration, i);
        if (node_is(child, "valbind")) {
            binding = child;
            binding_count++;
        }
    }
    if (binding_count != 1) {
        ctx->report->unsupported_nodes++;
        lower_fail(ctx,
                   "swaff SML: local val-and bindings are unsupported");
        return false;
    }

    TSNode name_node = simple_pattern_name(field(binding, "pat"));
    if (ts_node_is_null(name_node)) {
        ctx->report->unsupported_nodes++;
        lower_fail(ctx,
                   "swaff SML: destructuring local val patterns are unsupported");
        return false;
    }
    char *name = node_text(name_node, ctx->source, ctx->source_len);
    char *value = lower_expression(ctx, block, field(binding, "def"));
    bool ok = name != NULL && value != NULL && push_alias(ctx, name, value);
    if (!ok && !ctx->failed)
        lower_fail(ctx, "swaff SML: could not lower local val binding");
    if (ok) ctx->report->declarations_lowered++;
    free(name);
    free(value);
    return ok;
}

static char *lower_let(ccw_sml_lower *ctx, ccw_node *block, TSNode node)
{
    int saved_aliases = ctx->alias_count;
    uint32_t count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < count && !ctx->failed; i++) {
        const char *field_name =
            ts_node_field_name_for_named_child(node, i);
        if (field_name == NULL || strcmp(field_name, "dec") != 0) continue;
        TSNode declaration = ts_node_named_child(node, i);
        if (!node_is(declaration, "val_dec")) {
            ctx->report->unsupported_nodes++;
            lower_fail(ctx,
                       "swaff SML: only local val declarations are supported");
            break;
        }
        lower_local_val_dec(ctx, block, declaration);
    }

    char *result = NULL;
    for (uint32_t i = 0; i < count && !ctx->failed; i++) {
        const char *field_name =
            ts_node_field_name_for_named_child(node, i);
        if (field_name == NULL || strcmp(field_name, "body") != 0) continue;
        free(result);
        result = lower_expression(ctx, block, ts_node_named_child(node, i));
    }
    pop_aliases(ctx, saved_aliases);
    if (!ctx->failed && result == NULL)
        lower_fail(ctx, "swaff SML: let expression has no body");
    return result;
}

static char *lower_expression(ccw_sml_lower *ctx, ccw_node *block,
                              TSNode expression)
{
    if (ctx->failed || ts_node_is_null(expression)) return NULL;
    const char *type = ts_node_type(expression);
    if (strcmp(type, "scon_exp") == 0) {
        TSNode literal = first_named_child(expression);
        if (node_is(literal, "integer_scon") ||
            node_is(literal, "word_scon"))
            return lower_integer(ctx, *block, expression);
    }
    if (strcmp(type, "vid_exp") == 0)
        return lower_value(ctx, *block, expression);
    if (strcmp(type, "app_exp") == 0)
        return lower_application(ctx, block, expression);
    if (strcmp(type, "cond_exp") == 0)
        return lower_if(ctx, block, expression);
    if (strcmp(type, "let_exp") == 0)
        return lower_let(ctx, block, expression);
    if (strcmp(type, "sequence_exp") == 0)
        return lower_sequence(ctx, block, expression);
    if (strcmp(type, "conj_exp") == 0 ||
        strcmp(type, "disj_exp") == 0)
        return lower_short_circuit(
            ctx, block, expression, strcmp(type, "conj_exp") == 0);
    if (strcmp(type, "paren_exp") == 0 ||
        strcmp(type, "typed_exp") == 0)
        return lower_expression(ctx, block, first_named_child(expression));
    if (strcmp(type, "unit_exp") == 0)
        return lower_boolean(ctx, *block, false);

    ctx->report->unsupported_nodes++;
    lower_fail(ctx, "swaff SML: unsupported expression");
    return NULL;
}

static bool add_function_parameter(ccw_sml_lower *ctx, TSNode pattern)
{
    TSNode name_node = simple_pattern_name(pattern);
    if (ts_node_is_null(name_node)) {
        ctx->report->unsupported_nodes++;
        lower_fail(ctx,
                   "swaff SML: destructuring function parameters are unsupported");
        return false;
    }
    char *name = node_text(name_node, ctx->source, ctx->source_len);
    if (name == NULL || !add_parameter_name(ctx, name) ||
        ccw_ir_function_add_param(ctx->ir, ctx->fn, CCW_TY_I64, name) !=
            CCW_OK) {
        free(name);
        if (!ctx->failed)
            lower_fail(ctx, "swaff SML: could not lower function parameter");
        return false;
    }
    free(name);
    return true;
}

static void lower_function_rule(ccw_sml_lower *ctx, TSNode rule)
{
    char *name = node_text(field(rule, "name"),
                           ctx->source, ctx->source_len);
    if (name == NULL) {
        lower_fail(ctx, "swaff SML: could not read function name");
        return;
    }
    ctx->fn = ccw_ir_function_add(ctx->ir, name, CCW_TY_I64);
    free(name);
    if (ctx->fn == 0) {
        lower_fail(ctx, "swaff SML: could not create function");
        return;
    }

    clear_function_names(ctx);
    ctx->temp_index = 0;
    ctx->block_index = 0;
    uint32_t count = ts_node_named_child_count(rule);
    for (uint32_t i = 0; i < count && !ctx->failed; i++) {
        const char *field_name =
            ts_node_field_name_for_named_child(rule, i);
        if (field_name != NULL && strcmp(field_name, "arg") == 0)
            add_function_parameter(ctx, ts_node_named_child(rule, i));
        else if (field_name != NULL &&
                 (strcmp(field_name, "argl") == 0 ||
                  strcmp(field_name, "argr") == 0)) {
            ctx->report->unsupported_nodes++;
            lower_fail(ctx,
                       "swaff SML: infix function declarations are unsupported");
        }
    }

    ccw_node block = ccw_ir_block_add(ctx->ir, ctx->fn, "entry");
    char *result = ctx->failed ? NULL :
        lower_expression(ctx, &block, field(rule, "def"));
    if (!ctx->failed &&
        (result == NULL ||
         (!block_terminated(ctx->ir, block) &&
          ccw_kliche_return(ctx->ir, block, result) == 0)))
        lower_fail(ctx, "swaff SML: could not lower function result");
    free(result);
    if (!ctx->failed) ctx->report->functions_lowered++;
    clear_function_names(ctx);
}

static void lower_fun_declaration(ccw_sml_lower *ctx, TSNode declaration)
{
    uint32_t count = ts_node_named_child_count(declaration);
    for (uint32_t i = 0; i < count && !ctx->failed; i++) {
        TSNode binding = ts_node_named_child(declaration, i);
        if (!node_is(binding, "fvalbind")) continue;
        int rule_count = 0;
        TSNode rule = null_node();
        uint32_t children = ts_node_named_child_count(binding);
        for (uint32_t j = 0; j < children; j++) {
            TSNode child = ts_node_named_child(binding, j);
            if (node_is(child, "fmrule")) {
                rule = child;
                rule_count++;
            }
        }
        if (rule_count != 1) {
            ctx->report->unsupported_nodes++;
            lower_fail(ctx,
                       "swaff SML: pattern-matching function clauses are unsupported");
            return;
        }
        lower_function_rule(ctx, rule);
    }
}

static void lower_fn_binding(ccw_sml_lower *ctx, TSNode binding, TSNode fn)
{
    TSNode name_node = simple_pattern_name(field(binding, "pat"));
    if (ts_node_is_null(name_node)) {
        ctx->report->unsupported_nodes++;
        return;
    }
    if (ts_node_named_child_count(fn) != 1 ||
        !node_is(ts_node_named_child(fn, 0), "mrule")) {
        ctx->report->unsupported_nodes++;
        lower_fail(ctx,
                   "swaff SML: pattern-matching fn expressions are unsupported");
        return;
    }
    TSNode rule = ts_node_named_child(fn, 0);
    if (ts_node_named_child_count(rule) != 2) {
        lower_fail(ctx, "swaff SML: malformed fn expression");
        return;
    }

    char *name = node_text(name_node, ctx->source, ctx->source_len);
    if (name == NULL) {
        lower_fail(ctx, "swaff SML: could not read val function name");
        return;
    }
    ctx->fn = ccw_ir_function_add(ctx->ir, name, CCW_TY_I64);
    free(name);
    if (ctx->fn == 0) {
        lower_fail(ctx, "swaff SML: could not create val function");
        return;
    }

    clear_function_names(ctx);
    ctx->temp_index = 0;
    ctx->block_index = 0;
    add_function_parameter(ctx, ts_node_named_child(rule, 0));
    ccw_node block = ccw_ir_block_add(ctx->ir, ctx->fn, "entry");
    char *result = ctx->failed ? NULL :
        lower_expression(ctx, &block, ts_node_named_child(rule, 1));
    if (!ctx->failed &&
        (result == NULL ||
         (!block_terminated(ctx->ir, block) &&
          ccw_kliche_return(ctx->ir, block, result) == 0)))
        lower_fail(ctx, "swaff SML: could not lower fn result");
    free(result);
    if (!ctx->failed) ctx->report->functions_lowered++;
    clear_function_names(ctx);
}

static void lower_top_val_declaration(ccw_sml_lower *ctx, TSNode declaration)
{
    uint32_t count = ts_node_named_child_count(declaration);
    for (uint32_t i = 0; i < count && !ctx->failed; i++) {
        TSNode binding = ts_node_named_child(declaration, i);
        if (!node_is(binding, "valbind")) continue;
        TSNode definition = field(binding, "def");
        if (node_is(definition, "fn_exp"))
            lower_fn_binding(ctx, binding, definition);
        else
            ctx->report->unsupported_nodes++;
    }
}

ccw_ir *ccw_swaff_lower_sml(const ccw_swaff_frontend *fe,
                            const char *source, size_t source_len,
                            const char *module_name, ccw_profile profile,
                            ccw_swaff_error_policy policy,
                            ccw_swaff_report *report, char **error_message)
{
    if (error_message != NULL) *error_message = NULL;
    ccw_swaff_report local;
    memset(&local, 0, sizeof(local));
    if (report != NULL) memset(report, 0, sizeof(*report));

    if (fe != &g_frontend_sml || source == NULL || module_name == NULL) {
        sml_set_error(
            error_message,
            "swaff SML: invalid frontend, source, or module name");
        return NULL;
    }
    if (source_len > UINT32_MAX) {
        sml_set_error(error_message,
                      "swaff SML: source is too large for Tree-sitter");
        return NULL;
    }

    TSParser *parser = ts_parser_new();
    const TSLanguage *language = tree_sitter_sml();
    if (parser == NULL || language == NULL ||
        !ts_parser_set_language(parser, language)) {
        if (parser != NULL) ts_parser_delete(parser);
        sml_set_error(
            error_message,
            "swaff SML: vendored SML grammar is ABI-incompatible");
        return NULL;
    }
    TSTree *tree =
        ts_parser_parse_string(parser, NULL, source, (uint32_t)source_len);
    if (tree == NULL) {
        ts_parser_delete(parser);
        sml_set_error(error_message,
                      "swaff SML: parser produced no syntax tree");
        return NULL;
    }

    TSNode root = ts_tree_root_node(tree);
    bool has_errors = scan_errors(root, &local);
    if (has_errors && policy == CCW_SWAFF_REJECT_ON_ERROR) {
        snprintf(local.message, sizeof(local.message),
                 "swaff SML: rejected CST with %d ERROR and %d MISSING nodes",
                 local.error_nodes, local.missing_nodes);
        if (report != NULL) *report = local;
        sml_set_error(error_message, local.message);
        ts_tree_delete(tree);
        ts_parser_delete(parser);
        return NULL;
    }

    ccw_ir *ir = ccw_ir_module_create(module_name, profile);
    if (ir == NULL) {
        ts_tree_delete(tree);
        ts_parser_delete(parser);
        sml_set_error(error_message, "swaff SML: out of memory");
        return NULL;
    }
    ccw_sml_lower ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.ir = ir;
    ctx.source = source;
    ctx.source_len = source_len;
    ctx.report = &local;

    uint32_t count = ts_node_named_child_count(root);
    for (uint32_t i = 0; i < count && !ctx.failed; i++) {
        TSNode child = ts_node_named_child(root, i);
        if (node_is(child, "block_comment") ||
            node_is(child, "line_comment"))
            continue;
        if (subtree_is_malformed(child)) {
            if (policy == CCW_SWAFF_RECOVER_ON_ERROR) {
                local.recovered_subtrees++;
                continue;
            }
        }
        if (node_is(child, "fun_dec"))
            lower_fun_declaration(&ctx, child);
        else if (node_is(child, "val_dec"))
            lower_top_val_declaration(&ctx, child);
        else
            local.unsupported_nodes++;
    }

    clear_function_names(&ctx);
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    if (ctx.failed) {
        snprintf(local.message, sizeof(local.message), "%s", ctx.failure);
        if (report != NULL) *report = local;
        sml_set_error(error_message, ctx.failure);
        ccw_ir_module_destroy(ir);
        return NULL;
    }
    if (has_errors)
        snprintf(local.message, sizeof(local.message),
                 "swaff SML: recovered %d malformed subtrees",
                 local.recovered_subtrees);
    if (report != NULL) *report = local;
    return ir;
}

#else

ccw_ir *ccw_swaff_lower_sml(const ccw_swaff_frontend *fe,
                            const char *source, size_t source_len,
                            const char *module_name, ccw_profile profile,
                            ccw_swaff_error_policy policy,
                            ccw_swaff_report *report, char **error_message)
{
    (void)fe;
    (void)source;
    (void)source_len;
    (void)module_name;
    (void)profile;
    (void)policy;
    (void)report;
    sml_set_error(
        error_message,
        "swaff SML: built without vendored Tree-sitter support");
    return NULL;
}

#endif
