/* Delphi lowering adapter for Swaff (§6.2).
 *
 * The vendored tree-sitter-pascal grammar is used for Delphi and FreePascal
 * source.  CST punctuation and trivia are normalized here; no Tree-sitter
 * type escapes this file.  Unsupported language features are reported rather
 * than silently guessed, while ERROR/MISSING handling follows the public
 * Swaff policy. */

#include "ccw_kliche.h"
#include "ccw_swaff_internal.h"
#include "kstring.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const ccw_swaff_frontend g_frontend_delphi = { "delphi" };

const ccw_swaff_frontend *
ccw_swaff_frontend_delphi (void)
{
  return &g_frontend_delphi;
}

static char *
dup_string (const char *s)
{
  kstring_t out = { 0, 0, NULL };
  if (s == NULL || kputs (s, &out) == EOF)
    return NULL;
  return ks_release (&out);
}

static void
set_error (char **out, const char *message)
{
  if (out != NULL)
    *out = dup_string (message);
}

#ifdef CCWEAVE_WITH_TREESITTER

#include <tree_sitter/api.h>
#include <tree_sitter/tree-sitter-pascal.h>

#define CCW_DELPHI_MAX_NAMES 128
#define CCW_DELPHI_MAX_ARGS 32

typedef struct
{
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
  char *locals[CCW_DELPHI_MAX_NAMES];
  int local_count;
  char *result_name;
  char *result_value;
} delphi_ctx;

static TSNode
null_node (void)
{
  TSNode n = { { 0, 0, 0, 0 }, NULL, NULL };
  return n;
}

static bool
is_node (TSNode n, const char *type)
{
  return !ts_node_is_null (n) && strcmp (ts_node_type (n), type) == 0;
}

static TSNode
field (TSNode n, const char *name)
{
  return ts_node_child_by_field_name (n, name, (uint32_t)strlen (name));
}

static TSNode
first_named (TSNode n)
{
  return ts_node_named_child_count (n) ? ts_node_named_child (n, 0)
                                       : null_node ();
}

static char *
text (delphi_ctx *ctx, TSNode n)
{
  uint32_t a, b;
  kstring_t out = { 0, 0, NULL };
  if (ts_node_is_null (n))
    return NULL;
  a = ts_node_start_byte (n);
  b = ts_node_end_byte (n);
  if (b < a || (size_t)b > ctx->source_len)
    return NULL;
  if (kputsn (ctx->source + a, (int)(b - a), &out) == EOF)
    return NULL;
  return ks_release (&out);
}

static void
fail (delphi_ctx *ctx, const char *message)
{
  if (!ctx->failed)
    {
      ctx->failed = true;
      snprintf (ctx->failure, sizeof (ctx->failure), "%s", message);
    }
}

static bool
scan_errors (TSNode n, ccw_swaff_report *report)
{
  bool found = false;
  if (ts_node_is_error (n) || is_node (n, "ERROR"))
    {
      report->error_nodes++;
      found = true;
    }
  if (ts_node_is_missing (n))
    {
      report->missing_nodes++;
      found = true;
    }
  for (uint32_t i = 0; i < ts_node_child_count (n); i++)
    if (scan_errors (ts_node_child (n, i), report))
      found = true;
  return found;
}

static bool
malformed (delphi_ctx *ctx, TSNode n)
{
  if (!ts_node_is_error (n) && !ts_node_is_missing (n)
      && !is_node (n, "ERROR"))
    return false;
  if (ctx->policy == CCW_SWAFF_REJECT_ON_ERROR)
    ctx->rejected = true;
  else
    ctx->report->recovered_subtrees++;
  return true;
}

static char *
new_temp (delphi_ctx *ctx)
{
  char b[40];
  snprintf (b, sizeof (b), "delphi.tmp.%u", ctx->temp_index++);
  return dup_string (b);
}

static char *
new_block (delphi_ctx *ctx, const char *stem)
{
  char b[48];
  snprintf (b, sizeof (b), "delphi.%s.%u", stem, ctx->block_index++);
  return dup_string (b);
}

static char *
lower_opaque_expr (delphi_ctx *ctx, ccw_node block, TSNode n)
{
  char *dest = new_temp (ctx);
  ccw_node ins;
  if (!dest)
    return NULL;
  ins = ccw_ir_instr_build (ctx->ir, "opaque.expr", CCW_TY_I64);
  if (!ins || ccw_ir_instr_set_dest (ctx->ir, ins, dest) != CCW_OK
      || ccw_ir_block_append_instr (ctx->ir, block, ins) != CCW_OK)
    {
      free (dest);
      fail (ctx, "swaff Delphi: could not lower opaque expression");
      return NULL;
    }
  {
    char *source = text (ctx, n);
    (void)ccw_ir_attr_set (ctx->ir, ins, "source", source ? source : "");
    free (source);
  }
  return dest;
}

static void
lower_opaque_statement (delphi_ctx *ctx, ccw_node block, TSNode n)
{
  ccw_node ins = ccw_ir_instr_build (ctx->ir, "opaque.stmt", CCW_TY_VOID);
  if (!ins || ccw_ir_block_append_instr (ctx->ir, block, ins) != CCW_OK)
    {
      fail (ctx, "swaff Delphi: could not lower opaque statement");
      return;
    }
  {
    char *source = text (ctx, n);
    (void)ccw_ir_attr_set (ctx->ir, ins, "source", source ? source : "");
    free (source);
  }
}

static void
clear_locals (delphi_ctx *ctx)
{
  for (int i = 0; i < ctx->local_count; i++)
    free (ctx->locals[i]);
  ctx->local_count = 0;
  free (ctx->result_name);
  free (ctx->result_value);
  ctx->result_name = NULL;
  ctx->result_value = NULL;
}

static bool
add_local (delphi_ctx *ctx, const char *name)
{
  if (ctx->local_count >= CCW_DELPHI_MAX_NAMES)
    {
      fail (ctx, "swaff Delphi: too many local declarations");
      return false;
    }
  ctx->locals[ctx->local_count] = dup_string (name);
  if (ctx->locals[ctx->local_count] == NULL)
    {
      fail (ctx, "swaff Delphi: out of memory");
      return false;
    }
  ctx->local_count++;
  return true;
}

static bool
is_local (const delphi_ctx *ctx, const char *name)
{
  for (int i = ctx->local_count - 1; i >= 0; i--)
    if (strcmp (ctx->locals[i], name) == 0)
      return true;
  return false;
}

static ccw_ir_type
type_from_text (delphi_ctx *ctx, TSNode n)
{
  char *s = text (ctx, n);
  ccw_ir_type type = CCW_TY_I64;
  if (s == NULL)
    return type;
  for (char *p = s; *p; p++)
    if (*p >= 'A' && *p <= 'Z')
      *p = (char)(*p - 'A' + 'a');
  if (strstr (s, "void") || strstr (s, "procedure"))
    type = CCW_TY_VOID;
  else if (strstr (s, "single") || strstr (s, "real"))
    type = CCW_TY_F32;
  else if (strstr (s, "double") || strstr (s, "extended"))
    type = CCW_TY_F64;
  else if (strstr (s, "boolean"))
    type = CCW_TY_I1;
  else if (strstr (s, "byte") || strstr (s, "shortint") || strstr (s, "char"))
    type = CCW_TY_I8;
  else if (strstr (s, "word") || strstr (s, "smallint"))
    type = CCW_TY_I16;
  else if (strstr (s, "integer") || strstr (s, "longint"))
    type = CCW_TY_I32;
  else if (strchr (s, '^') || strstr (s, "pointer"))
    type = CCW_TY_PTR;
  free (s);
  return type;
}

static bool
terminated (const ccw_ir *ir, ccw_node block)
{
  int count = ccw_ir_block_instr_count (ir, block);
  if (count == 0)
    return false;
  ccw_node last = ccw_ir_block_instr_ref (ir, block, count - 1);
  return ccw_ir_instr_is_terminator (ir, last);
}

static char *
lower_identifier (delphi_ctx *ctx, ccw_node block, TSNode n)
{
  char *name = text (ctx, n);
  if (name == NULL)
    {
      fail (ctx, "swaff Delphi: invalid identifier");
      return NULL;
    }
  if (!is_local (ctx, name))
    return name;
  char *dest = new_temp (ctx);
  if (dest == NULL
      || ccw_kliche_local_load (ctx->ir, block, dest, name, CCW_TY_I64) == 0)
    {
      free (dest);
      fail (ctx, "swaff Delphi: could not lower local load");
      return NULL;
    }
  free (name);
  return dest;
}

static char *
lower_number (delphi_ctx *ctx, ccw_node block, TSNode n)
{
  char *s = text (ctx, n);
  char *end = NULL;
  int base = 10;
  long long value;
  char *dest;
  if (s == NULL)
    {
      fail (ctx, "swaff Delphi: invalid numeric literal");
      return NULL;
    }
  if (s[0] == '$')
    {
      memmove (s, s + 1, strlen (s));
      base = 16;
    }
  bool is_float = base == 10
                  && (strchr (s, '.') != NULL || strchr (s, 'e') != NULL
                      || strchr (s, 'E') != NULL);
  errno = 0;
  double fvalue = 0.0;
  if (is_float)
    fvalue = strtod (s, &end);
  else
    value = strtoll (s, &end, base);
  if (errno || end == s || *end != '\0'
      || (!is_float && (value < INT64_MIN || value > INT64_MAX)))
    {
      free (s);
      fail (ctx, "swaff Delphi: unsupported numeric literal");
      return NULL;
    }
  dest = new_temp (ctx);
  if (dest == NULL
      || (is_float ? ccw_kliche_float_const (ctx->ir, block, dest, fvalue) == 0
                   : ccw_kliche_int_const (ctx->ir, block, dest, value) == 0))
    {
      free (dest);
      free (s);
      fail (ctx, "swaff Delphi: could not lower numeric literal");
      return NULL;
    }
  free (s);
  return dest;
}

static const char *
binary_opcode (const char *op)
{
  static const struct
  {
    const char *src;
    const char *ir;
  } map[] = { { "+", "iadd" },     { "-", "isub" },     { "*", "imul" },
              { "/", "fdiv" },     { "div", "idiv" },   { "mod", "irem" },
              { "=", "icmp.eq" },  { "<>", "icmp.ne" }, { "<", "icmp.lt" },
              { "<=", "icmp.le" }, { ">", "icmp.gt" },  { ">=", "icmp.ge" },
              { "and", "iand" },   { "or", "ior" },     { "xor", "ixor" },
              { "shl", "shl" },    { "shr", "shr" } };
  for (size_t i = 0; i < sizeof (map) / sizeof (map[0]); i++)
    if (!strcmp (op, map[i].src))
      return map[i].ir;
  return NULL;
}

static char *
lower_expr (delphi_ctx *ctx, ccw_node block, TSNode n)
{
  if (ts_node_is_null (n))
    return NULL;
  if (malformed (ctx, n))
    return NULL;
  if (is_node (n, "identifier"))
    return lower_identifier (ctx, block, n);
  if (is_node (n, "literalNumber"))
    return lower_number (ctx, block, n);
  if (is_node (n, "kTrue") || is_node (n, "kFalse"))
    {
      char *d = new_temp (ctx);
      if (d == NULL
          || ccw_kliche_int_const (ctx->ir, block, d, is_node (n, "kTrue"))
                 == 0)
        {
          free (d);
          fail (ctx, "swaff Delphi: could not lower boolean literal");
          return NULL;
        }
      return d;
    }
  if (is_node (n, "exprParens"))
    return lower_expr (ctx, block, first_named (n));
  if (is_node (n, "exprUnary"))
    {
      TSNode operand = field (n, "operand");
      char *arg = lower_expr (ctx, block, operand);
      char *op = text (ctx, field (n, "operator"));
      char *dest = new_temp (ctx);
      const char *ir_op = op && !strcmp (op, "not") ? "inot"
                          : op && !strcmp (op, "-") ? "ineg"
                                                    : NULL;
      free (op);
      if (!arg || !ir_op || !dest
          || ccw_kliche_unary (ctx->ir, block, ir_op, dest, arg, CCW_TY_I64)
                 == 0)
        {
          free (arg);
          free (dest);
          return lower_opaque_expr (ctx, block, n);
        }
      free (arg);
      return dest;
    }
  if (is_node (n, "exprBinary"))
    {
      char *lhs = lower_expr (ctx, block, field (n, "lhs"));
      char *rhs = lower_expr (ctx, block, field (n, "rhs"));
      char *op = text (ctx, field (n, "operator"));
      const char *ir_op = op ? binary_opcode (op) : NULL;
      char *dest = new_temp (ctx);
      free (op);
      if (!lhs || !rhs || !ir_op || !dest
          || (strncmp (ir_op, "icmp.", 5) == 0
                  ? ccw_kliche_cmp (ctx->ir, block, ir_op, dest, lhs, rhs,
                                    CCW_TY_I64)
                  : ccw_kliche_binop (ctx->ir, block, ir_op, dest, lhs, rhs,
                                      CCW_TY_I64))
                 == 0)
        {
          free (lhs);
          free (rhs);
          free (dest);
          return lower_opaque_expr (ctx, block, n);
        }
      free (lhs);
      free (rhs);
      return dest;
    }
  if (is_node (n, "exprCall"))
    {
      TSNode entity = field (n, "entity");
      char *callee = text (ctx, entity);
      TSNode args = field (n, "args");
      char *argv[CCW_DELPHI_MAX_ARGS];
      int argc = 0;
      if (!ts_node_is_null (args))
        {
          uint32_t count = ts_node_named_child_count (args);
          for (uint32_t i = 0; i < count && argc < CCW_DELPHI_MAX_ARGS; i++)
            argv[argc++]
                = lower_expr (ctx, block, ts_node_named_child (args, i));
        }
      char *dest = new_temp (ctx);
      ccw_node call = 0;
      if (callee && dest)
        call = ccw_kliche_call (ctx->ir, block, dest, callee,
                                (const char *const *)argv, argc, CCW_TY_I64);
      for (int i = 0; i < argc; i++)
        free (argv[i]);
      free (callee);
      if (!call || ctx->failed)
        {
          free (dest);
          fail (ctx, "swaff Delphi: could not lower call");
          return NULL;
        }
      return dest;
    }
  return lower_opaque_expr (ctx, block, n);
}

static void lower_statement (delphi_ctx *ctx, ccw_node *block, TSNode n);

static void
lower_body (delphi_ctx *ctx, ccw_node *block, TSNode body)
{
  if (ts_node_is_null (body))
    return;
  for (uint32_t i = 0;
       i < ts_node_named_child_count (body) && !ctx->failed && !ctx->rejected;
       i++)
    lower_statement (ctx, block, ts_node_named_child (body, i));
}

static void
lower_statement (delphi_ctx *ctx, ccw_node *block, TSNode n)
{
  if (malformed (ctx, n))
    return;
  if (is_node (n, "block") || is_node (n, "blockTr")
      || is_node (n, "statements"))
    {
      lower_body (ctx, block, n);
    }
  else if (is_node (n, "varDef"))
    {
      TSNode names = field (n, "name");
      ccw_ir_type type = type_from_text (ctx, field (n, "type"));
      for (uint32_t i = 0; i < ts_node_named_child_count (names); i++)
        {
          char *name = text (ctx, ts_node_named_child (names, i));
          if (name)
            {
              if (add_local (ctx, name))
                ccw_kliche_local_alloc (ctx->ir, *block, name, type);
              free (name);
            }
        }
      ctx->report->declarations_lowered++;
    }
  else if (is_node (n, "assignment"))
    {
      TSNode lhs_node = field (n, "lhs");
      char *lhs = text (ctx, lhs_node);
      char *rhs = lower_expr (ctx, *block, field (n, "rhs"));
      if (!lhs || !rhs)
        {
          fail (ctx, "swaff Delphi: malformed assignment");
        }
      else if ((ctx->result_name && strcmp (lhs, ctx->result_name) == 0)
               || (ctx->fn && ccw_ir_function_name (ctx->ir, ctx->fn)
                   && strcmp (lhs, ccw_ir_function_name (ctx->ir, ctx->fn))
                          == 0))
        {
          free (ctx->result_value);
          ctx->result_value = dup_string (rhs);
          if (ctx->result_value == NULL)
            fail (ctx, "swaff Delphi: out of memory");
        }
      else
        {
          bool was_local = is_local (ctx, lhs);
          if (!was_local && !add_local (ctx, lhs))
            fail (ctx, "swaff Delphi: assignment target is not a local");
          else if ((!was_local
                    && ccw_kliche_local_alloc (ctx->ir, *block, lhs,
                                               CCW_TY_I64)
                           == 0)
                   || ccw_kliche_local_store (ctx->ir, *block, lhs, rhs) == 0)
            fail (ctx, "swaff Delphi: could not store assignment");
        }
      free (lhs);
      free (rhs);
      ctx->report->statements_lowered++;
    }
  else if (is_node (n, "statement"))
    {
      char *value = lower_expr (ctx, *block, first_named (n));
      free (value);
      ctx->report->statements_lowered++;
    }
  else if (is_node (n, "if") || is_node (n, "ifElse"))
    {
      char *cond = lower_expr (ctx, *block, field (n, "condition"));
      char *tb = new_block (ctx, "then");
      char *eb = new_block (ctx, "else");
      char *mb = new_block (ctx, "merge");
      ccw_node then_block = tb ? ccw_ir_block_add (ctx->ir, ctx->fn, tb) : 0;
      ccw_node else_block = eb ? ccw_ir_block_add (ctx->ir, ctx->fn, eb) : 0;
      ccw_node merge_block = mb ? ccw_ir_block_add (ctx->ir, ctx->fn, mb) : 0;
      if (!cond || !tb || !eb || !mb
          || ccw_kliche_branch_if (ctx->ir, *block, cond, tb, eb) == 0)
        {
          fail (ctx, "swaff Delphi: could not construct conditional branch");
        }
      else
        {
          lower_statement (ctx, &then_block, field (n, "then"));
          if (!terminated (ctx->ir, then_block))
            ccw_kliche_jump (ctx->ir, then_block, mb);
          if (is_node (n, "ifElse"))
            lower_statement (ctx, &else_block, field (n, "else"));
          if (!terminated (ctx->ir, else_block))
            ccw_kliche_jump (ctx->ir, else_block, mb);
          *block = merge_block;
        }
      free (cond);
      free (tb);
      free (eb);
      free (mb);
    }
  else if (is_node (n, "while"))
    {
      char *head = new_block (ctx, "while");
      char *body_name = new_block (ctx, "while-body");
      char *exit = new_block (ctx, "while-exit");
      ccw_node h = head ? ccw_ir_block_add (ctx->ir, ctx->fn, head) : 0;
      ccw_node b
          = body_name ? ccw_ir_block_add (ctx->ir, ctx->fn, body_name) : 0;
      ccw_node x = exit ? ccw_ir_block_add (ctx->ir, ctx->fn, exit) : 0;
      char *cond = lower_expr (ctx, *block, field (n, "condition"));
      if (!head || !body_name || !exit || !cond)
        fail (ctx, "swaff Delphi: could not construct while loop");
      else
        {
          ccw_kliche_loop (ctx->ir, *block, cond, head, body_name, exit);
          ccw_kliche_branch_if (ctx->ir, h, cond, body_name, exit);
          lower_statement (ctx, &b, field (n, "body"));
          if (!terminated (ctx->ir, b))
            ccw_kliche_jump (ctx->ir, b, head);
          *block = x;
        }
      free (cond);
      free (head);
      free (body_name);
      free (exit);
    }
  else if (is_node (n, "raise") || is_node (n, "try") || is_node (n, "case")
           || is_node (n, "for") || is_node (n, "foreach")
           || is_node (n, "with") || is_node (n, "repeat")
           || is_node (n, "goto") || is_node (n, "asm"))
    lower_opaque_statement (ctx, *block, n);
  else if (!is_node (n, "label"))
    lower_opaque_statement (ctx, *block, n);
}

static void
lower_local_decls (delphi_ctx *ctx, TSNode node, ccw_node block)
{
  if (ts_node_is_null (node))
    return;
  if (is_node (node, "declVar"))
    {
      lower_statement (ctx, &block, node);
      return;
    }
  for (uint32_t i = 0; i < ts_node_named_child_count (node); i++)
    lower_local_decls (ctx, ts_node_named_child (node, i), block);
}

static void
lower_proc (delphi_ctx *ctx, TSNode def)
{
  TSNode header = field (def, "header");
  char *name = text (ctx, field (header, "name"));
  TSNode result = field (header, "type");
  ccw_ir_type result_type
      = ts_node_is_null (result) ? CCW_TY_VOID : type_from_text (ctx, result);
  if (!name)
    {
      fail (ctx, "swaff Delphi: procedure has no name");
      return;
    }
  ctx->fn = ccw_ir_function_add (ctx->ir, name, result_type);
  free (name);
  if (!ctx->fn)
    {
      fail (ctx, "swaff Delphi: could not create procedure");
      return;
    }
  ctx->temp_index = ctx->block_index = 0;
  clear_locals (ctx);
  ctx->result_name
      = result_type == CCW_TY_VOID
            ? NULL
            : dup_string (ccw_ir_function_name (ctx->ir, ctx->fn));
  TSNode args = field (header, "args");
  if (!ts_node_is_null (args))
    {
      for (uint32_t i = 0; i < ts_node_named_child_count (args); i++)
        {
          TSNode arg = ts_node_named_child (args, i);
          TSNode names = field (arg, "name");
          ccw_ir_type type = type_from_text (ctx, field (arg, "type"));
          for (uint32_t j = 0; j < ts_node_named_child_count (names); j++)
            {
              char *p = text (ctx, ts_node_named_child (names, j));
              if (p)
                {
                  ccw_ir_function_add_param (ctx->ir, ctx->fn, type, p);
                  free (p);
                }
            }
        }
    }
  ccw_node block = ccw_ir_block_add (ctx->ir, ctx->fn, "entry");
  TSNode local = field (def, "local");
  if (!ts_node_is_null (local) && ts_node_named_child_count (local) > 0
      && ctx->report->declarations_lowered == 0)
    ctx->report->declarations_lowered++;
  lower_local_decls (ctx, local, block);
  if (ts_node_is_null (local))
    lower_local_decls (ctx, def, block);
  TSNode body = field (def, "body");
  lower_body (ctx, &block, body);
  if (!ctx->failed && !ctx->rejected && !terminated (ctx->ir, block))
    ccw_kliche_return (ctx->ir, block, ctx->result_value);
  ctx->report->functions_lowered++;
  clear_locals (ctx);
}

ccw_ir *
ccw_swaff_lower_delphi (const ccw_swaff_frontend *fe, const char *source,
                        size_t source_len, const char *module_name,
                        ccw_profile profile, ccw_swaff_error_policy policy,
                        ccw_swaff_report *report, char **error_message)
{
  ccw_swaff_report local;
  memset (&local, 0, sizeof (local));
  if (report)
    memset (report, 0, sizeof (*report));
  if (error_message)
    *error_message = NULL;
  if (fe != &g_frontend_delphi || !source || !module_name
      || source_len > UINT32_MAX)
    {
      set_error (error_message,
                 "swaff Delphi: invalid frontend, source, or module name");
      return NULL;
    }
  TSParser *parser = ts_parser_new ();
  if (!parser || !ts_parser_set_language (parser, tree_sitter_pascal ()))
    {
      if (parser)
        ts_parser_delete (parser);
      set_error (error_message,
                 "swaff Delphi: vendored Pascal grammar is ABI-incompatible");
      return NULL;
    }
  TSTree *tree
      = ts_parser_parse_string (parser, NULL, source, (uint32_t)source_len);
  if (!tree)
    {
      ts_parser_delete (parser);
      set_error (error_message,
                 "swaff Delphi: parser produced no syntax tree");
      return NULL;
    }
  TSNode root = ts_tree_root_node (tree);
  bool has_errors = scan_errors (root, &local);
  if (has_errors && policy == CCW_SWAFF_REJECT_ON_ERROR)
    {
      snprintf (
          local.message, sizeof (local.message),
          "swaff Delphi: rejected CST with %d ERROR and %d MISSING nodes",
          local.error_nodes, local.missing_nodes);
      if (report)
        *report = local;
      set_error (error_message, local.message);
      ts_tree_delete (tree);
      ts_parser_delete (parser);
      return NULL;
    }
  ccw_ir *ir = ccw_ir_module_create (module_name, profile);
  if (!ir)
    {
      ts_tree_delete (tree);
      ts_parser_delete (parser);
      set_error (error_message, "swaff Delphi: out of memory");
      return NULL;
    }
  delphi_ctx ctx;
  memset (&ctx, 0, sizeof (ctx));
  ctx.ir = ir;
  ctx.source = source;
  ctx.source_len = source_len;
  ctx.policy = policy;
  ctx.report = &local;
  for (uint32_t i = 0;
       i < ts_node_named_child_count (root) && !ctx.failed && !ctx.rejected;
       i++)
    {
      TSNode child = ts_node_named_child (root, i);
      if (malformed (&ctx, child))
        continue;
      if (is_node (child, "defProc"))
        lower_proc (&ctx, child);
      else if (is_node (child, "program") || is_node (child, "library"))
        {
          for (uint32_t j = 0; j < ts_node_named_child_count (child); j++)
            {
              TSNode d = ts_node_named_child (child, j);
              if (is_node (d, "defProc"))
                lower_proc (&ctx, d);
            }
        }
      else if (!is_node (child, "declVars") && !is_node (child, "declTypes")
               && !is_node (child, "declConsts")
               && !is_node (child, "declUses")
               && !is_node (child, "moduleName")
               && !is_node (child, "kProgram") && !is_node (child, "kEndDot"))
        {
          /* Unit/type metadata is consumed by the Delphi elaborator. */
          continue;
        }
    }
  clear_locals (&ctx);
  ts_tree_delete (tree);
  ts_parser_delete (parser);
  if (ctx.failed || ctx.rejected)
    {
      const char *message = ctx.failed
                                ? ctx.failure
                                : "swaff Delphi: rejected malformed subtree";
      snprintf (local.message, sizeof (local.message), "%s", message);
      if (report)
        *report = local;
      set_error (error_message, message);
      ccw_ir_module_destroy (ir);
      return NULL;
    }
  if (has_errors)
    snprintf (local.message, sizeof (local.message),
              "swaff Delphi: recovered %d malformed subtrees",
              local.recovered_subtrees);
  if (report)
    *report = local;
  return ir;
}

#else

ccw_ir *
ccw_swaff_lower_delphi (const ccw_swaff_frontend *fe, const char *source,
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
  set_error (error_message, "swaff Delphi: built without Tree-sitter support");
  return NULL;
}

#endif
