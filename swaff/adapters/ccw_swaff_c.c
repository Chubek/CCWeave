/* C lowering adapter for Swaff (§6.2).
 *
 * This is the only CCWeave translation unit that includes Tree-sitter
 * headers or names CST node types. It consumes the vendored C grammar,
 * normalizes away punctuation/comments, handles ERROR/MISSING nodes
 * according to the caller's policy, and emits the imperative Kliche
 * stereotype rather than exposing CST details to lower layers. */

#include "ccw_kliche.h"
#include "ccw_swaff_internal.h"
#include "kstring.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const ccw_swaff_frontend g_frontend_c = { "c" };

const ccw_swaff_frontend *
ccw_swaff_frontend_c (void)
{
  return &g_frontend_c;
}

const char *
ccw_swaff_frontend_name (const ccw_swaff_frontend *fe)
{
  return fe ? fe->name : "";
}

static char *
ccw_strdup (const char *s)
{
  if (s == NULL)
    return NULL;
  kstring_t copy = { 0, 0, NULL };
  if (kputs (s, &copy) == EOF)
    return NULL;
  return ks_release (&copy);
}

static void
set_error (char **error_message, const char *msg)
{
  if (error_message != NULL)
    *error_message = ccw_strdup (msg);
}

#ifdef CCWEAVE_WITH_TREESITTER

#include <tree_sitter/api.h>
#include <tree_sitter/tree-sitter-c.h>

#define CCW_C_MAX_NAMES 128
#define CCW_C_MAX_ARGS 32

typedef struct
{
  ccw_ir *ir;
  ccw_node fn;
  const char *source;
  size_t source_len;
  ccw_swaff_error_policy policy;
  ccw_swaff_report *report;
  bool rejected;
  bool failed;
  char failure[256];
  unsigned temp_index;
  unsigned block_index;
  char *locals[CCW_C_MAX_NAMES];
  int local_count;
} ccw_c_lower;

bool
ccw_swaff_available (void)
{
  return true;
}

static void
lower_fail (ccw_c_lower *ctx, const char *msg)
{
  if (ctx->failed)
    return;
  ctx->failed = true;
  snprintf (ctx->failure, sizeof (ctx->failure), "%s", msg);
}

static bool
node_is (TSNode node, const char *type)
{
  return !ts_node_is_null (node) && strcmp (ts_node_type (node), type) == 0;
}

static TSNode
field (TSNode node, const char *name)
{
  return ts_node_child_by_field_name (node, name, (uint32_t)strlen (name));
}

static TSNode
null_node (void)
{
  TSNode node = { { 0, 0, 0, 0 }, NULL, NULL };
  return node;
}

static char *
node_text (TSNode node, const char *source, size_t source_len)
{
  uint32_t start = ts_node_start_byte (node);
  uint32_t end = ts_node_end_byte (node);
  if (end < start || (size_t)end > source_len)
    return NULL;
  size_t n = (size_t)(end - start);
  kstring_t out = { 0, 0, NULL };
  if (kputsn (source + start, (int)n, &out) == EOF)
    return NULL;
  return ks_release (&out);
}

static TSNode
first_named_child (TSNode node)
{
  return ts_node_named_child_count (node) > 0 ? ts_node_named_child (node, 0)
                                              : null_node ();
}

/* Declarators nest through pointer/function/array/parenthesized forms.
 * The identifier is the only declaration name, so recursive discovery is
 * stable across all those shapes. */
static TSNode
declarator_identifier (TSNode node)
{
  if (ts_node_is_null (node))
    return null_node ();
  if (node_is (node, "identifier") || node_is (node, "field_identifier")
      || node_is (node, "statement_identifier"))
    return node;

  TSNode inner = field (node, "declarator");
  if (!ts_node_is_null (inner))
    {
      TSNode found = declarator_identifier (inner);
      if (!ts_node_is_null (found))
        return found;
    }

  uint32_t n = ts_node_named_child_count (node);
  for (uint32_t i = 0; i < n; i++)
    {
      TSNode found = declarator_identifier (ts_node_named_child (node, i));
      if (!ts_node_is_null (found))
        return found;
    }
  return null_node ();
}

static TSNode
find_descendant (TSNode node, const char *type)
{
  if (node_is (node, type))
    return node;
  uint32_t n = ts_node_named_child_count (node);
  for (uint32_t i = 0; i < n; i++)
    {
      TSNode found = find_descendant (ts_node_named_child (node, i), type);
      if (!ts_node_is_null (found))
        return found;
    }
  return null_node ();
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
  uint32_t n = ts_node_child_count (node);
  for (uint32_t i = 0; i < n; i++)
    if (scan_errors (ts_node_child (node, i), report))
      found = true;
  return found;
}

static bool
malformed_node (ccw_c_lower *ctx, TSNode node)
{
  if (!ts_node_is_error (node) && !ts_node_is_missing (node)
      && !node_is (node, "ERROR"))
    return false;
  if (ctx->policy == CCW_SWAFF_REJECT_ON_ERROR)
    {
      ctx->rejected = true;
    }
  else
    {
      ctx->report->recovered_subtrees++;
    }
  return true;
}

static ccw_ir_type
lower_type (TSNode node, const char *source, size_t source_len)
{
  char *text = node_text (node, source, source_len);
  if (text == NULL)
    return CCW_TY_I64;
  ccw_ir_type type = CCW_TY_I64;
  if (strstr (text, "void") != NULL)
    type = CCW_TY_VOID;
  else if (strstr (text, "double") != NULL)
    type = CCW_TY_F64;
  else if (strstr (text, "float") != NULL)
    type = CCW_TY_F32;
  else if (strstr (text, "char") != NULL)
    type = CCW_TY_I8;
  else if (strstr (text, "short") != NULL)
    type = CCW_TY_I16;
  else if (strchr (text, '*') != NULL)
    type = CCW_TY_PTR;
  free (text);
  return type;
}

static bool
is_local (const ccw_c_lower *ctx, const char *name)
{
  for (int i = 0; i < ctx->local_count; i++)
    if (strcmp (ctx->locals[i], name) == 0)
      return true;
  return false;
}

static bool
add_local (ccw_c_lower *ctx, const char *name)
{
  if (is_local (ctx, name))
    return true;
  if (ctx->local_count >= CCW_C_MAX_NAMES)
    {
      lower_fail (ctx, "swaff C: too many local declarations in one function");
      return false;
    }
  ctx->locals[ctx->local_count] = ccw_strdup (name);
  if (ctx->locals[ctx->local_count] == NULL)
    {
      lower_fail (ctx, "swaff C: out of memory");
      return false;
    }
  ctx->local_count++;
  return true;
}

static void
clear_locals (ccw_c_lower *ctx)
{
  for (int i = 0; i < ctx->local_count; i++)
    free (ctx->locals[i]);
  ctx->local_count = 0;
}

static char *
new_temp (ccw_c_lower *ctx)
{
  char name[32];
  snprintf (name, sizeof (name), "c.tmp.%u", ctx->temp_index++);
  return ccw_strdup (name);
}

static char *
new_block_name (ccw_c_lower *ctx, const char *stem)
{
  char name[48];
  snprintf (name, sizeof (name), "c.%s.%u", stem, ctx->block_index++);
  return ccw_strdup (name);
}

static bool
block_terminated (const ccw_ir *ir, ccw_node block)
{
  int count = ccw_ir_block_instr_count (ir, block);
  if (count == 0)
    return false;
  const char *opcode = ccw_ir_instr_opcode (
      ir, ccw_ir_block_instr_ref (ir, block, count - 1));
  return opcode != NULL
         && (strcmp (opcode, "ret") == 0 || strcmp (opcode, "br") == 0
             || strcmp (opcode, "br.cond") == 0);
}

static char *lower_expression (ccw_c_lower *ctx, ccw_node block, TSNode expr);
static void lower_statement (ccw_c_lower *ctx, ccw_node *block,
                             TSNode statement);

static char *
lower_identifier (ccw_c_lower *ctx, ccw_node block, TSNode expr)
{
  char *name = node_text (expr, ctx->source, ctx->source_len);
  if (name == NULL)
    {
      lower_fail (ctx, "swaff C: could not read identifier");
      return NULL;
    }
  if (!is_local (ctx, name))
    return name; /* function parameter */

  char *temp = new_temp (ctx);
  if (temp == NULL
      || ccw_kliche_local_load (ctx->ir, block, temp, name, CCW_TY_I64) == 0)
    {
      free (name);
      free (temp);
      lower_fail (ctx, "swaff C: could not lower local load");
      return NULL;
    }
  free (name);
  return temp;
}

static char *
lower_number (ccw_c_lower *ctx, ccw_node block, TSNode expr)
{
  char *text = node_text (expr, ctx->source, ctx->source_len);
  if (text == NULL)
    return NULL;
  errno = 0;
  char *end = NULL;
  long long value = strtoll (text, &end, 0);
  bool valid = errno == 0 && end != text;
  while (valid && *end != '\0')
    {
      if (*end != 'u' && *end != 'U' && *end != 'l' && *end != 'L')
        valid = false;
      end++;
    }
  free (text);
  if (!valid)
    {
      lower_fail (ctx, "swaff C: unsupported integer literal");
      return NULL;
    }

  char *temp = new_temp (ctx);
  if (temp == NULL
      || ccw_kliche_int_const (ctx->ir, block, temp, (int64_t)value) == 0)
    {
      free (temp);
      lower_fail (ctx, "swaff C: could not lower integer literal");
      return NULL;
    }
  return temp;
}

static const char *
binary_opcode (const char *operator_text)
{
  struct op_map
  {
    const char *c;
    const char *ir;
  };
  static const struct op_map map[]
      = { { "+", "iadd" },     { "-", "isub" },       { "*", "imul" },
          { "/", "idiv" },     { "%", "irem" },       { "<<", "shl" },
          { ">>", "shr" },     { "&", "iand" },       { "|", "ior" },
          { "^", "ixor" },     { "==", "icmp.eq" },   { "!=", "icmp.ne" },
          { "<", "icmp.lt" },  { "<=", "icmp.le" },   { ">", "icmp.gt" },
          { ">=", "icmp.ge" }, { "&&", "logic.and" }, { "||", "logic.or" } };
  for (size_t i = 0; i < sizeof (map) / sizeof (map[0]); i++)
    if (strcmp (operator_text, map[i].c) == 0)
      return map[i].ir;
  return NULL;
}

static char *
lower_binary (ccw_c_lower *ctx, ccw_node block, TSNode expr)
{
  TSNode left = field (expr, "left");
  TSNode right = field (expr, "right");
  TSNode op = field (expr, "operator");
  char *op_text = node_text (op, ctx->source, ctx->source_len);
  const char *opcode = binary_opcode (op_text ? op_text : "");
  if (opcode == NULL)
    {
      free (op_text);
      lower_fail (ctx, "swaff C: unsupported binary operator");
      return NULL;
    }

  char *lhs = lower_expression (ctx, block, left);
  char *rhs = lower_expression (ctx, block, right);
  char *dest = new_temp (ctx);
  if (lhs == NULL || rhs == NULL || dest == NULL
      || ccw_kliche_binary (ctx->ir, block, opcode, dest, lhs, rhs,
                            strncmp (opcode, "icmp.", 5) == 0 ? CCW_TY_I1
                                                              : CCW_TY_I64)
             == 0)
    {
      free (lhs);
      free (rhs);
      free (dest);
      free (op_text);
      if (!ctx->failed)
        lower_fail (ctx, "swaff C: could not lower binary expression");
      return NULL;
    }
  free (lhs);
  free (rhs);
  free (op_text);
  return dest;
}

static char *
lower_unary (ccw_c_lower *ctx, ccw_node block, TSNode expr)
{
  TSNode op = field (expr, "operator");
  TSNode argument = field (expr, "argument");
  char *op_text = node_text (op, ctx->source, ctx->source_len);
  const char *opcode = NULL;
  if (op_text != NULL)
    {
      if (strcmp (op_text, "-") == 0)
        opcode = "ineg";
      else if (strcmp (op_text, "~") == 0)
        opcode = "inot";
      else if (strcmp (op_text, "!") == 0)
        opcode = "logic.not";
      else if (strcmp (op_text, "+") == 0)
        opcode = "imov";
    }
  if (opcode == NULL)
    {
      free (op_text);
      lower_fail (ctx, "swaff C: unsupported unary operator");
      return NULL;
    }
  char *operand = lower_expression (ctx, block, argument);
  char *dest = new_temp (ctx);
  if (operand == NULL || dest == NULL
      || ccw_kliche_unary (ctx->ir, block, opcode, dest, operand,
                           strcmp (opcode, "logic.not") == 0 ? CCW_TY_I1
                                                             : CCW_TY_I64)
             == 0)
    {
      free (operand);
      free (dest);
      free (op_text);
      if (!ctx->failed)
        lower_fail (ctx, "swaff C: could not lower unary expression");
      return NULL;
    }
  free (operand);
  free (op_text);
  return dest;
}

static char *
lower_call (ccw_c_lower *ctx, ccw_node block, TSNode expr)
{
  TSNode function = field (expr, "function");
  if (!node_is (function, "identifier"))
    {
      lower_fail (ctx, "swaff C: only direct function calls are supported");
      return NULL;
    }
  char *callee = node_text (function, ctx->source, ctx->source_len);
  TSNode arguments = field (expr, "arguments");
  const char *args[CCW_C_MAX_ARGS];
  char *owned[CCW_C_MAX_ARGS];
  int arg_count = 0;
  memset (owned, 0, sizeof (owned));

  uint32_t n = ts_node_named_child_count (arguments);
  for (uint32_t i = 0; i < n; i++)
    {
      TSNode arg = ts_node_named_child (arguments, i);
      if (arg_count >= CCW_C_MAX_ARGS)
        {
          lower_fail (ctx, "swaff C: too many call arguments");
          break;
        }
      owned[arg_count] = lower_expression (ctx, block, arg);
      if (owned[arg_count] == NULL)
        break;
      args[arg_count] = owned[arg_count];
      arg_count++;
    }

  char *dest = ctx->failed ? NULL : new_temp (ctx);
  if (callee == NULL || dest == NULL
      || ccw_kliche_call (ctx->ir, block, dest, callee, args, arg_count,
                          CCW_TY_I64)
             == 0)
    {
      if (!ctx->failed)
        lower_fail (ctx, "swaff C: could not lower function call");
      free (dest);
      dest = NULL;
    }
  for (int i = 0; i < arg_count; i++)
    free (owned[i]);
  free (callee);
  return dest;
}

static char *
lower_assignment (ccw_c_lower *ctx, ccw_node block, TSNode expr)
{
  TSNode left = field (expr, "left");
  TSNode right = field (expr, "right");
  TSNode op = field (expr, "operator");
  if (!node_is (left, "identifier"))
    {
      lower_fail (ctx, "swaff C: only local-variable assignment is supported");
      return NULL;
    }
  char *name = node_text (left, ctx->source, ctx->source_len);
  char *op_text = node_text (op, ctx->source, ctx->source_len);
  if (name == NULL || !is_local (ctx, name))
    {
      free (name);
      free (op_text);
      lower_fail (ctx, "swaff C: assignment target is not a local variable");
      return NULL;
    }

  char *value = NULL;
  if (op_text != NULL && strcmp (op_text, "=") != 0)
    {
      char basic_op[3] = { op_text[0], '\0', '\0' };
      if (op_text[0] == '<' || op_text[0] == '>')
        basic_op[1] = op_text[1];
      const char *opcode = binary_opcode (basic_op);
      char *current = lower_identifier (ctx, block, left);
      char *rhs = lower_expression (ctx, block, right);
      value = new_temp (ctx);
      if (opcode == NULL || current == NULL || rhs == NULL || value == NULL
          || ccw_kliche_binary (ctx->ir, block, opcode, value, current, rhs,
                                CCW_TY_I64)
                 == 0)
        {
          free (current);
          free (rhs);
          free (value);
          free (name);
          free (op_text);
          if (!ctx->failed)
            lower_fail (ctx, "swaff C: could not lower compound assignment");
          return NULL;
        }
      free (current);
      free (rhs);
    }
  else
    {
      value = lower_expression (ctx, block, right);
    }

  if (value == NULL
      || ccw_kliche_local_store (ctx->ir, block, name, value) == 0)
    {
      free (value);
      free (name);
      free (op_text);
      if (!ctx->failed)
        lower_fail (ctx, "swaff C: could not lower assignment");
      return NULL;
    }
  free (name);
  free (op_text);
  return value;
}

static char *
lower_expression (ccw_c_lower *ctx, ccw_node block, TSNode expr)
{
  if (ctx->failed || ctx->rejected || ts_node_is_null (expr))
    return NULL;
  if (malformed_node (ctx, expr))
    return NULL;

  const char *type = ts_node_type (expr);
  if (strcmp (type, "identifier") == 0)
    return lower_identifier (ctx, block, expr);
  if (strcmp (type, "number_literal") == 0
      || strcmp (type, "char_literal") == 0)
    return lower_number (ctx, block, expr);
  if (strcmp (type, "binary_expression") == 0)
    return lower_binary (ctx, block, expr);
  if (strcmp (type, "unary_expression") == 0)
    return lower_unary (ctx, block, expr);
  if (strcmp (type, "assignment_expression") == 0)
    return lower_assignment (ctx, block, expr);
  if (strcmp (type, "call_expression") == 0)
    return lower_call (ctx, block, expr);
  if (strcmp (type, "parenthesized_expression") == 0
      || strcmp (type, "expression_statement") == 0)
    return lower_expression (ctx, block, first_named_child (expr));

  ctx->report->unsupported_nodes++;
  lower_fail (ctx, "swaff C: unsupported expression");
  return NULL;
}

static void
lower_declarator (ccw_c_lower *ctx, ccw_node block, TSNode declarator,
                  ccw_ir_type type)
{
  TSNode value = null_node ();
  TSNode bare = declarator;
  if (node_is (declarator, "init_declarator"))
    {
      bare = field (declarator, "declarator");
      value = field (declarator, "value");
    }
  TSNode id = declarator_identifier (bare);
  char *name = node_text (id, ctx->source, ctx->source_len);
  if (name == NULL || !add_local (ctx, name)
      || ccw_kliche_local_alloc (ctx->ir, block, name, type) == 0)
    {
      free (name);
      if (!ctx->failed)
        lower_fail (ctx, "swaff C: could not lower local declaration");
      return;
    }
  ctx->report->declarations_lowered++;
  if (!ts_node_is_null (value))
    {
      char *initial = lower_expression (ctx, block, value);
      if (initial != NULL)
        {
          if (ccw_kliche_local_store (ctx->ir, block, name, initial) == 0)
            lower_fail (ctx, "swaff C: could not lower local initializer");
          free (initial);
        }
    }
  free (name);
}

static void
lower_declaration (ccw_c_lower *ctx, ccw_node block, TSNode declaration)
{
  ccw_ir_type type
      = lower_type (field (declaration, "type"), ctx->source, ctx->source_len);
  uint32_t n = ts_node_child_count (declaration);
  for (uint32_t i = 0; i < n && !ctx->failed; i++)
    {
      TSNode child = ts_node_child (declaration, i);
      const char *field_name = ts_node_field_name_for_child (declaration, i);
      if (field_name != NULL && strcmp (field_name, "declarator") == 0)
        lower_declarator (ctx, block, child, type);
    }
}

static void
lower_return (ccw_c_lower *ctx, ccw_node block, TSNode statement)
{
  TSNode value = first_named_child (statement);
  char *reg
      = ts_node_is_null (value) ? NULL : lower_expression (ctx, block, value);
  if (!ctx->failed && ccw_kliche_return (ctx->ir, block, reg) == 0)
    lower_fail (ctx, "swaff C: could not lower return statement");
  free (reg);
}

static void
lower_if (ccw_c_lower *ctx, ccw_node *block, TSNode statement)
{
  TSNode condition_node = field (statement, "condition");
  TSNode condition_expr = node_is (condition_node, "parenthesized_expression")
                              ? first_named_child (condition_node)
                              : condition_node;
  char *condition = lower_expression (ctx, *block, condition_expr);
  char *then_name = new_block_name (ctx, "then");
  char *else_name = new_block_name (ctx, "else");
  char *merge_name = new_block_name (ctx, "merge");
  if (condition == NULL || then_name == NULL || else_name == NULL
      || merge_name == NULL)
    {
      free (condition);
      free (then_name);
      free (else_name);
      free (merge_name);
      if (!ctx->failed)
        lower_fail (ctx, "swaff C: could not construct if blocks");
      return;
    }

  ccw_node then_block = ccw_ir_block_add (ctx->ir, ctx->fn, then_name);
  ccw_node else_block = ccw_ir_block_add (ctx->ir, ctx->fn, else_name);
  ccw_node merge_block = ccw_ir_block_add (ctx->ir, ctx->fn, merge_name);
  if (then_block == 0 || else_block == 0 || merge_block == 0
      || ccw_kliche_branch_if (ctx->ir, *block, condition, then_name,
                               else_name)
             == 0)
    {
      lower_fail (ctx, "swaff C: could not lower conditional branch");
    }

  TSNode consequence = field (statement, "consequence");
  TSNode alternative = field (statement, "alternative");
  lower_statement (ctx, &then_block, consequence);
  if (!ctx->failed && !block_terminated (ctx->ir, then_block))
    ccw_kliche_jump (ctx->ir, then_block, merge_name);

  if (!ts_node_is_null (alternative))
    {
      TSNode else_body = node_is (alternative, "else_clause")
                             ? first_named_child (alternative)
                             : alternative;
      lower_statement (ctx, &else_block, else_body);
    }
  if (!ctx->failed && !block_terminated (ctx->ir, else_block))
    ccw_kliche_jump (ctx->ir, else_block, merge_name);

  *block = merge_block;
  free (condition);
  free (then_name);
  free (else_name);
  free (merge_name);
}

static void
lower_compound (ccw_c_lower *ctx, ccw_node *block, TSNode compound)
{
  uint32_t n = ts_node_named_child_count (compound);
  for (uint32_t i = 0; i < n && !ctx->failed && !ctx->rejected; i++)
    lower_statement (ctx, block, ts_node_named_child (compound, i));
}

static void
lower_statement (ccw_c_lower *ctx, ccw_node *block, TSNode statement)
{
  if (ctx->failed || ctx->rejected || ts_node_is_null (statement))
    return;
  if (malformed_node (ctx, statement))
    return;
  if (node_is (statement, "comment"))
    return;

  const char *type = ts_node_type (statement);
  if (strcmp (type, "compound_statement") == 0)
    {
      lower_compound (ctx, block, statement);
    }
  else if (strcmp (type, "declaration") == 0)
    {
      lower_declaration (ctx, *block, statement);
    }
  else if (strcmp (type, "return_statement") == 0)
    {
      lower_return (ctx, *block, statement);
    }
  else if (strcmp (type, "if_statement") == 0)
    {
      lower_if (ctx, block, statement);
    }
  else if (strcmp (type, "expression_statement") == 0)
    {
      char *unused = lower_expression (ctx, *block, statement);
      free (unused);
    }
  else
    {
      ctx->report->unsupported_nodes++;
      lower_fail (ctx, "swaff C: unsupported statement");
    }
  ctx->report->statements_lowered++;
}

static void
lower_parameters (ccw_c_lower *ctx, TSNode declarator)
{
  TSNode list = find_descendant (declarator, "parameter_list");
  if (ts_node_is_null (list))
    return;
  uint32_t n = ts_node_named_child_count (list);
  for (uint32_t i = 0; i < n && !ctx->failed; i++)
    {
      TSNode parameter = ts_node_named_child (list, i);
      if (!node_is (parameter, "parameter_declaration"))
        continue;
      TSNode decl = field (parameter, "declarator");
      TSNode id = declarator_identifier (decl);
      if (ts_node_is_null (id))
        continue; /* e.g. `(void)` */
      char *name = node_text (id, ctx->source, ctx->source_len);
      ccw_ir_type type = lower_type (field (parameter, "type"), ctx->source,
                                     ctx->source_len);
      if (name == NULL
          || ccw_ir_function_add_param (ctx->ir, ctx->fn, type, name)
                 != CCW_OK)
        lower_fail (ctx, "swaff C: could not lower function parameter");
      free (name);
    }
}

static void
lower_function (ccw_c_lower *ctx, TSNode function)
{
  TSNode declarator = field (function, "declarator");
  TSNode id = declarator_identifier (declarator);
  char *name = node_text (id, ctx->source, ctx->source_len);
  if (name == NULL)
    {
      lower_fail (ctx, "swaff C: function has no declarator name");
      return;
    }
  ccw_ir_type result
      = lower_type (field (function, "type"), ctx->source, ctx->source_len);
  ctx->fn = ccw_ir_function_add (ctx->ir, name, result);
  free (name);
  if (ctx->fn == 0)
    {
      lower_fail (ctx, "swaff C: could not create function");
      return;
    }

  ctx->temp_index = 0;
  ctx->block_index = 0;
  clear_locals (ctx);
  lower_parameters (ctx, declarator);

  ccw_node block = ccw_ir_block_add (ctx->ir, ctx->fn, "entry");
  TSNode body = field (function, "body");
  lower_compound (ctx, &block, body);
  if (!ctx->failed && !ctx->rejected && !block_terminated (ctx->ir, block))
    ccw_kliche_return (ctx->ir, block, NULL);
  ctx->report->functions_lowered++;
  clear_locals (ctx);
}

ccw_ir *
ccw_swaff_lower (const ccw_swaff_frontend *fe, const char *source,
                 size_t source_len, const char *module_name,
                 ccw_profile profile, ccw_swaff_error_policy policy,
                 ccw_swaff_report *report, char **error_message)
{
  if (error_message != NULL)
    *error_message = NULL;
  if (fe == ccw_swaff_frontend_lua ())
    return ccw_swaff_lower_lua (fe, source, source_len, module_name, profile,
                                policy, report, error_message);
  if (fe == ccw_swaff_frontend_ocaml ())
    return ccw_swaff_lower_ocaml (fe, source, source_len, module_name, profile,
                                  policy, report, error_message);
  if (fe == ccw_swaff_frontend_sml ())
    return ccw_swaff_lower_sml (fe, source, source_len, module_name, profile,
                                policy, report, error_message);
  if (fe == ccw_swaff_frontend_delphi ())
    return ccw_swaff_lower_delphi (fe, source, source_len, module_name,
                                   profile, policy, report, error_message);

  ccw_swaff_report local;
  memset (&local, 0, sizeof (local));
  if (report != NULL)
    memset (report, 0, sizeof (*report));

  if (fe != &g_frontend_c || source == NULL || module_name == NULL)
    {
      set_error (error_message,
                 "swaff C: invalid frontend, source, or module name");
      return NULL;
    }
  if (source_len > UINT32_MAX)
    {
      set_error (error_message,
                 "swaff C: source is too large for Tree-sitter");
      return NULL;
    }

  TSParser *parser = ts_parser_new ();
  const TSLanguage *language = tree_sitter_c ();
  if (parser == NULL || language == NULL
      || !ts_parser_set_language (parser, language))
    {
      if (parser != NULL)
        ts_parser_delete (parser);
      set_error (error_message,
                 "swaff C: vendored C grammar is ABI-incompatible");
      return NULL;
    }
  TSTree *tree
      = ts_parser_parse_string (parser, NULL, source, (uint32_t)source_len);
  if (tree == NULL)
    {
      ts_parser_delete (parser);
      set_error (error_message, "swaff C: parser produced no syntax tree");
      return NULL;
    }

  TSNode root = ts_tree_root_node (tree);
  bool has_errors = scan_errors (root, &local);
  if (has_errors && policy == CCW_SWAFF_REJECT_ON_ERROR)
    {
      snprintf (local.message, sizeof (local.message),
                "swaff C: rejected CST with %d ERROR and %d MISSING nodes",
                local.error_nodes, local.missing_nodes);
      if (report != NULL)
        *report = local;
      set_error (error_message, local.message);
      ts_tree_delete (tree);
      ts_parser_delete (parser);
      return NULL;
    }

  ccw_ir *ir = ccw_ir_module_create (module_name, profile);
  if (ir == NULL)
    {
      ts_tree_delete (tree);
      ts_parser_delete (parser);
      set_error (error_message, "swaff C: out of memory");
      return NULL;
    }
  ccw_c_lower ctx;
  memset (&ctx, 0, sizeof (ctx));
  ctx.ir = ir;
  ctx.source = source;
  ctx.source_len = source_len;
  ctx.policy = policy;
  ctx.report = &local;

  uint32_t n = ts_node_named_child_count (root);
  for (uint32_t i = 0; i < n && !ctx.failed && !ctx.rejected; i++)
    {
      TSNode child = ts_node_named_child (root, i);
      if (node_is (child, "comment") || malformed_node (&ctx, child))
        continue;
      if (node_is (child, "function_definition"))
        {
          lower_function (&ctx, child);
        }
      else if (!node_is (child, "preproc_include")
               && !node_is (child, "preproc_def")
               && !node_is (child, "preproc_function_def")
               && !node_is (child, "declaration")
               && !node_is (child, "type_definition"))
        {
          local.unsupported_nodes++;
        }
    }

  clear_locals (&ctx);
  ts_tree_delete (tree);
  ts_parser_delete (parser);

  if (ctx.rejected || ctx.failed)
    {
      const char *message
          = ctx.failed ? ctx.failure : "swaff C: rejected malformed subtree";
      snprintf (local.message, sizeof (local.message), "%s", message);
      if (report != NULL)
        *report = local;
      set_error (error_message, message);
      ccw_ir_module_destroy (ir);
      return NULL;
    }
  if (has_errors)
    snprintf (local.message, sizeof (local.message),
              "swaff C: recovered %d malformed subtrees",
              local.recovered_subtrees);
  if (report != NULL)
    *report = local;
  return ir;
}

#else

bool
ccw_swaff_available (void)
{
  return false;
}

ccw_ir *
ccw_swaff_lower (const ccw_swaff_frontend *fe, const char *source,
                 size_t source_len, const char *module_name,
                 ccw_profile profile, ccw_swaff_error_policy policy,
                 ccw_swaff_report *report, char **error_message)
{
  if (fe == ccw_swaff_frontend_ocaml ())
    return ccw_swaff_lower_ocaml (fe, source, source_len, module_name, profile,
                                  policy, report, error_message);
  if (fe == ccw_swaff_frontend_lua ())
    return ccw_swaff_lower_lua (fe, source, source_len, module_name, profile,
                                policy, report, error_message);
  if (fe == ccw_swaff_frontend_sml ())
    return ccw_swaff_lower_sml (fe, source, source_len, module_name, profile,
                                policy, report, error_message);
  if (fe == ccw_swaff_frontend_delphi ())
    return ccw_swaff_lower_delphi (fe, source, source_len, module_name,
                                   profile, policy, report, error_message);
  (void)fe;
  (void)source;
  (void)source_len;
  (void)module_name;
  (void)profile;
  (void)policy;
  (void)report;
  set_error (error_message,
             "swaff C: built without vendored Tree-sitter support");
  return NULL;
}

#endif
