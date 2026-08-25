/* OCaml lowering adapter for Swaff (§6.2).
 *
 * The adapter consumes only the implementation-file grammar. It normalizes
 * punctuation, comments, and type annotations, then emits functional Kliche
 * calls for higher-order application and core Kliche construction patterns
 * for scalar expressions and control flow. Tree-sitter node names remain
 * confined to this translation unit. */

#include "ccw_kliche.h"
#include "ccw_swaff_internal.h"
#include "kstring.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const ccw_swaff_frontend g_frontend_ocaml = { "ocaml" };

const ccw_swaff_frontend *
ccw_swaff_frontend_ocaml (void)
{
  return &g_frontend_ocaml;
}

static char *
ocaml_strdup (const char *s)
{
  if (s == NULL)
    return NULL;
  kstring_t copy = { 0, 0, NULL };
  if (kputs (s, &copy) == EOF)
    return NULL;
  return ks_release (&copy);
}

static void
ocaml_set_error (char **error_message, const char *message)
{
  if (error_message != NULL)
    *error_message = ocaml_strdup (message);
}

#ifdef CCWEAVE_WITH_TREESITTER

#include <tree_sitter/api.h>
#include <tree_sitter/tree-sitter-ocaml.h>

#define CCW_OCAML_MAX_NAMES 128
#define CCW_OCAML_MAX_ARGS 32

typedef struct
{
  char *name;
  char *reg;
} ccw_ocaml_alias;

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
  char *parameters[CCW_OCAML_MAX_NAMES];
  int parameter_count;
  ccw_ocaml_alias aliases[CCW_OCAML_MAX_NAMES];
  int alias_count;
} ccw_ocaml_lower;

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
  size_t n = (size_t)(end - start);
  kstring_t text = { 0, 0, NULL };
  if (kputsn (source + start, (int)n, &text) == EOF)
    return NULL;
  return ks_release (&text);
}

static void
lower_fail (ccw_ocaml_lower *ctx, const char *message)
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
new_temp (ccw_ocaml_lower *ctx)
{
  char name[40];
  snprintf (name, sizeof (name), "ocaml.tmp.%u", ctx->temp_index++);
  return ocaml_strdup (name);
}

static char *
new_block_name (ccw_ocaml_lower *ctx, const char *stem)
{
  char name[48];
  snprintf (name, sizeof (name), "ocaml.%s.%u", stem, ctx->block_index++);
  return ocaml_strdup (name);
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
clear_function_names (ccw_ocaml_lower *ctx)
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
add_parameter_name (ccw_ocaml_lower *ctx, const char *name)
{
  if (ctx->parameter_count >= CCW_OCAML_MAX_NAMES)
    {
      lower_fail (ctx, "swaff OCaml: too many parameters");
      return false;
    }
  ctx->parameters[ctx->parameter_count] = ocaml_strdup (name);
  if (ctx->parameters[ctx->parameter_count] == NULL)
    {
      lower_fail (ctx, "swaff OCaml: out of memory");
      return false;
    }
  ctx->parameter_count++;
  return true;
}

static bool
is_parameter (const ccw_ocaml_lower *ctx, const char *name)
{
  for (int i = 0; i < ctx->parameter_count; i++)
    if (strcmp (ctx->parameters[i], name) == 0)
      return true;
  return false;
}

static const char *
lookup_alias (const ccw_ocaml_lower *ctx, const char *name)
{
  for (int i = ctx->alias_count - 1; i >= 0; i--)
    if (strcmp (ctx->aliases[i].name, name) == 0)
      return ctx->aliases[i].reg;
  return NULL;
}

static bool
push_alias (ccw_ocaml_lower *ctx, const char *name, const char *reg)
{
  if (ctx->alias_count >= CCW_OCAML_MAX_NAMES)
    {
      lower_fail (ctx, "swaff OCaml: too many nested local bindings");
      return false;
    }
  ccw_ocaml_alias *alias = &ctx->aliases[ctx->alias_count];
  alias->name = ocaml_strdup (name);
  alias->reg = ocaml_strdup (reg);
  if (alias->name == NULL || alias->reg == NULL)
    {
      free (alias->name);
      free (alias->reg);
      alias->name = NULL;
      alias->reg = NULL;
      lower_fail (ctx, "swaff OCaml: out of memory");
      return false;
    }
  ctx->alias_count++;
  return true;
}

static void
pop_aliases (ccw_ocaml_lower *ctx, int saved_count)
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
  if (node_is (pattern, "value_pattern") || node_is (pattern, "value_name"))
    return pattern;
  if (node_is (pattern, "typed_pattern"))
    {
      TSNode inner = field (pattern, "pattern");
      return simple_pattern_name (inner);
    }
  if (node_is (pattern, "parenthesized_pattern"))
    {
      TSNode inner = first_named_child (pattern);
      return simple_pattern_name (inner);
    }
  return null_node ();
}

static char *lower_expression (ccw_ocaml_lower *ctx, ccw_node *block,
                               TSNode expression);

static char *
lower_opaque_expression (ccw_ocaml_lower *ctx, ccw_node *block, TSNode node)
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
      lower_fail (ctx, "swaff OCaml: could not lower opaque expression");
      return NULL;
    }
  {
    char *source = node_text (node, ctx->source, ctx->source_len);
    (void)ccw_ir_attr_set (ctx->ir, ins, "source", source ? source : "");
    free (source);
  }
  return dest;
}

static char *
lower_number (ccw_ocaml_lower *ctx, ccw_node block, TSNode node)
{
  char *text = node_text (node, ctx->source, ctx->source_len);
  if (text == NULL)
    {
      lower_fail (ctx, "swaff OCaml: could not read integer literal");
      return NULL;
    }

  size_t length = strlen (text);
  char *normalized = (char *)malloc (length + 1u);
  if (normalized == NULL)
    {
      free (text);
      lower_fail (ctx, "swaff OCaml: out of memory");
      return NULL;
    }
  size_t out = 0;
  for (size_t i = 0; i < length; i++)
    if (text[i] != '_')
      normalized[out++] = text[i];
  if (out > 0
      && (normalized[out - 1] == 'l' || normalized[out - 1] == 'L'
          || normalized[out - 1] == 'n'))
    out--;
  normalized[out] = '\0';

  errno = 0;
  char *end = NULL;
  long long value = strtoll (normalized, &end, 0);
  bool valid = errno == 0 && end != normalized && *end == '\0';
  free (normalized);
  free (text);
  if (!valid)
    {
      lower_fail (ctx, "swaff OCaml: unsupported numeric literal");
      return NULL;
    }

  char *dest = new_temp (ctx);
  if (dest == NULL
      || ccw_kliche_int_const (ctx->ir, block, dest, (int64_t)value) == 0)
    {
      free (dest);
      lower_fail (ctx, "swaff OCaml: could not lower integer literal");
      return NULL;
    }
  return dest;
}

static char *
lower_boolean (ccw_ocaml_lower *ctx, ccw_node block, TSNode node)
{
  char *text = node_text (node, ctx->source, ctx->source_len);
  bool value = text != NULL && strcmp (text, "true") == 0;
  free (text);
  char *dest = new_temp (ctx);
  if (dest == NULL
      || ccw_kliche_int_const (ctx->ir, block, dest, value ? 1 : 0) == 0)
    {
      free (dest);
      lower_fail (ctx, "swaff OCaml: could not lower boolean literal");
      return NULL;
    }
  return dest;
}

static char *
lower_value_path (ccw_ocaml_lower *ctx, TSNode node)
{
  char *name = node_text (node, ctx->source, ctx->source_len);
  if (name == NULL)
    {
      lower_fail (ctx, "swaff OCaml: could not read value path");
      return NULL;
    }
  const char *alias = lookup_alias (ctx, name);
  if (alias != NULL)
    {
      free (name);
      return ocaml_strdup (alias);
    }
  return name;
}

static const char *
binary_opcode (const char *operator_text)
{
  struct operator_map
  {
    const char *ocaml;
    const char *ir;
  };
  static const struct operator_map operators[]
      = { { "+", "iadd" },     { "-", "isub" },     { "*", "imul" },
          { "/", "idiv" },     { "mod", "irem" },   { "land", "iand" },
          { "lor", "ior" },    { "lxor", "ixor" },  { "lsl", "shl" },
          { "lsr", "shr" },    { "asr", "shr" },    { "=", "icmp.eq" },
          { "<>", "icmp.ne" }, { "<", "icmp.lt" },  { "<=", "icmp.le" },
          { ">", "icmp.gt" },  { ">=", "icmp.ge" }, { "&&", "logic.and" },
          { "||", "logic.or" } };
  for (size_t i = 0; i < sizeof (operators) / sizeof (operators[0]); i++)
    if (strcmp (operator_text, operators[i].ocaml) == 0)
      return operators[i].ir;
  return NULL;
}

static char *
lower_binary (ccw_ocaml_lower *ctx, ccw_node *block, TSNode node)
{
  char *operator_text
      = node_text (field (node, "operator"), ctx->source, ctx->source_len);
  const char *opcode = binary_opcode (operator_text ? operator_text : "");
  if (opcode == NULL)
    {
      free (operator_text);
      return lower_opaque_expression (ctx, block, node);
    }

  char *left = lower_expression (ctx, block, field (node, "left"));
  char *right = lower_expression (ctx, block, field (node, "right"));
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
      free (operator_text);
      if (!ctx->failed)
        lower_fail (ctx, "swaff OCaml: could not lower infix expression");
      return NULL;
    }
  free (left);
  free (right);
  free (operator_text);
  return dest;
}

static char *
lower_unary (ccw_ocaml_lower *ctx, ccw_node *block, TSNode node)
{
  char *operator_text
      = node_text (field (node, "operator"), ctx->source, ctx->source_len);
  const char *opcode = NULL;
  if (operator_text != NULL)
    {
      if (strcmp (operator_text, "-") == 0
          || strcmp (operator_text, "-.") == 0)
        opcode = "ineg";
      else if (strcmp (operator_text, "+") == 0
               || strcmp (operator_text, "+.") == 0)
        opcode = "imov";
      else if (strcmp (operator_text, "not") == 0)
        opcode = "logic.not";
    }
  if (opcode == NULL)
    {
      free (operator_text);
      return lower_opaque_expression (ctx, block, node);
    }
  TSNode operand_node = field (node, "expression");
  char *operand = lower_expression (ctx, block, operand_node);
  char *dest = new_temp (ctx);
  if (operand == NULL || dest == NULL
      || ccw_kliche_unary (ctx->ir, *block, opcode, dest, operand,
                           strcmp (opcode, "logic.not") == 0 ? CCW_TY_I1
                                                             : CCW_TY_I64)
             == 0)
    {
      free (operand);
      free (dest);
      free (operator_text);
      if (!ctx->failed)
        lower_fail (ctx, "swaff OCaml: could not lower prefix expression");
      return NULL;
    }
  free (operand);
  free (operator_text);
  return dest;
}

static int
collect_arguments (ccw_ocaml_lower *ctx, ccw_node *block, TSNode node,
                   char **owned, const char **arguments)
{
  int count = 0;
  uint32_t children = ts_node_child_count (node);
  for (uint32_t i = 0; i < children; i++)
    {
      const char *field_name = ts_node_field_name_for_child (node, i);
      if (field_name == NULL || strcmp (field_name, "argument") != 0)
        continue;
      if (count >= CCW_OCAML_MAX_ARGS)
        {
          lower_fail (ctx, "swaff OCaml: too many application arguments");
          return count;
        }
      TSNode argument = ts_node_child (node, i);
      if (node_is (argument, "labeled_argument"))
        {
          argument = field (argument, "expression");
          if (ts_node_is_null (argument))
            {
              owned[count] = lower_opaque_expression (ctx, block, argument);
              arguments[count] = owned[count];
              count++;
              continue;
            }
        }
      owned[count] = lower_expression (ctx, block, argument);
      if (owned[count] == NULL)
        return count;
      arguments[count] = owned[count];
      count++;
    }
  return count;
}

static char *
lower_application (ccw_ocaml_lower *ctx, ccw_node *block, TSNode node)
{
  TSNode function_node = field (node, "function");
  if (!node_is (function_node, "value_path"))
    return lower_opaque_expression (ctx, block, node);
  char *function_name
      = node_text (function_node, ctx->source, ctx->source_len);
  if (function_name == NULL)
    {
      lower_fail (ctx, "swaff OCaml: could not read applied function");
      return NULL;
    }
  const char *aliased = lookup_alias (ctx, function_name);
  const char *callable = aliased != NULL ? aliased : function_name;

  char *owned[CCW_OCAML_MAX_ARGS] = { 0 };
  const char *arguments[CCW_OCAML_MAX_ARGS];
  int argument_count = collect_arguments (ctx, block, node, owned, arguments);
  char *result = NULL;

  if (!ctx->failed && (is_parameter (ctx, function_name) || aliased != NULL))
    {
      char *current = ocaml_strdup (callable);
      for (int i = 0; i < argument_count && current != NULL; i++)
        {
          char *next = new_temp (ctx);
          if (next == NULL
              || ccw_kliche_closure_apply (ctx->ir, *block, next, current,
                                           arguments[i])
                     == 0)
            {
              free (next);
              free (current);
              current = NULL;
              lower_fail (
                  ctx,
                  "swaff OCaml: could not lower higher-order application");
              break;
            }
          free (current);
          current = next;
        }
      result = current;
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
          lower_fail (ctx, "swaff OCaml: could not lower direct application");
        }
    }

  for (int i = 0; i < argument_count; i++)
    free (owned[i]);
  free (function_name);
  return result;
}

static char *
lower_if (ccw_ocaml_lower *ctx, ccw_node *block, TSNode node)
{
  char *condition = lower_expression (ctx, block, field (node, "condition"));
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
        lower_fail (ctx, "swaff OCaml: could not construct if expression");
      return NULL;
    }

  ccw_node then_block = ccw_ir_block_add (ctx->ir, ctx->fn, then_name);
  ccw_node else_block = ccw_ir_block_add (ctx->ir, ctx->fn, else_name);
  ccw_node merge_block = ccw_ir_block_add (ctx->ir, ctx->fn, merge_name);
  if (then_block == 0 || else_block == 0 || merge_block == 0
      || ccw_kliche_branch_if (ctx->ir, *block, condition, then_name,
                               else_name)
             == 0)
    {
      lower_fail (ctx, "swaff OCaml: could not lower conditional branch");
    }

  TSNode then_clause = null_node ();
  TSNode else_clause = null_node ();
  uint32_t child_count = ts_node_named_child_count (node);
  for (uint32_t i = 0; i < child_count; i++)
    {
      TSNode child = ts_node_named_child (node, i);
      if (node_is (child, "then_clause"))
        then_clause = child;
      else if (node_is (child, "else_clause"))
        else_clause = child;
    }

  char *then_value
      = ctx->failed ? NULL
                    : lower_expression (ctx, &then_block,
                                        field (then_clause, "expression"));
  if (!ctx->failed
      && (then_value == NULL
          || ccw_kliche_local_store (ctx->ir, then_block, slot, then_value)
                 == 0
          || (!block_terminated (ctx->ir, then_block)
              && ccw_kliche_jump (ctx->ir, then_block, merge_name) == 0)))
    lower_fail (ctx, "swaff OCaml: could not lower then expression");

  char *else_value = NULL;
  if (!ctx->failed && !ts_node_is_null (else_clause))
    else_value = lower_expression (ctx, &else_block,
                                   field (else_clause, "expression"));
  else if (!ctx->failed)
    else_value = lower_boolean (ctx, else_block, else_clause);
  if (!ctx->failed
      && (else_value == NULL
          || ccw_kliche_local_store (ctx->ir, else_block, slot, else_value)
                 == 0
          || (!block_terminated (ctx->ir, else_block)
              && ccw_kliche_jump (ctx->ir, else_block, merge_name) == 0)))
    lower_fail (ctx, "swaff OCaml: could not lower else expression");

  char *result = ctx->failed ? NULL : new_temp (ctx);
  if (!ctx->failed
      && (result == NULL
          || ccw_kliche_local_load (ctx->ir, merge_block, result, slot,
                                    CCW_TY_I64)
                 == 0))
    {
      free (result);
      result = NULL;
      lower_fail (ctx, "swaff OCaml: could not merge if expression");
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
lower_sequence (ccw_ocaml_lower *ctx, ccw_node *block, TSNode node)
{
  char *result = NULL;
  uint32_t count = ts_node_named_child_count (node);
  for (uint32_t i = 0; i < count && !ctx->failed; i++)
    {
      TSNode child = ts_node_named_child (node, i);
      if (node_is (child, "attribute_id") || node_is (child, "comment"))
        continue;
      free (result);
      result = lower_expression (ctx, block, child);
    }
  return result;
}

static char *
lower_let_expression (ccw_ocaml_lower *ctx, ccw_node *block, TSNode node)
{
  TSNode definition = first_named_child (node);
  TSNode binding = null_node ();
  int binding_count = 0;
  uint32_t count = ts_node_named_child_count (definition);
  for (uint32_t i = 0; i < count; i++)
    {
      TSNode child = ts_node_named_child (definition, i);
      if (node_is (child, "let_binding"))
        {
          binding = child;
          binding_count++;
        }
    }
  if (binding_count != 1)
    return lower_opaque_expression (ctx, block, node);
  TSNode name_node = simple_pattern_name (field (binding, "pattern"));
  if (ts_node_is_null (name_node))
    return lower_opaque_expression (ctx, block, node);
  for (uint32_t i = 0; i < ts_node_named_child_count (binding); i++)
    if (node_is (ts_node_named_child (binding, i), "parameter"))
      {
        return lower_opaque_expression (ctx, block, node);
      }

  char *name = node_text (name_node, ctx->source, ctx->source_len);
  char *value = lower_expression (ctx, block, field (binding, "body"));
  int saved_aliases = ctx->alias_count;
  if (name == NULL || value == NULL || !push_alias (ctx, name, value))
    {
      free (name);
      free (value);
      if (!ctx->failed)
        lower_fail (ctx, "swaff OCaml: could not lower local let binding");
      return NULL;
    }
  ctx->report->declarations_lowered++;
  char *result = lower_expression (ctx, block, field (node, "body"));
  pop_aliases (ctx, saved_aliases);
  free (name);
  free (value);
  return result;
}

static char *
lower_expression (ccw_ocaml_lower *ctx, ccw_node *block, TSNode expression)
{
  if (ctx->failed || ts_node_is_null (expression))
    return NULL;
  const char *type = ts_node_type (expression);
  if (strcmp (type, "number") == 0 || strcmp (type, "signed_number") == 0)
    return lower_number (ctx, *block, expression);
  if (strcmp (type, "boolean") == 0)
    return lower_boolean (ctx, *block, expression);
  if (strcmp (type, "value_path") == 0)
    return lower_value_path (ctx, expression);
  if (strcmp (type, "infix_expression") == 0)
    return lower_binary (ctx, block, expression);
  if (strcmp (type, "prefix_expression") == 0
      || strcmp (type, "sign_expression") == 0)
    return lower_unary (ctx, block, expression);
  if (strcmp (type, "application_expression") == 0)
    return lower_application (ctx, block, expression);
  if (strcmp (type, "if_expression") == 0)
    return lower_if (ctx, block, expression);
  if (strcmp (type, "let_expression") == 0)
    return lower_let_expression (ctx, block, expression);
  if (strcmp (type, "sequence_expression") == 0)
    return lower_sequence (ctx, block, expression);
  if (strcmp (type, "parenthesized_expression") == 0
      || strcmp (type, "typed_expression") == 0)
    return lower_expression (ctx, block, field (expression, "expression"));
  if (strcmp (type, "unit") == 0)
    return lower_boolean (ctx, *block, expression);

  return lower_opaque_expression (ctx, block, expression);
}

static bool
add_function_parameter (ccw_ocaml_lower *ctx, TSNode parameter)
{
  TSNode pattern = field (parameter, "pattern");
  TSNode name_node = simple_pattern_name (pattern);
  if (ts_node_is_null (name_node))
    {
      char synthetic[32];
      snprintf (synthetic, sizeof (synthetic), "ocaml.arg.%u",
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
        lower_fail (ctx, "swaff OCaml: could not lower function parameter");
      return false;
    }
  free (name);
  return true;
}

static void
lower_function_binding (ccw_ocaml_lower *ctx, TSNode binding)
{
  TSNode name_node = simple_pattern_name (field (binding, "pattern"));
  if (ts_node_is_null (name_node))
    return;
  char *name = node_text (name_node, ctx->source, ctx->source_len);
  if (name == NULL)
    {
      lower_fail (ctx, "swaff OCaml: could not read function name");
      return;
  }

  TSNode body = field (binding, "body");
  bool body_is_fun = node_is (body, "fun_expression");
  uint32_t child_count = ts_node_named_child_count (binding);
  ctx->fn = ccw_ir_function_add (ctx->ir, name, CCW_TY_I64);
  free (name);
  if (ctx->fn == 0)
    {
      lower_fail (ctx, "swaff OCaml: could not create function");
      return;
    }
  clear_function_names (ctx);
  ctx->temp_index = 0;
  ctx->block_index = 0;

  for (uint32_t i = 0; i < child_count && !ctx->failed; i++)
    {
      TSNode child = ts_node_named_child (binding, i);
      if (node_is (child, "parameter"))
        add_function_parameter (ctx, child);
    }
  if (body_is_fun)
    {
      uint32_t fun_children = ts_node_named_child_count (body);
      for (uint32_t i = 0; i < fun_children && !ctx->failed; i++)
        {
          TSNode child = ts_node_named_child (body, i);
          if (node_is (child, "parameter"))
            add_function_parameter (ctx, child);
        }
      body = field (body, "body");
    }

  ccw_node block = ccw_ir_block_add (ctx->ir, ctx->fn, "entry");
  char *result = ctx->failed ? NULL : lower_expression (ctx, &block, body);
  if (!ctx->failed
      && (result == NULL
          || (!block_terminated (ctx->ir, block)
              && ccw_kliche_return (ctx->ir, block, result) == 0)))
    lower_fail (ctx, "swaff OCaml: could not lower function result");
  free (result);
  if (!ctx->failed)
    ctx->report->functions_lowered++;
  clear_function_names (ctx);
}

static void
lower_value_definition (ccw_ocaml_lower *ctx, TSNode definition)
{
  uint32_t count = ts_node_named_child_count (definition);
  for (uint32_t i = 0; i < count && !ctx->failed; i++)
    {
      TSNode child = ts_node_named_child (definition, i);
      if (node_is (child, "let_binding"))
        lower_function_binding (ctx, child);
    }
}

ccw_ir *
ccw_swaff_lower_ocaml (const ccw_swaff_frontend *fe, const char *source,
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

  if (fe != &g_frontend_ocaml || source == NULL || module_name == NULL)
    {
      ocaml_set_error (
          error_message,
          "swaff OCaml: invalid frontend, source, or module name");
      return NULL;
    }
  if (source_len > UINT32_MAX)
    {
      ocaml_set_error (error_message,
                       "swaff OCaml: source is too large for Tree-sitter");
      return NULL;
    }

  TSParser *parser = ts_parser_new ();
  const TSLanguage *language = tree_sitter_ocaml ();
  if (parser == NULL || language == NULL
      || !ts_parser_set_language (parser, language))
    {
      if (parser != NULL)
        ts_parser_delete (parser);
      ocaml_set_error (
          error_message,
          "swaff OCaml: vendored OCaml grammar is ABI-incompatible");
      return NULL;
    }
  TSTree *tree
      = ts_parser_parse_string (parser, NULL, source, (uint32_t)source_len);
  if (tree == NULL)
    {
      ts_parser_delete (parser);
      ocaml_set_error (error_message,
                       "swaff OCaml: parser produced no syntax tree");
      return NULL;
    }

  TSNode root = ts_tree_root_node (tree);
  bool has_errors = scan_errors (root, &local);
  if (has_errors && policy == CCW_SWAFF_REJECT_ON_ERROR)
    {
      snprintf (local.message, sizeof (local.message),
                "swaff OCaml: rejected CST with %d ERROR and %d MISSING nodes",
                local.error_nodes, local.missing_nodes);
      if (report != NULL)
        *report = local;
      ocaml_set_error (error_message, local.message);
      ts_tree_delete (tree);
      ts_parser_delete (parser);
      return NULL;
    }

  ccw_ir *ir = ccw_ir_module_create (module_name, profile);
  if (ir == NULL)
    {
      ts_tree_delete (tree);
      ts_parser_delete (parser);
      ocaml_set_error (error_message, "swaff OCaml: out of memory");
      return NULL;
    }
  ccw_ocaml_lower ctx;
  memset (&ctx, 0, sizeof (ctx));
  ctx.ir = ir;
  ctx.source = source;
  ctx.source_len = source_len;
  ctx.report = &local;

  uint32_t count = ts_node_named_child_count (root);
  for (uint32_t i = 0; i < count && !ctx.failed; i++)
    {
      TSNode child = ts_node_named_child (root, i);
      if (node_is (child, "comment") || node_is (child, "shebang"))
        continue;
      if (subtree_is_malformed (child))
        {
          if (policy == CCW_SWAFF_RECOVER_ON_ERROR)
            {
              local.recovered_subtrees++;
              continue;
            }
        }
      if (node_is (child, "value_definition"))
        lower_value_definition (&ctx, child);
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
      ocaml_set_error (error_message, ctx.failure);
      ccw_ir_module_destroy (ir);
      return NULL;
    }
  if (has_errors)
    snprintf (local.message, sizeof (local.message),
              "swaff OCaml: recovered %d malformed subtrees",
              local.recovered_subtrees);
  if (report != NULL)
    *report = local;
  return ir;
}

#else

ccw_ir *
ccw_swaff_lower_ocaml (const ccw_swaff_frontend *fe, const char *source,
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
  ocaml_set_error (error_message,
                   "swaff OCaml: built without vendored Tree-sitter support");
  return NULL;
}

#endif
