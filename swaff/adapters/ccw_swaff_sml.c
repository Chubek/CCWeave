/* Standard ML lowering adapter for Swaff (§6.2).
 *
 * tree-sitter-sml deliberately leaves fixity resolution to consumers, so
 * infix source forms arrive as application CSTs. This adapter normalizes the
 * supported Basis operators before emitting profile-independent functional
 * and imperative Kliche construction patterns. Tree-sitter node names remain
 * confined to this translation unit. */

#include "ccw_kliche.h"
#include "ccw_swaff_internal.h"
#include "kstring.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const ccw_swaff_frontend g_frontend_sml = { "sml" };

const ccw_swaff_frontend *
ccw_swaff_frontend_sml (void)
{
  return &g_frontend_sml;
}

static char *
sml_strdup (const char *s)
{
  if (s == NULL)
    return NULL;
  kstring_t copy = { 0, 0, NULL };
  if (kputs (s, &copy) == EOF)
    return NULL;
  return ks_release (&copy);
}

static void
sml_set_error (char **error_message, const char *message)
{
  if (error_message != NULL)
    *error_message = sml_strdup (message);
}

#ifdef CCWEAVE_WITH_TREESITTER

#include <tree_sitter/api.h>
#include <tree_sitter/tree-sitter-sml.h>

#define CCW_SML_MAX_NAMES 128
#define CCW_SML_MAX_ARGS 32

typedef struct
{
  char *name;
  char *reg;
} ccw_sml_alias;

typedef struct
{
  ccw_ir *ir;
  ccw_node fn;
  const char *source;
  size_t source_len;
  ccw_swaff_report *report;
  bool failed;
  char failure[256];
  unsigned temp_index;
  unsigned block_index;
  char *parameters[CCW_SML_MAX_NAMES];
  int parameter_count;
  ccw_sml_alias aliases[CCW_SML_MAX_NAMES];
  int alias_count;
} ccw_sml_lower;

static bool
node_is (TSNode node, const char *type)
{
  return !ts_node_is_null (node) && strcmp (ts_node_type (node), type) == 0;
}

static TSNode
null_node (void)
{
  TSNode node = { { 0, 0, 0, 0 }, NULL, NULL };
  return node;
}

static TSNode
field (TSNode node, const char *name)
{
  return ts_node_child_by_field_name (node, name, (uint32_t)strlen (name));
}

static TSNode
first_named_child (TSNode node)
{
  return ts_node_named_child_count (node) > 0 ? ts_node_named_child (node, 0)
                                              : null_node ();
}

static char *
node_text (TSNode node, const char *source, size_t source_len)
{
  uint32_t start = ts_node_start_byte (node);
  uint32_t end = ts_node_end_byte (node);
  if (ts_node_is_null (node) || end < start || (size_t)end > source_len)
    return NULL;
  size_t length = (size_t)(end - start);
  kstring_t text = { 0, 0, NULL };
  if (kputsn (source + start, (int)length, &text) == EOF)
    return NULL;
  return ks_release (&text);
}

static void
lower_fail (ccw_sml_lower *ctx, const char *message)
{
  if (ctx->failed)
    return;
  ctx->failed = true;
  snprintf (ctx->failure, sizeof (ctx->failure), "%s", message);
}

static bool
scan_errors (TSNode node, ccw_swaff_report *report)
{
  bool found = false;
  if (ts_node_is_error (node) || node_is (node, "ERROR"))
    {
      report->error_nodes++;
      found = true;
    }
  if (ts_node_is_missing (node))
    {
      report->missing_nodes++;
      found = true;
    }
  uint32_t count = ts_node_child_count (node);
  for (uint32_t i = 0; i < count; i++)
    if (scan_errors (ts_node_child (node, i), report))
      found = true;
  return found;
}

static bool
subtree_is_malformed (TSNode node)
{
  if (ts_node_is_error (node) || ts_node_is_missing (node)
      || node_is (node, "ERROR"))
    return true;
  uint32_t count = ts_node_child_count (node);
  for (uint32_t i = 0; i < count; i++)
    if (subtree_is_malformed (ts_node_child (node, i)))
      return true;
  return false;
}

static char *
new_temp (ccw_sml_lower *ctx)
{
  char name[40];
  snprintf (name, sizeof (name), "sml.tmp.%u", ctx->temp_index++);
  return sml_strdup (name);
}

static char *
new_block_name (ccw_sml_lower *ctx, const char *stem)
{
  char name[48];
  snprintf (name, sizeof (name), "sml.%s.%u", stem, ctx->block_index++);
  return sml_strdup (name);
}

static bool
block_terminated (const ccw_ir *ir, ccw_node block)
{
  int count = ccw_ir_block_instr_count (ir, block);
  if (count == 0)
    return false;
  ccw_node last = ccw_ir_block_instr_ref (ir, block, count - 1);
  return ccw_ir_instr_is_terminator (ir, last);
}
static void
clear_function_names (ccw_sml_lower *ctx)
{
  for (int i = 0; i < ctx->parameter_count; i++)
    free (ctx->parameters[i]);
  for (int i = 0; i < ctx->alias_count; i++)
    {
      free (ctx->aliases[i].name);
      free (ctx->aliases[i].reg);
    }
  ctx->parameter_count = 0;
  ctx->alias_count = 0;
}

static bool
add_parameter_name (ccw_sml_lower *ctx, const char *name)
{
  if (ctx->parameter_count >= CCW_SML_MAX_NAMES)
    {
      lower_fail (ctx, "swaff SML: too many parameters");
      return false;
    }
  ctx->parameters[ctx->parameter_count] = sml_strdup (name);
  if (ctx->parameters[ctx->parameter_count] == NULL)
    {
      lower_fail (ctx, "swaff SML: out of memory");
      return false;
    }
  ctx->parameter_count++;
  return true;
}

static bool
is_parameter (const ccw_sml_lower *ctx, const char *name)
{
  for (int i = 0; i < ctx->parameter_count; i++)
    if (strcmp (ctx->parameters[i], name) == 0)
      return true;
  return false;
}

static const char *
lookup_alias (const ccw_sml_lower *ctx, const char *name)
{
  for (int i = ctx->alias_count - 1; i >= 0; i--)
    if (strcmp (ctx->aliases[i].name, name) == 0)
      return ctx->aliases[i].reg;
  return NULL;
}

static bool
push_alias (ccw_sml_lower *ctx, const char *name, const char *reg)
{
  if (ctx->alias_count >= CCW_SML_MAX_NAMES)
    {
      lower_fail (ctx, "swaff SML: too many nested value bindings");
      return false;
    }
  ccw_sml_alias *alias = &ctx->aliases[ctx->alias_count];
  alias->name = sml_strdup (name);
  alias->reg = sml_strdup (reg);
  if (alias->name == NULL || alias->reg == NULL)
    {
      free (alias->name);
      free (alias->reg);
      alias->name = NULL;
      alias->reg = NULL;
      lower_fail (ctx, "swaff SML: out of memory");
      return false;
    }
  ctx->alias_count++;
  return true;
}

static void
pop_aliases (ccw_sml_lower *ctx, int saved_count)
{
  while (ctx->alias_count > saved_count)
    {
      ctx->alias_count--;
      free (ctx->aliases[ctx->alias_count].name);
      free (ctx->aliases[ctx->alias_count].reg);
    }
}

static TSNode
simple_pattern_name (TSNode pattern)
{
  if (node_is (pattern, "vid_pat"))
    return pattern;
  if (node_is (pattern, "typed_pat") || node_is (pattern, "paren_pat"))
    return simple_pattern_name (first_named_child (pattern));
  return null_node ();
}

static char *lower_expression (ccw_sml_lower *ctx, ccw_node *block,
                               TSNode expression);

static char *
lower_opaque_expression (ccw_sml_lower *ctx, ccw_node *block, TSNode node)
{
  char *dest = new_temp (ctx);
  ccw_node ins;
  if (!dest)
    return NULL;
  ins = ccw_ir_instr_build (ctx->ir, "opaque.expr", CCW_TY_I64);
  if (!ins || ccw_ir_instr_set_dest (ctx->ir, ins, dest) != CCW_OK
      || ccw_ir_block_append_instr (ctx->ir, *block, ins) != CCW_OK)
    {
      free (dest);
      lower_fail (ctx, "swaff SML: could not lower opaque expression");
      return NULL;
    }
  {
    char *source = node_text (node, ctx->source, ctx->source_len);
    (void)ccw_ir_attr_set (ctx->ir, ins, "source", source ? source : "");
    free (source);
  }
  return dest;
}

static bool
parse_integer_text (const char *text, int64_t *value)
{
  size_t length = strlen (text);
  char *normalized = (char *)malloc (length + 2u);
  if (normalized == NULL)
    return false;

  size_t out = 0;
  size_t in = 0;
  if (text[0] == '~')
    {
      normalized[out++] = '-';
      in++;
    }
  for (; in < length; in++)
    if (text[in] != '_')
      normalized[out++] = text[in];
  normalized[out] = '\0';

  bool negative = normalized[0] == '-';
  char *digits = normalized + (negative ? 1 : 0);
  if (digits[0] == '0' && digits[1] == 'w')
    {
      memmove (digits + 1, digits + 2, strlen (digits + 2) + 1u);
      if (digits[1] != 'x' && digits[1] != 'b')
        memmove (digits, digits + 1, strlen (digits + 1) + 1u);
    }
  int base = 10;
  if (digits[0] == '0' && digits[1] == 'b')
    {
      base = 2;
      digits += 2;
    }
  else if (digits[0] == '0' && digits[1] == 'x')
    {
      base = 16;
      digits += 2;
    }

  errno = 0;
  char *end = NULL;
  long long parsed = strtoll (digits, &end, base);
  bool valid = errno == 0 && end != digits && *end == '\0';
  if (valid)
    *value = (int64_t)(negative ? -parsed : parsed);
  free (normalized);
  return valid;
}

static char *
lower_integer (ccw_sml_lower *ctx, ccw_node block, TSNode node)
{
  TSNode literal
      = node_is (node, "scon_exp") ? first_named_child (node) : node;
  char *text = node_text (literal, ctx->source, ctx->source_len);
  int64_t value;
  if (text == NULL || !parse_integer_text (text, &value))
    {
      free (text);
      ctx->report->unsupported_nodes++;
      lower_fail (ctx, "swaff SML: unsupported integer literal");
      return NULL;
    }
  free (text);

  char *dest = new_temp (ctx);
  if (dest == NULL || ccw_kliche_int_const (ctx->ir, block, dest, value) == 0)
    {
      free (dest);
      lower_fail (ctx, "swaff SML: could not lower integer literal");
      return NULL;
    }
  return dest;
}

static char *
lower_boolean (ccw_sml_lower *ctx, ccw_node block, bool value)
{
  char *dest = new_temp (ctx);
  if (dest == NULL
      || ccw_kliche_int_const (ctx->ir, block, dest, value ? 1 : 0) == 0)
    {
      free (dest);
      lower_fail (ctx, "swaff SML: could not lower boolean value");
      return NULL;
    }
  return dest;
}

/* Extract the longvid text from a vid_exp node, stripping the optional `op'
 * prefix.  For plain `vid` nodes this is equivalent to node_text. */
static char *
vid_exp_text (TSNode node, const char *source, size_t source_len)
{
  if (node_is (node, "vid_exp"))
    {
      TSNode longvid = first_named_child (node);
      if (!ts_node_is_null (longvid))
        return node_text (longvid, source, source_len);
    }
  return node_text (node, source, source_len);
}

static char *
lower_value (ccw_sml_lower *ctx, ccw_node block, TSNode node)
{
  char *name = vid_exp_text (node, ctx->source, ctx->source_len);
  if (name == NULL)
    {
      lower_fail (ctx, "swaff SML: could not read value identifier");
      return NULL;
    }
  if (strcmp (name, "true") == 0 || strcmp (name, "false") == 0)
    {
      bool value = strcmp (name, "true") == 0;
      free (name);
      return lower_boolean (ctx, block, value);
    }
  const char *alias = lookup_alias (ctx, name);
  if (alias != NULL)
    {
      free (name);
      return sml_strdup (alias);
    }
  return name;
}

static const char *
binary_opcode (const char *operator_text)
{
  struct operator_map
  {
    const char *sml;
    const char *ir;
  };
  static const struct operator_map operators[]
      = { { "+", "iadd" },     { "-", "isub" },     { "*", "imul" },
          { "div", "idiv" },   { "mod", "irem" },   { "quot", "idiv" },
          { "rem", "irem" },   { "andb", "iand" },  { "orb", "ior" },
          { "xorb", "ixor" },  { "<<", "shl" },     { ">>", "shr" },
          { "=", "icmp.eq" },  { "<>", "icmp.ne" }, { "<", "icmp.lt" },
          { "<=", "icmp.le" }, { ">", "icmp.gt" },  { ">=", "icmp.ge" } };
  for (size_t i = 0; i < sizeof (operators) / sizeof (operators[0]); i++)
    if (strcmp (operator_text, operators[i].sml) == 0)
      return operators[i].ir;
  return NULL;
}

static char *
lower_binary_values (ccw_sml_lower *ctx, ccw_node *block, const char *opcode,
                     TSNode left_node, TSNode right_node)
{
  char *left = lower_expression (ctx, block, left_node);
  char *right = lower_expression (ctx, block, right_node);
  char *dest = new_temp (ctx);
  ccw_ir_type type
      = strncmp (opcode, "icmp.", 5) == 0 || strncmp (opcode, "logic.", 6) == 0
            ? CCW_TY_I1
            : CCW_TY_I64;
  if (left == NULL || right == NULL || dest == NULL
      || (strncmp (opcode, "icmp.", 5) == 0
              ? ccw_kliche_cmp (ctx->ir, *block, opcode, dest, left, right,
                                CCW_TY_I64)
              : ccw_kliche_binop (ctx->ir, *block, opcode, dest, left, right,
                                  type)) == 0)
    {
      free (left);
      free (right);
      free (dest);
      if (!ctx->failed)
        lower_fail (ctx, "swaff SML: could not lower binary expression");
      return NULL;
    }
  free (left);
  free (right);
  return dest;
}

static char *
lower_prefix (ccw_sml_lower *ctx, ccw_node *block, const char *operator_text,
              TSNode operand_node)
{
  const char *opcode = NULL;
  ccw_ir_type type = CCW_TY_I64;
  if (strcmp (operator_text, "~") == 0)
    opcode = "ineg";
  else if (strcmp (operator_text, "not") == 0)
    {
      opcode = "logic.not";
      type = CCW_TY_I1;
    }
  if (opcode == NULL)
    return NULL;

  char *operand = lower_expression (ctx, block, operand_node);
  char *dest = new_temp (ctx);
  if (operand == NULL || dest == NULL
      || ccw_kliche_unary (ctx->ir, *block, opcode, dest, operand, type) == 0)
    {
      free (operand);
      free (dest);
      if (!ctx->failed)
        lower_fail (ctx, "swaff SML: could not lower prefix expression");
      return NULL;
    }
  free (operand);
  return dest;
}

static char *
lower_application (ccw_sml_lower *ctx, ccw_node *block, TSNode node)
{
  uint32_t count = ts_node_named_child_count (node);
  if (count < 2)
    {
      lower_fail (ctx, "swaff SML: malformed application expression");
      return NULL;
    }

  if (count == 3)
    {
      TSNode operator_node = ts_node_named_child (node, 1);
      char *operator_text
          = node_is (operator_node, "vid_exp")
                ? vid_exp_text (operator_node, ctx->source, ctx->source_len)
                : NULL;
      const char *opcode
          = binary_opcode (operator_text != NULL ? operator_text : "");
      if (opcode != NULL)
        {
          char *result = lower_binary_values (ctx, block, opcode,
                                              ts_node_named_child (node, 0),
                                              ts_node_named_child (node, 2));
          free (operator_text);
          return result;
        }
      free (operator_text);
    }

  TSNode function_node = ts_node_named_child (node, 0);
  if (!node_is (function_node, "vid_exp"))
    return lower_opaque_expression (ctx, block, node);
  char *function_name
      = vid_exp_text (function_node, ctx->source, ctx->source_len);
  if (function_name == NULL)
    {
      lower_fail (ctx, "swaff SML: could not read applied function");
      return NULL;
    }

  if (count == 2)
    {
      char *prefix_result = lower_prefix (ctx, block, function_name,
                                          ts_node_named_child (node, 1));
      if (prefix_result != NULL || ctx->failed)
        {
          free (function_name);
          return prefix_result;
        }
    }

  for (uint32_t i = 1; i < count; i++)
    {
      TSNode child = ts_node_named_child (node, i);
      if (!node_is (child, "vid_exp"))
        continue;
      char *text = vid_exp_text (child, ctx->source, ctx->source_len);
      bool is_infix = text != NULL && binary_opcode (text) != NULL;
      free (text);
      if (is_infix)
        {
          free (function_name);
          return lower_opaque_expression (ctx, block, node);
        }
    }

  if (count - 1u > CCW_SML_MAX_ARGS)
    {
      free (function_name);
      lower_fail (ctx, "swaff SML: too many application arguments");
      return NULL;
    }

  char *owned[CCW_SML_MAX_ARGS] = { 0 };
  const char *arguments[CCW_SML_MAX_ARGS];
  int argument_count = 0;
  for (uint32_t i = 1; i < count && !ctx->failed; i++)
    {
      owned[argument_count]
          = lower_expression (ctx, block, ts_node_named_child (node, i));
      if (owned[argument_count] == NULL)
        break;
      arguments[argument_count] = owned[argument_count];
      argument_count++;
    }

  const char *alias = lookup_alias (ctx, function_name);
  bool indirect = is_parameter (ctx, function_name) || alias != NULL;
  const char *callable = alias != NULL ? alias : function_name;
  char *result = NULL;
  if (!ctx->failed && indirect)
    {
      result = sml_strdup (callable);
      for (int i = 0; i < argument_count && result != NULL; i++)
        {
          char *next = new_temp (ctx);
          if (next == NULL
              || ccw_kliche_closure_apply (ctx->ir, *block, next, result,
                                           arguments[i])
                     == 0)
            {
              free (next);
              free (result);
              result = NULL;
              lower_fail (
                  ctx, "swaff SML: could not lower higher-order application");
              break;
            }
          free (result);
          result = next;
        }
    }
  else if (!ctx->failed)
    {
      result = new_temp (ctx);
      if (result == NULL
          || ccw_kliche_call (ctx->ir, *block, result, function_name,
                              arguments, argument_count, CCW_TY_I64)
                 == 0)
        {
          free (result);
          result = NULL;
          lower_fail (ctx, "swaff SML: could not lower direct application");
        }
    }

  for (int i = 0; i < argument_count; i++)
    free (owned[i]);
  free (function_name);
  return result;
}

static char *
lower_if (ccw_sml_lower *ctx, ccw_node *block, TSNode node)
{
  char *condition = lower_expression (ctx, block, field (node, "if_exp"));
  char *slot = new_temp (ctx);
  char *then_name = new_block_name (ctx, "then");
  char *else_name = new_block_name (ctx, "else");
  char *merge_name = new_block_name (ctx, "merge");
  if (condition == NULL || slot == NULL || then_name == NULL
      || else_name == NULL || merge_name == NULL
      || ccw_kliche_local_alloc (ctx->ir, *block, slot, CCW_TY_I64) == 0)
    {
      free (condition);
      free (slot);
      free (then_name);
      free (else_name);
      free (merge_name);
      if (!ctx->failed)
        lower_fail (ctx, "swaff SML: could not construct if expression");
      return NULL;
    }

  ccw_node then_block = ccw_ir_block_add (ctx->ir, ctx->fn, then_name);
  ccw_node else_block = ccw_ir_block_add (ctx->ir, ctx->fn, else_name);
  ccw_node merge_block = ccw_ir_block_add (ctx->ir, ctx->fn, merge_name);
  if (then_block == 0 || else_block == 0 || merge_block == 0
      || ccw_kliche_branch_if (ctx->ir, *block, condition, then_name,
                               else_name)
             == 0)
    lower_fail (ctx, "swaff SML: could not lower conditional branch");

  char *then_value = ctx->failed ? NULL
                                 : lower_expression (ctx, &then_block,
                                                     field (node, "then_exp"));
  if (!ctx->failed
      && (then_value == NULL
          || ccw_kliche_local_store (ctx->ir, then_block, slot, then_value)
                 == 0
          || (!block_terminated (ctx->ir, then_block)
              && ccw_kliche_jump (ctx->ir, then_block, merge_name) == 0)))
    lower_fail (ctx, "swaff SML: could not lower then expression");

  TSNode else_expression = field (node, "else_exp");
  char *else_value = NULL;
  if (!ctx->failed && !ts_node_is_null (else_expression))
    else_value = lower_expression (ctx, &else_block, else_expression);
  else if (!ctx->failed)
    else_value = lower_boolean (ctx, else_block, false);
  if (!ctx->failed
      && (else_value == NULL
          || ccw_kliche_local_store (ctx->ir, else_block, slot, else_value)
                 == 0
          || (!block_terminated (ctx->ir, else_block)
              && ccw_kliche_jump (ctx->ir, else_block, merge_name) == 0)))
    lower_fail (ctx, "swaff SML: could not lower else expression");

  char *result = ctx->failed ? NULL : new_temp (ctx);
  if (!ctx->failed
      && (result == NULL
          || ccw_kliche_local_load (ctx->ir, merge_block, result, slot,
                                    CCW_TY_I64)
                 == 0))
    {
      free (result);
      result = NULL;
      lower_fail (ctx, "swaff SML: could not merge if expression");
    }
  *block = merge_block;
  free (condition);
  free (slot);
  free (then_name);
  free (else_name);
  free (merge_name);
  free (then_value);
  free (else_value);
  return result;
}

static char *
lower_short_circuit (ccw_sml_lower *ctx, ccw_node *block, TSNode node,
                     bool conjunction)
{
  if (ts_node_named_child_count (node) != 2)
    {
      lower_fail (ctx, "swaff SML: malformed short-circuit expression");
      return NULL;
    }
  char *left = lower_expression (ctx, block, ts_node_named_child (node, 0));
  char *slot = new_temp (ctx);
  char *rhs_name = new_block_name (ctx, "bool.rhs");
  char *short_name = new_block_name (ctx, "bool.short");
  char *merge_name = new_block_name (ctx, "bool.merge");
  if (left == NULL || slot == NULL || rhs_name == NULL || short_name == NULL
      || merge_name == NULL
      || ccw_kliche_local_alloc (ctx->ir, *block, slot, CCW_TY_I1) == 0)
    {
      free (left);
      free (slot);
      free (rhs_name);
      free (short_name);
      free (merge_name);
      if (!ctx->failed)
        lower_fail (ctx,
                    "swaff SML: could not construct short-circuit expression");
      return NULL;
    }

  ccw_node rhs_block = ccw_ir_block_add (ctx->ir, ctx->fn, rhs_name);
  ccw_node short_block = ccw_ir_block_add (ctx->ir, ctx->fn, short_name);
  ccw_node merge_block = ccw_ir_block_add (ctx->ir, ctx->fn, merge_name);
  const char *then_name = conjunction ? rhs_name : short_name;
  const char *else_name = conjunction ? short_name : rhs_name;
  if (rhs_block == 0 || short_block == 0 || merge_block == 0
      || ccw_kliche_branch_if (ctx->ir, *block, left, then_name, else_name)
             == 0)
    lower_fail (ctx, "swaff SML: could not lower short-circuit branch");

  char *right = ctx->failed ? NULL
                            : lower_expression (ctx, &rhs_block,
                                                ts_node_named_child (node, 1));
  if (!ctx->failed
      && (right == NULL
          || ccw_kliche_local_store (ctx->ir, rhs_block, slot, right) == 0
          || (!block_terminated (ctx->ir, rhs_block)
              && ccw_kliche_jump (ctx->ir, rhs_block, merge_name) == 0)))
    lower_fail (ctx, "swaff SML: could not lower short-circuit right operand");

  char *short_value
      = ctx->failed ? NULL : lower_boolean (ctx, short_block, !conjunction);
  if (!ctx->failed
      && (short_value == NULL
          || ccw_kliche_local_store (ctx->ir, short_block, slot, short_value)
                 == 0
          || ccw_kliche_jump (ctx->ir, short_block, merge_name) == 0))
    lower_fail (ctx, "swaff SML: could not lower short-circuit constant");

  char *result = ctx->failed ? NULL : new_temp (ctx);
  if (!ctx->failed
      && (result == NULL
          || ccw_kliche_local_load (ctx->ir, merge_block, result, slot,
                                    CCW_TY_I1)
                 == 0))
    {
      free (result);
      result = NULL;
      lower_fail (ctx, "swaff SML: could not merge short-circuit expression");
    }
  *block = merge_block;
  free (left);
  free (slot);
  free (rhs_name);
  free (short_name);
  free (merge_name);
  free (right);
  free (short_value);
  return result;
}

static char *
lower_sequence (ccw_sml_lower *ctx, ccw_node *block, TSNode node)
{
  char *result = NULL;
  uint32_t count = ts_node_named_child_count (node);
  for (uint32_t i = 0; i < count && !ctx->failed; i++)
    {
      free (result);
      result = lower_expression (ctx, block, ts_node_named_child (node, i));
      ctx->report->statements_lowered++;
    }
  return result;
}

static bool
lower_local_val_dec (ccw_sml_lower *ctx, ccw_node *block, TSNode declaration)
{
  TSNode binding = null_node ();
  int binding_count = 0;
  uint32_t count = ts_node_named_child_count (declaration);
  for (uint32_t i = 0; i < count; i++)
    {
      TSNode child = ts_node_named_child (declaration, i);
      if (node_is (child, "valbind"))
        {
          binding = child;
          binding_count++;
        }
    }
  if (binding_count != 1)
    return true;

  TSNode name_node = simple_pattern_name (field (binding, "pat"));
  if (ts_node_is_null (name_node))
    {
      ccw_node ins = ccw_ir_instr_build (ctx->ir, "opaque.stmt", CCW_TY_VOID);
      if (!ins || ccw_ir_block_append_instr (ctx->ir, *block, ins) != CCW_OK)
        {
          lower_fail (ctx, "swaff SML: could not lower pattern binding");
          return false;
        }
      return true;
    }
  char *name = node_text (name_node, ctx->source, ctx->source_len);
  char *value = lower_expression (ctx, block, field (binding, "def"));
  bool ok = name != NULL && value != NULL && push_alias (ctx, name, value);
  if (!ok && !ctx->failed)
    lower_fail (ctx, "swaff SML: could not lower local val binding");
  if (ok)
    ctx->report->declarations_lowered++;
  free (name);
  free (value);
  return ok;
}

static char *
lower_let (ccw_sml_lower *ctx, ccw_node *block, TSNode node)
{
  int saved_aliases = ctx->alias_count;
  uint32_t count = ts_node_named_child_count (node);
  for (uint32_t i = 0; i < count && !ctx->failed; i++)
    {
      const char *field_name = ts_node_field_name_for_named_child (node, i);
      if (field_name == NULL || strcmp (field_name, "dec") != 0)
        continue;
      TSNode declaration = ts_node_named_child (node, i);
      if (!node_is (declaration, "val_dec"))
        continue;
      lower_local_val_dec (ctx, block, declaration);
    }

  char *result = NULL;
  for (uint32_t i = 0; i < count && !ctx->failed; i++)
    {
      const char *field_name = ts_node_field_name_for_named_child (node, i);
      if (field_name == NULL || strcmp (field_name, "body") != 0)
        continue;
      free (result);
      result = lower_expression (ctx, block, ts_node_named_child (node, i));
    }
  pop_aliases (ctx, saved_aliases);
  if (!ctx->failed && result == NULL)
    lower_fail (ctx, "swaff SML: let expression has no body");
  return result;
}

static char *
lower_expression (ccw_sml_lower *ctx, ccw_node *block, TSNode expression)
{
  if (ctx->failed || ts_node_is_null (expression))
    return NULL;
  const char *type = ts_node_type (expression);
  if (strcmp (type, "scon_exp") == 0)
    {
      TSNode literal = first_named_child (expression);
      if (node_is (literal, "integer_scon") || node_is (literal, "word_scon"))
        return lower_integer (ctx, *block, expression);
    }
  if (strcmp (type, "vid_exp") == 0)
    return lower_value (ctx, *block, expression);
  if (strcmp (type, "app_exp") == 0)
    return lower_application (ctx, block, expression);
  if (strcmp (type, "cond_exp") == 0)
    return lower_if (ctx, block, expression);
  if (strcmp (type, "let_exp") == 0)
    return lower_let (ctx, block, expression);
  if (strcmp (type, "sequence_exp") == 0)
    return lower_sequence (ctx, block, expression);
  if (strcmp (type, "conj_exp") == 0 || strcmp (type, "disj_exp") == 0)
    return lower_short_circuit (ctx, block, expression,
                                strcmp (type, "conj_exp") == 0);
  if (strcmp (type, "paren_exp") == 0 || strcmp (type, "typed_exp") == 0)
    return lower_expression (ctx, block, first_named_child (expression));
  if (strcmp (type, "unit_exp") == 0)
    return lower_boolean (ctx, *block, false);

  return lower_opaque_expression (ctx, block, expression);
}

static bool
add_function_parameter (ccw_sml_lower *ctx, TSNode pattern)
{
  TSNode name_node = simple_pattern_name (pattern);
  if (ts_node_is_null (name_node))
    {
      char synthetic[32];
      snprintf (synthetic, sizeof (synthetic), "sml.arg.%u",
                ctx->parameter_count);
      return add_parameter_name (ctx, synthetic)
             && ccw_ir_function_add_param (ctx->ir, ctx->fn, CCW_TY_I64,
                                            synthetic)
                    == CCW_OK;
    }
  char *name = node_text (name_node, ctx->source, ctx->source_len);
  if (name == NULL || !add_parameter_name (ctx, name)
      || ccw_ir_function_add_param (ctx->ir, ctx->fn, CCW_TY_I64, name)
             != CCW_OK)
    {
      free (name);
      if (!ctx->failed)
        lower_fail (ctx, "swaff SML: could not lower function parameter");
      return false;
    }
  free (name);
  return true;
}

static void
lower_function_rule (ccw_sml_lower *ctx, TSNode rule)
{
  char *name = node_text (field (rule, "name"), ctx->source, ctx->source_len);
  if (name == NULL)
    {
      lower_fail (ctx, "swaff SML: could not read function name");
      return;
    }
  ctx->fn = ccw_ir_function_add (ctx->ir, name, CCW_TY_I64);
  free (name);
  if (ctx->fn == 0)
    {
      lower_fail (ctx, "swaff SML: could not create function");
      return;
    }

  clear_function_names (ctx);
  ctx->temp_index = 0;
  ctx->block_index = 0;
  uint32_t count = ts_node_named_child_count (rule);
  for (uint32_t i = 0; i < count && !ctx->failed; i++)
    {
      const char *field_name = ts_node_field_name_for_named_child (rule, i);
      if (field_name != NULL && strcmp (field_name, "arg") == 0)
        add_function_parameter (ctx, ts_node_named_child (rule, i));
      else if (field_name != NULL
               && (strcmp (field_name, "argl") == 0
                   || strcmp (field_name, "argr") == 0))
        {
          add_function_parameter (ctx, ts_node_named_child (rule, i));
        }
    }

  ccw_node block = ccw_ir_block_add (ctx->ir, ctx->fn, "entry");
  char *result = ctx->failed
                     ? NULL
                     : lower_expression (ctx, &block, field (rule, "def"));
  if (!ctx->failed
      && (result == NULL
          || (!block_terminated (ctx->ir, block)
              && ccw_kliche_return (ctx->ir, block, result) == 0)))
    lower_fail (ctx, "swaff SML: could not lower function result");
  free (result);
  if (!ctx->failed)
    ctx->report->functions_lowered++;
  clear_function_names (ctx);
}

static void
lower_fun_declaration (ccw_sml_lower *ctx, TSNode declaration)
{
  uint32_t count = ts_node_named_child_count (declaration);
  for (uint32_t i = 0; i < count && !ctx->failed; i++)
    {
      TSNode binding = ts_node_named_child (declaration, i);
      if (!node_is (binding, "fvalbind"))
        continue;
      int rule_count = 0;
      TSNode rule = null_node ();
      uint32_t children = ts_node_named_child_count (binding);
      for (uint32_t j = 0; j < children; j++)
        {
          TSNode child = ts_node_named_child (binding, j);
          if (node_is (child, "fmrule"))
            {
              rule = child;
              rule_count++;
            }
        }
      if (rule_count != 1)
        continue;
      lower_function_rule (ctx, rule);
    }
}

static void
lower_fn_binding (ccw_sml_lower *ctx, TSNode binding, TSNode fn)
{
  TSNode name_node = simple_pattern_name (field (binding, "pat"));
  if (ts_node_is_null (name_node))
    return;
  if (ts_node_named_child_count (fn) != 1
      || !node_is (ts_node_named_child (fn, 0), "mrule"))
    return;
  TSNode rule = ts_node_named_child (fn, 0);
  if (ts_node_named_child_count (rule) != 2)
    {
      lower_fail (ctx, "swaff SML: malformed fn expression");
      return;
    }

  char *name = node_text (name_node, ctx->source, ctx->source_len);
  if (name == NULL)
    {
      lower_fail (ctx, "swaff SML: could not read val function name");
      return;
    }
  ctx->fn = ccw_ir_function_add (ctx->ir, name, CCW_TY_I64);
  free (name);
  if (ctx->fn == 0)
    {
      lower_fail (ctx, "swaff SML: could not create val function");
      return;
    }

  clear_function_names (ctx);
  ctx->temp_index = 0;
  ctx->block_index = 0;
  add_function_parameter (ctx, ts_node_named_child (rule, 0));
  ccw_node block = ccw_ir_block_add (ctx->ir, ctx->fn, "entry");
  char *result
      = ctx->failed
            ? NULL
            : lower_expression (ctx, &block, ts_node_named_child (rule, 1));
  if (!ctx->failed
      && (result == NULL
          || (!block_terminated (ctx->ir, block)
              && ccw_kliche_return (ctx->ir, block, result) == 0)))
    lower_fail (ctx, "swaff SML: could not lower fn result");
  free (result);
  if (!ctx->failed)
    ctx->report->functions_lowered++;
  clear_function_names (ctx);
}

static void
lower_top_val_declaration (ccw_sml_lower *ctx, TSNode declaration)
{
  uint32_t count = ts_node_named_child_count (declaration);
  for (uint32_t i = 0; i < count && !ctx->failed; i++)
    {
      TSNode binding = ts_node_named_child (declaration, i);
      if (!node_is (binding, "valbind"))
        continue;
      TSNode definition = field (binding, "def");
      if (node_is (definition, "fn_exp"))
        lower_fn_binding (ctx, binding, definition);
      else
        {
          TSNode name_node = simple_pattern_name (field (binding, "pat"));
          char *name = ts_node_is_null (name_node)
                           ? sml_strdup ("sml.value")
                           : node_text (name_node, ctx->source,
                                        ctx->source_len);
          if (name == NULL)
            {
              lower_fail (ctx, "swaff SML: could not read top-level value");
              continue;
            }
          ctx->fn = ccw_ir_function_add (ctx->ir, name, CCW_TY_I64);
          free (name);
          if (ctx->fn == 0)
            {
              lower_fail (ctx, "swaff SML: could not create top-level value");
              continue;
            }
          clear_function_names (ctx);
          ctx->temp_index = ctx->block_index = 0;
          ccw_node block = ccw_ir_block_add (ctx->ir, ctx->fn, "entry");
          char *value = lower_expression (ctx, &block, definition);
          if (value != NULL && !block_terminated (ctx->ir, block))
            ccw_kliche_return (ctx->ir, block, value);
          free (value);
          if (!ctx->failed)
            ctx->report->functions_lowered++;
          clear_function_names (ctx);
        }
    }
}

ccw_ir *
ccw_swaff_lower_sml (const ccw_swaff_frontend *fe, const char *source,
                     size_t source_len, const char *module_name,
                     ccw_profile profile, ccw_swaff_error_policy policy,
                     ccw_swaff_report *report, char **error_message)
{
  if (error_message != NULL)
    *error_message = NULL;
  ccw_swaff_report local;
  memset (&local, 0, sizeof (local));
  if (report != NULL)
    memset (report, 0, sizeof (*report));

  if (fe != &g_frontend_sml || source == NULL || module_name == NULL)
    {
      sml_set_error (error_message,
                     "swaff SML: invalid frontend, source, or module name");
      return NULL;
    }
  if (source_len > UINT32_MAX)
    {
      sml_set_error (error_message,
                     "swaff SML: source is too large for Tree-sitter");
      return NULL;
    }

  TSParser *parser = ts_parser_new ();
  const TSLanguage *language = tree_sitter_sml ();
  if (parser == NULL || language == NULL
      || !ts_parser_set_language (parser, language))
    {
      if (parser != NULL)
        ts_parser_delete (parser);
      sml_set_error (error_message,
                     "swaff SML: vendored SML grammar is ABI-incompatible");
      return NULL;
    }
  TSTree *tree
      = ts_parser_parse_string (parser, NULL, source, (uint32_t)source_len);
  if (tree == NULL)
    {
      ts_parser_delete (parser);
      sml_set_error (error_message,
                     "swaff SML: parser produced no syntax tree");
      return NULL;
    }

  TSNode root = ts_tree_root_node (tree);
  bool has_errors = scan_errors (root, &local);
  if (has_errors && policy == CCW_SWAFF_REJECT_ON_ERROR)
    {
      snprintf (local.message, sizeof (local.message),
                "swaff SML: rejected CST with %d ERROR and %d MISSING nodes",
                local.error_nodes, local.missing_nodes);
      if (report != NULL)
        *report = local;
      sml_set_error (error_message, local.message);
      ts_tree_delete (tree);
      ts_parser_delete (parser);
      return NULL;
    }

  ccw_ir *ir = ccw_ir_module_create (module_name, profile);
  if (ir == NULL)
    {
      ts_tree_delete (tree);
      ts_parser_delete (parser);
      sml_set_error (error_message, "swaff SML: out of memory");
      return NULL;
    }
  ccw_sml_lower ctx;
  memset (&ctx, 0, sizeof (ctx));
  ctx.ir = ir;
  ctx.source = source;
  ctx.source_len = source_len;
  ctx.report = &local;

  uint32_t count = ts_node_named_child_count (root);
  for (uint32_t i = 0; i < count && !ctx.failed; i++)
    {
      TSNode child = ts_node_named_child (root, i);
      if (node_is (child, "block_comment") || node_is (child, "line_comment"))
        continue;
      if (subtree_is_malformed (child))
        {
          if (policy == CCW_SWAFF_RECOVER_ON_ERROR)
            {
              local.recovered_subtrees++;
              continue;
            }
        }
      if (node_is (child, "fun_dec"))
        lower_fun_declaration (&ctx, child);
      else if (node_is (child, "val_dec"))
        lower_top_val_declaration (&ctx, child);
      else
        continue;
    }

  clear_function_names (&ctx);
  ts_tree_delete (tree);
  ts_parser_delete (parser);
  if (ctx.failed)
    {
      snprintf (local.message, sizeof (local.message), "%s", ctx.failure);
      if (report != NULL)
        *report = local;
      sml_set_error (error_message, ctx.failure);
      ccw_ir_module_destroy (ir);
      return NULL;
    }
  if (has_errors)
    snprintf (local.message, sizeof (local.message),
              "swaff SML: recovered %d malformed subtrees",
              local.recovered_subtrees);
  if (report != NULL)
    *report = local;
  return ir;
}

#else

ccw_ir *
ccw_swaff_lower_sml (const ccw_swaff_frontend *fe, const char *source,
                     size_t source_len, const char *module_name,
                     ccw_profile profile, ccw_swaff_error_policy policy,
                     ccw_swaff_report *report, char **error_message)
{
  (void)fe;
  (void)source;
  (void)source_len;
  (void)module_name;
  (void)profile;
  (void)policy;
  (void)report;
  sml_set_error (error_message,
                 "swaff SML: built without vendored Tree-sitter support");
  return NULL;
}

#endif

/* Parse-only SML surface adapter (D-0046). Kept in this translation unit so
 * the Swaff SML frontend and Parthia parse entry point share one grammar
 * binding. */

/* Parse-only Standard ML '97 surface-AST adapter for Swaff (D-0046).
 *
 * The parse-only entry point performs NO semantic work: it walks the
 * tree-sitter-sml CST and emits a
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

#include "ccw_swaff_internal.h"
#include "ccw_swaff_parse.h"
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

typedef enum
{
  SML_FIX_NONFIX = 0,
  SML_FIX_INFIX, /* left-associative  */
  SML_FIX_INFIXR /* right-associative */
} sml_fixity_kind;

typedef struct
{
  char *name;
  sml_fixity_kind kind;
  int prec; /* 0-9; meaningful only when kind != SML_FIX_NONFIX */
} sml_fixity_entry;

typedef struct
{
  sml_fixity_entry entries[CCW_SML_FIXITY_MAX];
  int count;
} sml_fixity_env;

typedef struct
{
  const char *source;
  size_t source_len;
  ccw_sml_parse_report *report;
  kstring_t out;
  bool failed;
  char failure[256];
  sml_fixity_env fixity;
} sml_parse_ctx;

/* ---------- small helpers ---------- */

static void
parse_fail (sml_parse_ctx *ctx, const char *fmt, ...)
{
  if (ctx->failed)
    return;
  ctx->failed = true;
  va_list ap;
  va_start (ap, fmt);
  vsnprintf (ctx->failure, sizeof (ctx->failure), fmt, ap);
  va_end (ap);
}

static bool
parse_node_is (TSNode node, const char *type)
{
  return !ts_node_is_null (node) && strcmp (ts_node_type (node), type) == 0;
}

static TSNode
parse_null_node (void)
{
  TSNode node = { { 0, 0, 0, 0 }, NULL, NULL };
  return node;
}

static TSNode
parse_first_named_child (TSNode node)
{
  return ts_node_named_child_count (node) > 0 ? ts_node_named_child (node, 0)
                                              : parse_null_node ();
}

static char *
parse_strdup (const char *text)
{
  size_t length;
  char *copy;
  if (text == NULL)
    return NULL;
  length = strlen (text);
  copy = (char *)malloc (length + 1u);
  if (copy == NULL)
    return NULL;
  memcpy (copy, text, length + 1u);
  return copy;
}

/* The raw source text of a node, appended verbatim (atoms only). */
static void
emit_raw (sml_parse_ctx *ctx, TSNode node)
{
  uint32_t start = ts_node_start_byte (node);
  uint32_t end = ts_node_end_byte (node);
  if (ts_node_is_null (node) || end < start || (size_t)end > ctx->source_len)
    {
      parse_fail (ctx, "swaff SML parse: node out of source range");
      return;
    }
  if (kputc (' ', &ctx->out) == EOF
      || kputsn (ctx->source + start, (int)(end - start), &ctx->out) == EOF)
    parse_fail (ctx, "swaff SML parse: out of memory");
}

static void
emit_open (sml_parse_ctx *ctx, const char *tag)
{
  if (kputc ('(', &ctx->out) == EOF || kputs (tag, &ctx->out) == EOF)
    parse_fail (ctx, "swaff SML parse: out of memory");
}

static void
emit_close (sml_parse_ctx *ctx)
{
  if (kputc (')', &ctx->out) == EOF)
    parse_fail (ctx, "swaff SML parse: out of memory");
}

static void
emit_atom (sml_parse_ctx *ctx, const char *text)
{
  if (kputc (' ', &ctx->out) == EOF || kputs (text, &ctx->out) == EOF)
    parse_fail (ctx, "swaff SML parse: out of memory");
}

/* ---------- fixity environment ---------- */

static void
fixity_seed_basis (sml_fixity_env *env)
{
  /* Basis default infix bindings (Definition of SML '97, Appendix C).
   * Seeded in a fixed order; lookup is linear so order does not affect
   * results. */
  static const struct
  {
    const char *n;
    sml_fixity_kind k;
    int p;
  } basis[] = { { "*", SML_FIX_INFIX, 7 },   { "/", SML_FIX_INFIX, 7 },
                { "div", SML_FIX_INFIX, 7 }, { "mod", SML_FIX_INFIX, 7 },
                { "+", SML_FIX_INFIX, 6 },   { "-", SML_FIX_INFIX, 6 },
                { "^", SML_FIX_INFIX, 6 },   { "::", SML_FIX_INFIXR, 5 },
                { "@", SML_FIX_INFIXR, 5 },  { "=", SML_FIX_INFIX, 4 },
                { "<>", SML_FIX_INFIX, 4 },  { "<", SML_FIX_INFIX, 4 },
                { "<=", SML_FIX_INFIX, 4 },  { ">", SML_FIX_INFIX, 4 },
                { ">=", SML_FIX_INFIX, 4 },  { ":=", SML_FIX_INFIX, 3 },
                { "o", SML_FIX_INFIX, 3 },   { "before", SML_FIX_INFIX, 0 } };
  env->count = 0;
  for (size_t i = 0; i < sizeof (basis) / sizeof (basis[0]); i++)
    {
      if (env->count >= CCW_SML_FIXITY_MAX)
        break;
      env->entries[env->count].name = parse_strdup (basis[i].n);
      env->entries[env->count].kind = basis[i].k;
      env->entries[env->count].prec = basis[i].p;
      if (env->entries[env->count].name != NULL)
        env->count++;
    }
}

static void
fixity_free (sml_fixity_env *env)
{
  for (int i = 0; i < env->count; i++)
    free (env->entries[i].name);
  env->count = 0;
}

static const sml_fixity_entry *
fixity_lookup (const sml_fixity_env *env, const char *name)
{
  /* Later declarations shadow earlier ones, matching SML scoping of fixity
   * directives; scan from the end. Deterministic (linear, source order). */
  for (int i = env->count - 1; i >= 0; i--)
    if (strcmp (env->entries[i].name, name) == 0)
      return &env->entries[i];
  return NULL;
}

static void
fixity_declare (sml_parse_ctx *ctx, TSNode dec, sml_fixity_kind kind)
{
  /* Optional precedence digit for infix/infixr; default 0 per Definition. */
  int prec = 0;
  uint32_t n = ts_node_named_child_count (dec);
  for (uint32_t i = 0; i < n; i++)
    {
      TSNode child = ts_node_named_child (dec, i);
      if (!parse_node_is (child, "vid"))
        continue;
      char *name = NULL;
      uint32_t s = ts_node_start_byte (child), e = ts_node_end_byte (child);
      if ((size_t)e <= ctx->source_len && e >= s)
        {
          size_t len = (size_t)(e - s);
          name = (char *)malloc (len + 1u);
          if (name != NULL)
            {
              memcpy (name, ctx->source + s, len);
              name[len] = '\0';
            }
        }
      if (name == NULL)
        {
          parse_fail (ctx, "swaff SML parse: out of memory");
          return;
        }
      if (ctx->fixity.count >= CCW_SML_FIXITY_MAX)
        {
          free (name);
          parse_fail (ctx, "swaff SML parse: too many fixity declarations");
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

static void emit_exp (sml_parse_ctx *ctx, TSNode node);
static void emit_pat (sml_parse_ctx *ctx, TSNode node);
static void emit_ty (sml_parse_ctx *ctx, TSNode node);
static void emit_dec (sml_parse_ctx *ctx, TSNode node);
static void emit_strexp (sml_parse_ctx *ctx, TSNode node);
static void emit_strdec (sml_parse_ctx *ctx, TSNode node);
static void emit_sigexp (sml_parse_ctx *ctx, TSNode node);
static void emit_spec (sml_parse_ctx *ctx, TSNode node);

static const char *
ast_tag (const char *type)
{
  /* Keep the surface vocabulary independent of the grammar's internal
   * `_foo`/`foo_bar` names.  In particular, these tags are the contract
   * consumed by Parthia's module elaborator (§2). */
  if (strcmp (type, "structure_strdec") == 0)
    return "structure";
  if (strcmp (type, "signature_sigdec") == 0)
    return "signature";
  if (strcmp (type, "functor_fctdec") == 0)
    return "functor";
  if (strcmp (type, "struct_strexp") == 0)
    return "struct";
  if (strcmp (type, "strid_strexp") == 0)
    return "strid";
  if (strcmp (type, "fctapp_strexp") == 0)
    return "fctapp";
  if (strcmp (type, "constr_strexp") == 0)
    return "constrain";
  if (strcmp (type, "let_strexp") == 0)
    return "let-struct";
  if (strcmp (type, "sig_sigexp") == 0)
    return "sig";
  if (strcmp (type, "sigid_sigexp") == 0)
    return "sigid";
  if (strcmp (type, "wheretype_sigexp") == 0)
    return "wheretype";
  if (strcmp (type, "sharing_spec") == 0)
    return "sharing";
  if (strcmp (type, "sharingtype_spec") == 0)
    return "sharing-type";
  if (strcmp (type, "structure_spec") == 0)
    return "structure-spec";
  return type;
}

/* Emit each named child under a repeated-field node via the matching
 * category emitter, dispatching on the child type. */
static void
emit_children_generic (sml_parse_ctx *ctx, TSNode node)
{
  uint32_t n = ts_node_named_child_count (node);
  for (uint32_t i = 0; i < n && !ctx->failed; i++)
    {
      TSNode child = ts_node_named_child (node, i);
      const char *t = ts_node_type (child);
      if (strstr (t, "_exp") != NULL || strcmp (t, "mrule") == 0)
        emit_exp (ctx, child);
      else if (strstr (t, "_pat") != NULL)
        emit_pat (ctx, child);
      else if (strstr (t, "_ty") != NULL || strcmp (t, "tyvarseq") == 0
               || strcmp (t, "tyseq") == 0 || strcmp (t, "tyrow") == 0
               || strcmp (t, "lab") == 0 || strcmp (t, "tycon") == 0
               || strcmp (t, "longtycon") == 0 || strcmp (t, "tyvar") == 0)
        emit_ty (ctx, child);
      else if (strstr (t, "_dec") != NULL || strcmp (t, "valbind") == 0
               || strcmp (t, "fvalbind") == 0 || strcmp (t, "fmrule") == 0
               || strcmp (t, "datbind") == 0 || strcmp (t, "conbind") == 0
               || strcmp (t, "exbind") == 0 || strcmp (t, "typbind") == 0)
        emit_dec (ctx, child);
      else if (strstr (t, "_strexp") != NULL || strcmp (t, "strbind") == 0
               || strcmp (t, "strdesc") == 0 || strcmp (t, "fctbind") == 0)
        emit_strexp (ctx, child);
      else if (strstr (t, "_strdec") != NULL)
        emit_strdec (ctx, child);
      else if (strstr (t, "_sigexp") != NULL || strcmp (t, "sigbind") == 0)
        emit_sigexp (ctx, child);
      else if (strstr (t, "_spec") != NULL || strcmp (t, "strdesc") == 0
               || strcmp (t, "condesc") == 0 || strcmp (t, "exdesc") == 0
               || strcmp (t, "typedesc") == 0 || strcmp (t, "datdesc") == 0)
        emit_spec (ctx, child);
      else
        emit_raw (ctx, child); /* vid, strid, sigid, fctid, lab, scon */
    }
}

/* Generic structural emitter: (tag field children...). Used for every node
 * that has no special normalization. The tag is the Tree-sitter node type. */
static void
emit_node (sml_parse_ctx *ctx, TSNode node)
{
  emit_open (ctx, ast_tag (ts_node_type (node)));
  emit_children_generic (ctx, node);
  emit_close (ctx);
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
static const sml_fixity_entry *
fixity_of_node (sml_parse_ctx *ctx, TSNode node, char *buf, size_t bufsz)
{
  if (!parse_node_is (node, "vid") && !parse_node_is (node, "vid_exp"))
    return NULL;
  TSNode leaf = parse_node_is (node, "vid_exp")
                    ? parse_first_named_child (node)
                    : node;
  if (ts_node_is_null (leaf))
    leaf = node;
  uint32_t s = ts_node_start_byte (leaf), e = ts_node_end_byte (leaf);
  if ((size_t)e > ctx->source_len || e < s)
    return NULL;
  size_t len = (size_t)(e - s);
  if (len == 0 || len >= bufsz)
    return NULL;
  memcpy (buf, ctx->source + s, len);
  buf[len] = '\0';
  return fixity_lookup (&ctx->fixity, buf);
}

/* Re-associate a flat application spine into a fixity-resolved tree and emit
 * it. `items` are the spine's operand/operator expressions in source order. */
static void
emit_app_spine (sml_parse_ctx *ctx, TSNode *items, int count)
{
  /* Separate the spine into operands and infix operators. An item is an
   * infix operator iff it is a vid with a non-nonfix fixity entry. */
  if (count <= 0)
    {
      parse_fail (ctx, "swaff SML parse: empty application");
      return;
    }

  /* Choose the lowest-precedence operator as the root.  Equal-precedence
   * left-associative operators choose the rightmost occurrence; right-
   * associative operators choose the leftmost occurrence. */
  int op_index = -1;
  sml_fixity_entry op_copy;
  memset (&op_copy, 0, sizeof (op_copy));
  for (int i = 1; i < count - 1; i++)
    {
      char opbuf[64];
      const sml_fixity_entry *f
          = fixity_of_node (ctx, items[i], opbuf, sizeof (opbuf));
      if (f != NULL && f->kind != SML_FIX_NONFIX)
        {
          if (op_index < 0 || f->prec < op_copy.prec
              || (f->prec == op_copy.prec
                  && ((f->kind == SML_FIX_INFIX && i > op_index)
                      || (f->kind == SML_FIX_INFIXR && i < op_index))))
            {
              op_index = i;
              op_copy = *f;
            }
        }
    }

  if (op_index < 0)
    {
      /* Pure function application: left-nested (app f a). */
      emit_open (ctx, "app");
      emit_exp (ctx, items[0]);
      for (int i = 1; i < count && !ctx->failed; i++)
        emit_exp (ctx, items[i]);
      emit_close (ctx);
      return;
    }

  /* Infix: re-associate by precedence/associativity. Emit as
   * (infix op lhs rhs) so the elaborator sees the resolved structure. */
  emit_open (ctx, "infix");
  emit_raw (ctx, items[op_index]); /* the operator vid */
  /* lhs and rhs are themselves spines. */
  if (op_index == 1)
    emit_exp (ctx, items[0]);
  else
    emit_app_spine (ctx, items, op_index);
  if (op_index == count - 2)
    emit_exp (ctx, items[count - 1]);
  else
    emit_app_spine (ctx, items + op_index + 1, count - op_index - 1);
  emit_close (ctx);
  (void)op_copy;
}

static void
emit_app_exp (sml_parse_ctx *ctx, TSNode node)
{
  uint32_t n = ts_node_named_child_count (node);
  if (n == 0)
    {
      parse_fail (ctx, "swaff SML parse: malformed app_exp");
      return;
    }
  TSNode *items = (TSNode *)malloc (n * sizeof (TSNode));
  if (items == NULL)
    {
      parse_fail (ctx, "swaff SML parse: out of memory");
      return;
    }
  for (uint32_t i = 0; i < n; i++)
    items[i] = ts_node_named_child (node, i);
  emit_app_spine (ctx, items, (int)n);
  free (items);
}

/* ---------- category emitters ---------- */

static void
emit_exp (sml_parse_ctx *ctx, TSNode node)
{
  if (ctx->failed || ts_node_is_null (node))
    return;
  const char *t = ts_node_type (node);
  if (strcmp (t, "app_exp") == 0)
    {
      emit_app_exp (ctx, node);
      return;
    }
  if (strcmp (t, "vid_exp") == 0)
    {
      emit_open (ctx, "vid");
      emit_raw (ctx, parse_first_named_child (node));
      emit_close (ctx);
      return;
    }
  if (strcmp (t, "scon_exp") == 0)
    {
      emit_open (ctx, "scon");
      emit_raw (ctx, parse_first_named_child (node));
      emit_close (ctx);
      return;
    }
  if (strcmp (t, "record_exp") == 0)
    {
      uint32_t count = ts_node_named_child_count (node);
      emit_open (ctx, "record_exp");
      for (uint32_t i = 0; i < count && !ctx->failed; i++)
        {
          TSNode row = ts_node_named_child (node, i);
          const char *row_type = ts_node_type (row);
          uint32_t row_count = ts_node_named_child_count (row);
          if (strcmp (row_type, "exprow") == 0 && row_count >= 2)
            {
              emit_open (ctx, "exprow");
              emit_raw (ctx, ts_node_named_child (row, 0));
              emit_exp (ctx, ts_node_named_child (row, 1));
              emit_close (ctx);
            }
          else if (strcmp (row_type, "labvar_exprow") == 0 && row_count >= 1)
            {
              emit_open (ctx, "labvar_exprow");
              emit_raw (ctx, ts_node_named_child (row, 0));
              if (row_count >= 2)
                emit_ty (ctx, ts_node_named_child (row, 1));
              emit_close (ctx);
            }
          else
            emit_node (ctx, row);
        }
      emit_close (ctx);
      return;
    }
  if (strcmp (t, "mrule") == 0)
    {
      emit_open (ctx, "mrule");
      emit_children_generic (ctx, node);
      emit_close (ctx);
      return;
    }
  /* All other expression forms are structural: (tag children...). */
  emit_node (ctx, node);
}

static void
emit_pat (sml_parse_ctx *ctx, TSNode node)
{
  if (ctx->failed || ts_node_is_null (node))
    return;
  const char *t = ts_node_type (node);
  if (strcmp (t, "vid_pat") == 0)
    {
      emit_open (ctx, "vid");
      emit_raw (ctx, parse_first_named_child (node));
      emit_close (ctx);
      return;
    }
  if (strcmp (t, "scon_pat") == 0)
    {
      emit_open (ctx, "scon");
      emit_raw (ctx, parse_first_named_child (node));
      emit_close (ctx);
      return;
    }
  if (strcmp (t, "wildcard_pat") == 0)
    {
      emit_atom (ctx, "_");
      return;
    }
  if (strcmp (t, "app_pat") == 0)
    {
      emit_app_exp (ctx, node);
      return;
    }
  emit_node (ctx, node);
}

static void
emit_ty (sml_parse_ctx *ctx, TSNode node)
{
  if (ctx->failed || ts_node_is_null (node))
    return;
  const char *t = ts_node_type (node);
  if (strcmp (t, "tyvar") == 0 || strcmp (t, "tycon") == 0
      || strcmp (t, "lab") == 0)
    {
      emit_raw (ctx, node);
      return;
    }
  if (strcmp (t, "tyvar_ty") == 0)
    {
      emit_open (ctx, "tyvar");
      emit_raw (ctx, parse_first_named_child (node));
      emit_close (ctx);
      return;
    }
  emit_node (ctx, node);
}

static void
emit_dec (sml_parse_ctx *ctx, TSNode node)
{
  if (ctx->failed || ts_node_is_null (node))
    return;
  const char *t = ts_node_type (node);
  /* Fixity declarations update the environment AND are recorded in the AST
   * so the elaborator sees the same resolution the parser used. */
  if (strcmp (t, "infix_dec") == 0)
    {
      fixity_declare (ctx, node, SML_FIX_INFIX);
      emit_open (ctx, "infix");
      emit_children_generic (ctx, node);
      emit_close (ctx);
      return;
    }
  if (strcmp (t, "infixr_dec") == 0)
    {
      fixity_declare (ctx, node, SML_FIX_INFIXR);
      emit_open (ctx, "infixr");
      emit_children_generic (ctx, node);
      emit_close (ctx);
      return;
    }
  if (strcmp (t, "nonfix_dec") == 0)
    {
      fixity_declare (ctx, node, SML_FIX_NONFIX);
      emit_open (ctx, "nonfix");
      emit_children_generic (ctx, node);
      emit_close (ctx);
      return;
    }
  emit_node (ctx, node);
}

static void
emit_strexp (sml_parse_ctx *ctx, TSNode node)
{
  if (ctx->failed || ts_node_is_null (node))
    return;
  emit_node (ctx, node);
}

static void
emit_strdec (sml_parse_ctx *ctx, TSNode node)
{
  if (ctx->failed || ts_node_is_null (node))
    return;
  const char *t = ts_node_type (node);
  /* strdec can contain core declarations with fixity directives. */
  if (strcmp (t, "infix_dec") == 0 || strcmp (t, "infixr_dec") == 0
      || strcmp (t, "nonfix_dec") == 0)
    {
      emit_dec (ctx, node);
      return;
    }
  emit_node (ctx, node);
}

static void
emit_sigexp (sml_parse_ctx *ctx, TSNode node)
{
  if (ctx->failed || ts_node_is_null (node))
    return;
  emit_node (ctx, node);
}

static void
emit_spec (sml_parse_ctx *ctx, TSNode node)
{
  if (ctx->failed || ts_node_is_null (node))
    return;
  emit_node (ctx, node);
}

/* ---------- top level ---------- */

static bool
parse_scan_errors (TSNode node, ccw_sml_parse_report *report)
{
  bool found = false;
  if (ts_node_is_error (node) || parse_node_is (node, "ERROR"))
    {
      report->error_nodes++;
      found = true;
    }
  if (ts_node_is_missing (node))
    {
      report->missing_nodes++;
      found = true;
    }
  uint32_t count = ts_node_child_count (node);
  for (uint32_t i = 0; i < count; i++)
    if (parse_scan_errors (ts_node_child (node, i), report))
      found = true;
  return found;
}

static void
scan_module_nodes (TSNode node, ccw_sml_parse_report *report)
{
  const char *type = ts_node_type (node);
  if (strcmp (type, "structure_strdec") == 0
      || strcmp (type, "struct_strexp") == 0
      || strcmp (type, "structure_spec") == 0)
    report->structure_count++;
  else if (strcmp (type, "signature_sigdec") == 0
           || strcmp (type, "sig_sigexp") == 0
           || strcmp (type, "sigid_sigexp") == 0)
    report->signature_count++;
  else if (strcmp (type, "functor_fctdec") == 0
           || strcmp (type, "fctapp_strexp") == 0)
    report->functor_count++;
  else if (strcmp (type, "sharing_spec") == 0
           || strcmp (type, "sharingtype_spec") == 0)
    report->sharing_count++;
  else if (strcmp (type, "wheretype_sigexp") == 0)
    report->wheretype_count++;

  for (uint32_t i = 0; i < ts_node_child_count (node); i++)
    scan_module_nodes (ts_node_child (node, i), report);
}

char *
ccw_swaff_parse_sml (const char *source, size_t source_len,
                     ccw_sml_parse_report *report, char **error_message)
{
  if (error_message != NULL)
    *error_message = NULL;
  ccw_sml_parse_report local;
  memset (&local, 0, sizeof (local));
  if (report != NULL)
    memset (report, 0, sizeof (*report));

  if (source == NULL)
    {
      if (error_message != NULL)
        *error_message = parse_strdup ("swaff SML parse: null source");
      return NULL;
    }
  if (source_len > UINT32_MAX)
    {
      if (error_message != NULL)
        *error_message = parse_strdup (
            "swaff SML parse: source too large for Tree-sitter");
      return NULL;
    }

  TSParser *parser = ts_parser_new ();
  const TSLanguage *language = tree_sitter_sml ();
  if (parser == NULL || language == NULL
      || !ts_parser_set_language (parser, language))
    {
      if (parser != NULL)
        ts_parser_delete (parser);
      if (error_message != NULL)
        *error_message = parse_strdup (
            "swaff SML parse: vendored grammar ABI-incompatible");
      return NULL;
    }
  TSTree *tree
      = ts_parser_parse_string (parser, NULL, source, (uint32_t)source_len);
  if (tree == NULL)
    {
      ts_parser_delete (parser);
      if (error_message != NULL)
        *error_message
            = parse_strdup ("swaff SML parse: parser produced no tree");
      return NULL;
    }

  TSNode root = ts_tree_root_node (tree);
  parse_scan_errors (root, &local);
  scan_module_nodes (root, &local);

  sml_parse_ctx ctx;
  memset (&ctx, 0, sizeof (ctx));
  ctx.source = source;
  ctx.source_len = source_len;
  ctx.report = &local;
  fixity_seed_basis (&ctx.fixity);

  emit_open (&ctx, "program");
  uint32_t count = ts_node_named_child_count (root);
  for (uint32_t i = 0; i < count && !ctx.failed; i++)
    {
      TSNode child = ts_node_named_child (root, i);
      if (parse_node_is (child, "block_comment")
          || parse_node_is (child, "line_comment"))
        continue; /* CST trivia discarded (D-0052: not part of the AST) */
      const char *t = ts_node_type (child);
      if (strstr (t, "_exp") != NULL && strstr (t, "_dec") == NULL
          && strstr (t, "strexp") == NULL && strstr (t, "strdec") == NULL
          && strstr (t, "sigdec") == NULL && strstr (t, "fctdec") == NULL)
        emit_exp (&ctx, child);
      else if (strstr (t, "_strdec") != NULL || strstr (t, "_fctdec") != NULL
               || strstr (t, "_sigdec") != NULL
               || strcmp (t, "structure_strdec") == 0)
        emit_strdec (&ctx, child);
      else if (strstr (t, "_dec") != NULL)
        emit_dec (&ctx, child);
      else
        emit_node (&ctx, child);
      local.topdec_count++;
    }
  emit_close (&ctx);

  fixity_free (&ctx.fixity);
  ts_tree_delete (tree);
  ts_parser_delete (parser);

  if (ctx.failed)
    {
      snprintf (local.message, sizeof (local.message), "%s", ctx.failure);
      if (report != NULL)
        *report = local;
      if (error_message != NULL)
        *error_message = parse_strdup (ctx.failure);
      free (ctx.out.s);
      return NULL;
    }
  if (local.error_nodes + local.missing_nodes > 0)
    snprintf (local.message, sizeof (local.message),
              "swaff SML parse: %d ERROR and %d MISSING nodes in CST",
              local.error_nodes, local.missing_nodes);
  local.ast_nodes = (int)(ctx.out.l); /* byte length as a stable size proxy */
  if (report != NULL)
    *report = local;
  return ks_release (&ctx.out);
}

#else /* !CCWEAVE_WITH_TREESITTER */

char *
ccw_swaff_parse_sml (const char *source, size_t source_len,
                     ccw_sml_parse_report *report, char **error_message)
{
  (void)source;
  (void)source_len;
  (void)report;
  if (error_message != NULL)
    *error_message
        = sml_strdup("swaff SML parse: built without vendored Tree-sitter");
  return NULL;
}

#endif
