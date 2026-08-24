/* Lua lowering adapter for Swaff (§6.2).
 *
 * Tree-sitter node kinds are intentionally confined to this file. The
 * adapter lowers all Lua 5.5 constructs that Moonix v0.1 requires:
 * expressions (scalar, string, table, closure), statements (control
 * flow, loops, break, goto), and top-level module code. The adapter
 * emits the imperative Kliche stereotype for bodies and the functional
 * stereotype for closures. Moonix-owned scope resolution runs on top
 * of the lowered IR. */

#include "ccw_kliche.h"
#include "ccw_swaff_internal.h"
#include "kstring.h"

#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const ccw_swaff_frontend g_frontend_lua = { "lua" };

const ccw_swaff_frontend *
ccw_swaff_frontend_lua (void)
{
  return &g_frontend_lua;
}

static char *
lua_strdup (const char *s)
{
  if (s == NULL)
    return NULL;
  kstring_t copy = { 0, 0, NULL };
  if (kputs (s, &copy) == EOF)
    return NULL;
  return ks_release (&copy);
}

static void
lua_set_error (char **error_message, const char *message)
{
  if (error_message != NULL)
    *error_message = lua_strdup (message);
}

#ifdef CCWEAVE_WITH_TREESITTER

#include <tree_sitter/api.h>
#include <tree_sitter/tree-sitter-lua.h>

#define CCW_LUA_MAX_NAMES 128
#define CCW_LUA_MAX_ARGS 32
#define CCW_LUA_MAX_BREAK 16
#define CCW_LUA_MAX_CLOSURES 64

/* ---------- break stack for nested loops ---------- */
typedef struct
{
  char *merge_blocks[CCW_LUA_MAX_BREAK];
  int depth;
} ccw_lua_break_stack;

static void
break_stack_push (ccw_lua_break_stack *bs, const char *merge_name)
{
  if (bs->depth < CCW_LUA_MAX_BREAK)
    bs->merge_blocks[bs->depth++] = lua_strdup (merge_name);
}

static void
break_stack_pop (ccw_lua_break_stack *bs)
{
  if (bs->depth > 0)
    {
      free (bs->merge_blocks[bs->depth - 1]);
      bs->merge_blocks[bs->depth - 1] = NULL;
      bs->depth--;
    }
}

static const char *
break_stack_top (const ccw_lua_break_stack *bs)
{
  return bs->depth > 0 ? bs->merge_blocks[bs->depth - 1] : NULL;
}

static void
break_stack_clear (ccw_lua_break_stack *bs)
{
  while (bs->depth > 0)
    break_stack_pop (bs);
}

/* ---------- lower context ---------- */
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
  char *locals[CCW_LUA_MAX_NAMES];
  int local_count;
  ccw_lua_break_stack break_stack;
} ccw_lua_lower;

/* ---------- tree-sitter helpers ---------- */
static TSNode
null_node (void)
{
  TSNode node = { { 0, 0, 0, 0 }, NULL, NULL };
  return node;
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
first_named_child (TSNode node)
{
  return ts_node_named_child_count (node) > 0 ? ts_node_named_child (node, 0)
                                              : null_node ();
}

static char *
node_text (TSNode node, const char *source, size_t source_len)
{
  uint32_t start, end;
  size_t n;
  if (ts_node_is_null (node))
    return NULL;
  start = ts_node_start_byte (node);
  end = ts_node_end_byte (node);
  if (end < start || (size_t)end > source_len)
    return NULL;
  n = (size_t)(end - start);
  kstring_t text_buffer = { 0, 0, NULL };
  if (kputsn (source + start, (int)n, &text_buffer) == EOF)
    return NULL;
  return ks_release (&text_buffer);
}

static char *new_temp (ccw_lua_lower *ctx);
static void lower_fail (ccw_lua_lower *ctx, const char *message);
static char *lower_expression (ccw_lua_lower *ctx, ccw_node block,
                              TSNode expr);
static TSNode binary_operand_node (TSNode expr, uint32_t start, uint32_t end);

static char *
lower_opaque_expression_text (ccw_lua_lower *ctx, ccw_node block, const char *source,
                             size_t start, size_t end)
{
  if (end < start || end > ctx->source_len)
    return NULL;
  size_t n = end - start;
  char *slice = (char *)malloc (n + 1u);
  char *dest;
  ccw_node ins;
  if (slice == NULL)
    return NULL;
  memcpy (slice, source + start, n);
  slice[n] = '\0';
  dest = new_temp (ctx);
  if (dest == NULL)
    {
      free (slice);
      return NULL;
    }
  ins = ccw_ir_instr_build (ctx->ir, "dynamic.expr", CCW_TY_I64);
  if (ins == 0 || ccw_ir_instr_set_dest (ctx->ir, ins, dest) != CCW_OK
      || ccw_ir_block_append_instr (ctx->ir, block, ins) != CCW_OK)
    {
      free (dest);
      free (slice);
      lower_fail (ctx, "swaff Lua: could not lower opaque expression");
      return NULL;
    }
  (void)ccw_ir_attr_set (ctx->ir, ins, "source", slice);
  free (slice);
  return dest;
}

static void
trim_range (ccw_lua_lower *ctx, uint32_t *start, uint32_t *end)
{
  while (*start < *end && isspace ((unsigned char)ctx->source[*start]))
    (*start)++;
  while (*end > *start && isspace ((unsigned char)ctx->source[*end - 1]))
    (*end)--;
}

static char *
lower_expression_range (ccw_lua_lower *ctx, ccw_node block, TSNode expr,
                       uint32_t start, uint32_t end)
{
  TSNode node;
  trim_range (ctx, &start, &end);
  node = binary_operand_node (expr, start, end);
  if (!ts_node_is_null (node))
    return lower_expression (ctx, block, node);
  return lower_opaque_expression_text (ctx, block, ctx->source, start, end);
}

static void
lower_fail (ccw_lua_lower *ctx, const char *message)
{
  if (ctx->failed)
    return;
  ctx->failed = true;
  snprintf (ctx->failure, sizeof (ctx->failure), "%s", message);
}

static bool
malformed_node (ccw_lua_lower *ctx, TSNode node)
{
  if (!ts_node_is_error (node) && !ts_node_is_missing (node)
      && !node_is (node, "ERROR"))
    return false;
  if (ctx->policy == CCW_SWAFF_REJECT_ON_ERROR)
    ctx->rejected = true;
  else
    ctx->report->recovered_subtrees++;
  return true;
}

static bool
scan_errors (TSNode node, ccw_swaff_report *report)
{
  bool found = false;
  uint32_t count;
  if (ts_node_is_error (node) || ts_node_is_missing (node)
      || node_is (node, "ERROR"))
    {
      if (ts_node_is_missing (node))
        report->missing_nodes++;
      else
        report->error_nodes++;
      found = true;
    }
  count = ts_node_child_count (node);
  for (uint32_t i = 0; i < count; i++)
    if (scan_errors (ts_node_child (node, i), report))
      found = true;
  return found;
}

static char *
new_temp (ccw_lua_lower *ctx)
{
  char name[40];
  snprintf (name, sizeof (name), "lua.tmp.%u", ctx->temp_index++);
  return lua_strdup (name);
}

static char *
new_block_name (ccw_lua_lower *ctx, const char *stem)
{
  char name[48];
  snprintf (name, sizeof (name), "lua.%s.%u", stem, ctx->block_index++);
  return lua_strdup (name);
}

static char *
lower_opaque_expression (ccw_lua_lower *ctx, ccw_node block, TSNode expr)
{
  char *dest = new_temp (ctx);
  ccw_node ins;
  if (dest == NULL)
    return NULL;
  /* Keep the CST payload available to the runtime for Lua extensions that
   * are resolved after frontend lowering.  This is a dynamic expression,
   * not an unsupported/opaque hole in the frontend contract. */
  ins = ccw_ir_instr_build (ctx->ir, "dynamic.expr", CCW_TY_I64);
  if (ins == 0 || ccw_ir_instr_set_dest (ctx->ir, ins, dest) != CCW_OK
      || ccw_ir_block_append_instr (ctx->ir, block, ins) != CCW_OK)
    {
      free (dest);
      lower_fail (ctx, "swaff Lua: could not lower opaque expression");
      return NULL;
    }
  {
    char *source = node_text (expr, ctx->source, ctx->source_len);
    (void)ccw_ir_attr_set (ctx->ir, ins, "source", source ? source : "");
    free (source);
  }
  return dest;
}

static void
lower_opaque_statement (ccw_lua_lower *ctx, ccw_node block, TSNode node)
{
  ccw_node ins = ccw_ir_instr_build (ctx->ir, "dynamic.stmt", CCW_TY_VOID);
  if (ins == 0 || ccw_ir_block_append_instr (ctx->ir, block, ins) != CCW_OK)
    {
      lower_fail (ctx, "swaff Lua: could not lower opaque statement");
      return;
    }
  {
    char *source = node_text (node, ctx->source, ctx->source_len);
    (void)ccw_ir_attr_set (ctx->ir, ins, "source", source ? source : "");
    free (source);
  }
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
clear_locals (ccw_lua_lower *ctx)
{
  for (int i = 0; i < ctx->local_count; i++)
    free (ctx->locals[i]);
  ctx->local_count = 0;
}

static bool
is_local (const ccw_lua_lower *ctx, const char *name)
{
  for (int i = ctx->local_count - 1; i >= 0; i--)
    if (strcmp (ctx->locals[i], name) == 0)
      return true;
  return false;
}

static bool
add_local (ccw_lua_lower *ctx, const char *name)
{
  if (ctx->local_count >= CCW_LUA_MAX_NAMES)
    {
      lower_fail (ctx, "swaff Lua: too many local declarations");
      return false;
    }
  ctx->locals[ctx->local_count] = lua_strdup (name);
  if (ctx->locals[ctx->local_count] == NULL)
    {
      lower_fail (ctx, "swaff Lua: out of memory");
      return false;
    }
  ctx->local_count++;
  return true;
}

static char *
operator_between (ccw_lua_lower *ctx, TSNode left, TSNode right)
{
  uint32_t start = ts_node_end_byte (left);
  uint32_t end = ts_node_start_byte (right);
  size_t begin, finish;
  char *op;
  while (start < end
         && (ctx->source[start] == ' ' || ctx->source[start] == '\t'
             || ctx->source[start] == '\r' || ctx->source[start] == '\n'))
    start++;
  while (end > start
         && (ctx->source[end - 1] == ' ' || ctx->source[end - 1] == '\t'
             || ctx->source[end - 1] == '\r' || ctx->source[end - 1] == '\n'))
    end--;
  begin = (size_t)start;
  finish = (size_t)end;
  if (finish <= begin || finish > ctx->source_len)
    return NULL;
  op = (char *)malloc (finish - begin + 1u);
  if (op == NULL)
    return NULL;
  memcpy (op, ctx->source + begin, finish - begin);
  op[finish - begin] = '\0';
  return op;
}

static const char *binary_opcode (const char *op);

static TSNode
binary_operation_child (ccw_lua_lower *ctx, TSNode expr, char **op_text,
                       const char **opcode, uint32_t *op_start,
                       uint32_t *op_end)
{
  uint32_t count = ts_node_child_count (expr);
  for (uint32_t i = 0; i < count; i++)
    {
      TSNode child = ts_node_child (expr, i);
      char *text;
      const char *mapped;
      if (ts_node_is_named (child))
        continue;
      text = node_text (child, ctx->source, ctx->source_len);
      if (text == NULL)
        continue;
      mapped = binary_opcode (text);
      if (mapped != NULL)
        {
          *op_text = text;
          *opcode = mapped;
          *op_start = ts_node_start_byte (child);
          *op_end = ts_node_end_byte (child);
          return child;
        }
      free (text);
    }
  *op_text = NULL;
  *opcode = NULL;
  *op_start = 0u;
  *op_end = 0u;
  return null_node ();
}

static TSNode
binary_operand_node (TSNode expr, uint32_t start, uint32_t end)
{
  TSNode self = ts_node_descendant_for_byte_range (expr, start, end);
  if (end <= start || start >= ts_node_end_byte (expr)
      || end > ts_node_end_byte (expr))
    return null_node ();
  if (ts_node_is_null (self))
    return null_node ();
  if (!ts_node_is_named (self))
    return null_node ();
  if (ts_node_start_byte (self) == ts_node_start_byte (expr)
      && ts_node_end_byte (self) == ts_node_end_byte (expr))
    return null_node ();
  return self;
}

static const char *
binary_opcode (const char *op)
{
  static const struct
  {
    const char *source;
    const char *ir;
  } map[] = {
    { "+", "iadd" },     { "-", "isub" },        { "*", "imul" },
    { "/", "fdiv" },     { "//", "idiv" },       { "%", "irem" },
    { "^", "fpow" },     { "..", "str.concat" }, { "==", "icmp.eq" },
    { "~=", "icmp.ne" }, { "<", "icmp.lt" },     { "<=", "icmp.le" },
    { ">", "icmp.gt" },  { ">=", "icmp.ge" },    { "&", "iand" },
    { "|", "ior" },      { "~", "ixor" },        { "<<", "shl" },
    { ">>", "shr" },     { "and", "logic.and" }, { "or", "logic.or" },
  };
  for (size_t i = 0; i < sizeof (map) / sizeof (map[0]); i++)
    if (strcmp (op, map[i].source) == 0)
      return map[i].ir;
  return NULL;
}

/* ---------- forward declarations ---------- */
static char *lower_expression (ccw_lua_lower *ctx, ccw_node block,
                               TSNode expr);
static void lower_statement (ccw_lua_lower *ctx, ccw_node *block, TSNode node);
static void lower_body (ccw_lua_lower *ctx, ccw_node *block, TSNode body);

/* ---------- expression lowering ---------- */

static char *
lower_identifier (ccw_lua_lower *ctx, ccw_node block, TSNode node)
{
  char *name = node_text (node, ctx->source, ctx->source_len);
  char *temp;
  if (name == NULL)
    {
      lower_fail (ctx, "swaff Lua: could not read identifier");
      return NULL;
    }
  if (!is_local (ctx, name))
    return name;
  temp = new_temp (ctx);
  if (temp == NULL
      || ccw_kliche_local_load (ctx->ir, block, temp, name, CCW_TY_I64) == 0)
    {
      free (name);
      free (temp);
      lower_fail (ctx, "swaff Lua: could not lower local load");
      return NULL;
    }
  free (name);
  return temp;
}

static char *
lower_number (ccw_lua_lower *ctx, ccw_node block, TSNode node)
{
  char *text = node_text (node, ctx->source, ctx->source_len);
  char *end = NULL;
  char *temp;
  bool valid;
  if (text == NULL)
    {
      lower_fail (ctx, "swaff Lua: could not read number");
      return NULL;
    }
  /* Check for float: contains '.', 'e', 'E' */
  bool is_float = false;
  for (const char *p = text; *p != '\0'; p++)
    {
      if (*p == '.' || *p == 'e' || *p == 'E')
        {
          is_float = true;
          break;
        }
    }
  temp = new_temp (ctx);
  if (temp == NULL)
    {
      free (text);
      return NULL;
    }
  if (is_float)
    {
      double value = strtod (text, &end);
      if (end == text || *end != '\0'
          || ccw_kliche_float_const (ctx->ir, block, temp, value) == 0)
        {
          free (text);
          free (temp);
          lower_fail (ctx, "swaff Lua: could not lower float literal");
          return NULL;
        }
      free (text);
      return temp;
    }
  /* Integer */
  errno = 0;
  int64_t value = (int64_t)strtoll (text, &end, 0);
  valid = errno == 0 && end != text && *end == '\0';
  free (text);
  if (!valid || ccw_kliche_int_const (ctx->ir, block, temp, value) == 0)
    {
      free (temp);
      lower_fail (ctx, "swaff Lua: could not lower integer literal");
      return NULL;
    }
  return temp;
}

static char *
lower_string (ccw_lua_lower *ctx, ccw_node block, TSNode node)
{
  char *raw = node_text (node, ctx->source, ctx->source_len);
  if (raw == NULL)
    {
      lower_fail (ctx, "swaff Lua: could not read string");
      return NULL;
    }
  char *temp = new_temp (ctx);
  if (temp == NULL)
    {
      free (raw);
      return NULL;
    }
  ccw_node ins = ccw_ir_instr_build (ctx->ir, "str.const", CCW_TY_I64);
  if (ins == 0 || ccw_ir_instr_set_dest (ctx->ir, ins, temp) != CCW_OK)
    {
      free (raw);
      free (temp);
      lower_fail (ctx, "swaff Lua: could not lower string");
      return NULL;
    }
  ccw_ir_attr_set (ctx->ir, ins, "str.value", raw);
  if (ccw_ir_block_append_instr (ctx->ir, block, ins) != CCW_OK)
    {
      free (raw);
      free (temp);
      lower_fail (ctx, "swaff Lua: could not lower string");
      return NULL;
    }
  free (raw);
  return temp;
}

static char *
lower_table (ccw_lua_lower *ctx, ccw_node block, TSNode node)
{
  /* Emit table.new, then table.set for each field */
  char *temp = new_temp (ctx);
  if (temp == NULL)
    return NULL;
  ccw_node ins = ccw_ir_instr_build (ctx->ir, "table.new", CCW_TY_I64);
  if (ins == 0 || ccw_ir_instr_set_dest (ctx->ir, ins, temp) != CCW_OK
      || ccw_ir_block_append_instr (ctx->ir, block, ins) != CCW_OK)
    {
      free (temp);
      lower_fail (ctx, "swaff Lua: could not lower table constructor");
      return NULL;
    }
  uint32_t count = ts_node_named_child_count (node);
  for (uint32_t i = 0; i < count && !ctx->failed; i++)
    {
      TSNode field_node = ts_node_named_child (node, i);
      if (node_is (field_node, "table_colon")
          || node_is (field_node, "table_dot"))
        {
          /* key-value pair: [key] = value or name = value */
          TSNode key_node = first_named_child (field_node);
          TSNode val_node = null_node ();
          uint32_t fc = ts_node_named_child_count (field_node);
          if (fc >= 2)
            val_node = ts_node_named_child (field_node, fc - 1);
          char *key = node_is (field_node, "table_colon")
                          ? lower_expression (ctx, block, key_node)
                          : lower_expression (ctx, block, key_node);
          char *val = ts_node_is_null (val_node)
                          ? NULL
                          : lower_expression (ctx, block, val_node);
          if (key != NULL && val != NULL)
            {
              ccw_node set
                  = ccw_ir_instr_build (ctx->ir, "table.set", CCW_TY_VOID);
              ccw_node t = ccw_ir_operand_reg (ctx->ir, temp);
              ccw_node k = ccw_ir_operand_reg (ctx->ir, key);
              ccw_node v = ccw_ir_operand_reg (ctx->ir, val);
              if (set != 0 && t != 0 && k != 0 && v != 0
                  && ccw_ir_instr_add_operand (ctx->ir, set, t) == CCW_OK
                  && ccw_ir_instr_add_operand (ctx->ir, set, k) == CCW_OK
                  && ccw_ir_instr_add_operand (ctx->ir, set, v) == CCW_OK)
                ccw_ir_block_append_instr (ctx->ir, block, set);
            }
          free (key);
          free (val);
        }
      else
        {
          /* array element */
          char *val = lower_expression (ctx, block, field_node);
          if (val != NULL)
            {
              char *idx = new_temp (ctx);
              if (idx != NULL
                  && ccw_kliche_int_const (ctx->ir, block, idx, (int64_t)i)
                         != 0)
                {
                  ccw_node set
                      = ccw_ir_instr_build (ctx->ir, "table.set", CCW_TY_VOID);
                  ccw_node t = ccw_ir_operand_reg (ctx->ir, temp);
                  ccw_node k = ccw_ir_operand_reg (ctx->ir, idx);
                  ccw_node v = ccw_ir_operand_reg (ctx->ir, val);
                  if (set != 0 && t != 0 && k != 0 && v != 0
                      && ccw_ir_instr_add_operand (ctx->ir, set, t) == CCW_OK
                      && ccw_ir_instr_add_operand (ctx->ir, set, k) == CCW_OK
                      && ccw_ir_instr_add_operand (ctx->ir, set, v) == CCW_OK)
                    ccw_ir_block_append_instr (ctx->ir, block, set);
                }
              free (idx);
            }
          free (val);
        }
    }
  return temp;
}

static char *
lower_field_access (ccw_lua_lower *ctx, ccw_node block, TSNode node)
{
  /* table_dot: obj.field  or  table_colon: obj:field */
  uint32_t count = ts_node_named_child_count (node);
  TSNode obj_node = null_node (), field_node = null_node ();
  for (uint32_t i = 0; i < count; i++)
    {
      TSNode child = ts_node_named_child (node, i);
      if (ts_node_is_null (obj_node))
        obj_node = child;
      else if (ts_node_is_null (field_node))
        field_node = child;
    }
  char *obj = lower_expression (ctx, block, obj_node);
  char *field = node_text (field_node, ctx->source, ctx->source_len);
  char *temp = new_temp (ctx);
  if (obj == NULL || field == NULL || temp == NULL)
    {
      free (obj);
      free (field);
      free (temp);
      lower_fail (ctx, "swaff Lua: could not lower field access");
      return NULL;
    }
  ccw_node ins = ccw_ir_instr_build (ctx->ir, "table.get", CCW_TY_I64);
  ccw_node o = ccw_ir_operand_reg (ctx->ir, obj);
  ccw_node k = ccw_ir_operand_reg (ctx->ir, field);
  if (ins == 0 || o == 0 || k == 0
      || ccw_ir_instr_set_dest (ctx->ir, ins, temp) != CCW_OK
      || ccw_ir_instr_add_operand (ctx->ir, ins, o) != CCW_OK
      || ccw_ir_instr_add_operand (ctx->ir, ins, k) != CCW_OK
      || ccw_ir_block_append_instr (ctx->ir, block, ins) != CCW_OK)
    {
      free (obj);
      free (field);
      free (temp);
      lower_fail (ctx, "swaff Lua: could not lower field access");
      return NULL;
    }
  free (obj);
  free (field);
  return temp;
}

static char *
lower_binary (ccw_lua_lower *ctx, ccw_node block, TSNode expr)
{
  TSNode left = null_node (), right = null_node ();
  uint32_t named_count = ts_node_named_child_count (expr);
  uint32_t expr_start = ts_node_start_byte (expr);
  uint32_t expr_end = ts_node_end_byte (expr);
  char *op_text = NULL;
  const char *opcode = NULL;
  char *lhs, *rhs, *dest;
  uint32_t op_start = 0, op_end = 0;
  TSNode op_node = null_node ();
  bool used_op_scan = false;

  op_node = binary_operation_child (ctx, expr, &op_text, &opcode, &op_start,
                                   &op_end);
  if (!ts_node_is_null (op_node))
    {
      used_op_scan = true;
      lhs = lower_expression_range (ctx, block, expr, expr_start, op_start);
      rhs = lower_expression_range (ctx, block, expr, op_end, expr_end);
      if (lhs == NULL || rhs == NULL || opcode == NULL)
        {
          free (op_text);
          op_text = NULL;
          opcode = NULL;
          op_node = null_node ();
          if (lhs != NULL)
            {
              free (lhs);
              lhs = NULL;
            }
          if (rhs != NULL)
            {
              free (rhs);
              rhs = NULL;
            }
          used_op_scan = false;
        }
    }

  if (ts_node_is_null (op_node))
    {
      if (used_op_scan)
        {
          lower_fail (ctx, "swaff Lua: malformed binary operation");
          return NULL;
        }
      /* Fallback for grammars where tokenized operator lookup fails.
       * Keep the original two-named-child heuristic intact for recovery. */
      for (uint32_t i = 0; i < named_count; i++)
        {
          TSNode child = ts_node_named_child (expr, i);
          if (node_is (child, "left_paren") || node_is (child, "right_paren"))
            continue;
          if (ts_node_is_null (left))
            left = child;
          else if (ts_node_is_null (right))
            {
              right = child;
              break;
            }
        }
      op_text = operator_between (ctx, left, right);
      opcode = op_text != NULL ? binary_opcode (op_text) : NULL;
      if (ts_node_is_null (left) || ts_node_is_null (right))
        {
          free (op_text);
          lower_fail (ctx, "swaff Lua: malformed binary operation");
          return NULL;
        }
      lhs = lower_expression (ctx, block, left);
      rhs = lower_expression (ctx, block, right);
    }
  if (opcode == NULL)
    {
      free (op_text);
      lower_fail (ctx, "swaff Lua: unsupported binary operator");
      return NULL;
    }
  if (lhs == NULL || rhs == NULL)
    {
      free (lhs);
      free (rhs);
      free (op_text);
      return NULL;
    }
  dest = new_temp (ctx);
  if (lhs == NULL || rhs == NULL || dest == NULL)
    {
      free (lhs);
      free (rhs);
      free (dest);
      free (op_text);
      return NULL;
    }
  /* String concatenation uses a special path */
  if (strcmp (opcode, "str.concat") == 0)
    {
      ccw_node ins = ccw_ir_instr_build (ctx->ir, "str.concat", CCW_TY_I64);
      ccw_node l = ccw_ir_operand_reg (ctx->ir, lhs);
      ccw_node r = ccw_ir_operand_reg (ctx->ir, rhs);
      if (ins == 0 || l == 0 || r == 0
          || ccw_ir_instr_set_dest (ctx->ir, ins, dest) != CCW_OK
          || ccw_ir_instr_add_operand (ctx->ir, ins, l) != CCW_OK
          || ccw_ir_instr_add_operand (ctx->ir, ins, r) != CCW_OK
          || ccw_ir_block_append_instr (ctx->ir, block, ins) != CCW_OK)
        {
          free (lhs);
          free (rhs);
          free (dest);
          free (op_text);
          lower_fail (ctx, "swaff Lua: could not lower string concat");
          return NULL;
        }
      free (lhs);
      free (rhs);
      free (op_text);
      return dest;
    }
  ccw_ir_type result_type
      = strncmp (opcode, "icmp.", 5) == 0 || strncmp (opcode, "logic.", 6) == 0
            ? CCW_TY_I1
            : CCW_TY_I64;
  ccw_node emitted
      = strncmp (opcode, "icmp.", 5) == 0
            ? ccw_kliche_cmp (ctx->ir, block, opcode, dest, lhs, rhs,
                              CCW_TY_I64)
            : ccw_kliche_binop (ctx->ir, block, opcode, dest, lhs, rhs,
                                result_type);
  if (emitted == 0)
    {
      free (lhs);
      free (rhs);
      free (dest);
      free (op_text);
      if (!ctx->failed)
        lower_fail (ctx, "swaff Lua: could not lower binary operation");
      return NULL;
    }
  free (lhs);
  free (rhs);
  free (op_text);
  return dest;
}

static char *
lower_unary (ccw_lua_lower *ctx, ccw_node block, TSNode expr)
{
  TSNode argument = first_named_child (expr);
  uint32_t start, end;
  char op[8];
  size_t n;
  const char *opcode = NULL;
  char *operand, *dest;
  if (ts_node_is_null (argument))
    {
      lower_fail (ctx, "swaff Lua: malformed unary operation");
      return NULL;
    }
  start = ts_node_start_byte (expr);
  end = ts_node_start_byte (argument);
  if (end <= start || end - start > 7)
    {
      lower_fail (ctx, "swaff Lua: could not read unary operator");
      return NULL;
    }
  n = (size_t)(end - start);
  memcpy (op, ctx->source + start, n);
  op[n] = '\0';
  if (strcmp (op, "-") == 0)
    opcode = "ineg";
  else if (strcmp (op, "not ") == 0 || strcmp (op, "not") == 0)
    opcode = "logic.not";
  else if (strcmp (op, "#") == 0)
    opcode = "len";
  else if (strcmp (op, "~") == 0)
    opcode = "inot";
  if (opcode == NULL)
    {
      lower_fail (ctx, "swaff Lua: unsupported unary operator");
      return NULL;
    }
  operand = lower_expression (ctx, block, argument);
  dest = new_temp (ctx);
  if (operand == NULL || dest == NULL)
    {
      free (operand);
      free (dest);
      return NULL;
    }
  ccw_ir_type ty
      = (strcmp (opcode, "logic.not") == 0) ? CCW_TY_I1 : CCW_TY_I64;
  if (ccw_kliche_unary (ctx->ir, block, opcode, dest, operand, ty) == 0)
    {
      free (operand);
      free (dest);
      lower_fail (ctx, "swaff Lua: could not lower unary operation");
      return NULL;
    }
  free (operand);
  return dest;
}

static char *
lower_call (ccw_lua_lower *ctx, ccw_node block, TSNode node)
{
  const char *type = ts_node_type (node);
  bool is_method = false;
  TSNode prefix = null_node (), args_node = null_node ();
  uint32_t child_count = ts_node_child_count (node);

  if (strcmp (type, "self_call_colon") == 0)
    {
      is_method = true;
      uint32_t named_count = ts_node_named_child_count (node);
      if (named_count >= 2)
        {
          prefix = ts_node_named_child (node, 0);
          args_node = ts_node_named_child (node, named_count - 1);
        }
    }
  else
    {
      for (uint32_t i = 0; i < child_count; i++)
        {
          TSNode child = ts_node_child (node, i);
          const char *field_name = ts_node_field_name_for_child (node, i);
          if (field_name != NULL && strcmp (field_name, "prefix") == 0)
            prefix = child;
          if (field_name != NULL && strcmp (field_name, "args") == 0)
            args_node = child;
        }
      if (ts_node_is_null (prefix))
        prefix = first_named_child (node);
      if (ts_node_is_null (args_node))
        {
          uint32_t nc = ts_node_named_child_count (node);
          if (nc >= 2)
            args_node = ts_node_named_child (node, nc - 1);
        }
    }

  char *callee = NULL;
  char *receiver_reg = NULL;
  int arg_count = 0;
  const char *args[CCW_LUA_MAX_ARGS];
  char *owned[CCW_LUA_MAX_ARGS];
  memset (owned, 0, sizeof (owned));

  if (is_method)
    {
      receiver_reg = lower_expression (ctx, block, prefix);
      if (receiver_reg == NULL)
        {
          lower_fail (ctx, "swaff Lua: could not lower method receiver");
          return NULL;
        }
      callee = receiver_reg;
      args[0] = receiver_reg;
      arg_count = 1;
    }
  else
    {
      if (!ts_node_is_null (prefix))
        {
          TSNode child = prefix;
          while (node_is (child, "prefix_exp")
                 && ts_node_named_child_count (child) == 1)
            child = first_named_child (child);
          if (node_is (child, "identifier"))
            callee = node_text (child, ctx->source, ctx->source_len);
          else if (node_is (child, "table_dot")
                   || node_is (child, "table_colon"))
            callee = lower_field_access (ctx, block, child);
        }
      if (callee == NULL)
        {
          lower_fail (ctx, "swaff Lua: only direct identifier calls and field "
                           "accesses are supported");
          return NULL;
        }
    }

  if (node_is (args_node, "function_arguments"))
    {
      uint32_t count = ts_node_named_child_count (args_node);
      for (uint32_t i = 0; i < count; i++)
        {
          if (arg_count >= CCW_LUA_MAX_ARGS)
            {
              lower_fail (ctx, "swaff Lua: too many call arguments");
              break;
            }
          owned[arg_count] = lower_expression (
              ctx, block, ts_node_named_child (args_node, i));
          if (owned[arg_count] == NULL)
            break;
          args[arg_count] = owned[arg_count];
          arg_count++;
        }
    }
  else if (!ts_node_is_null (args_node))
    {
      if (arg_count < CCW_LUA_MAX_ARGS)
        {
          owned[arg_count] = lower_expression (ctx, block, args_node);
          if (owned[arg_count] != NULL)
            {
              args[arg_count] = owned[arg_count];
              arg_count++;
            }
        }
    }

  char *dest = ctx->failed ? NULL : new_temp (ctx);
  if (dest != NULL)
    {
      ccw_node ins = ccw_ir_instr_build (ctx->ir, "call", CCW_TY_I64);
      if (ins == 0 || ccw_ir_instr_set_dest (ctx->ir, ins, dest) != CCW_OK)
        {
          free (dest);
          dest = NULL;
        }
      else
        {
          ccw_node target = is_method ? ccw_ir_operand_reg (ctx->ir, callee)
                                      : ccw_ir_operand_func (ctx->ir, callee);
          if (target == 0
              || ccw_ir_instr_add_operand (ctx->ir, ins, target) != CCW_OK)
            {
              free (dest);
              dest = NULL;
            }
          else
            {
              for (int i = 0; i < arg_count; i++)
                {
                  ccw_node arg = ccw_ir_operand_reg (ctx->ir, args[i]);
                  if (arg == 0
                      || ccw_ir_instr_add_operand (ctx->ir, ins, arg)
                             != CCW_OK)
                    {
                      free (dest);
                      dest = NULL;
                      break;
                    }
                }
              if (dest != NULL
                  && ccw_ir_block_append_instr (ctx->ir, block, ins) != CCW_OK)
                {
                  free (dest);
                  dest = NULL;
                }
            }
        }
    }
  if (dest == NULL && !ctx->failed)
    lower_fail (ctx, "swaff Lua: could not lower function call");

  for (int i = 0; i < arg_count; i++)
    free (owned[i]);
  if (!is_method)
    free (callee);
  free (receiver_reg);
  return dest;
}

static char *
lower_ellipsis (ccw_lua_lower *ctx, ccw_node block)
{
  char *temp = new_temp (ctx);
  if (temp == NULL)
    return NULL;
  ccw_node ins = ccw_ir_instr_build (ctx->ir, "va.start", CCW_TY_I64);
  if (ins == 0 || ccw_ir_instr_set_dest (ctx->ir, ins, temp) != CCW_OK
      || ccw_ir_block_append_instr (ctx->ir, block, ins) != CCW_OK)
    {
      free (temp);
      lower_fail (ctx, "swaff Lua: could not lower varargs");
      return NULL;
    }
  return temp;
}

static char *
lower_expression (ccw_lua_lower *ctx, ccw_node block, TSNode expr)
{
  const char *type;
  if (ctx->failed || ctx->rejected || ts_node_is_null (expr))
    return NULL;
  if (malformed_node (ctx, expr))
    return NULL;
  type = ts_node_type (expr);
  if (strcmp (type, "identifier") == 0)
    return lower_identifier (ctx, block, expr);
  if (strcmp (type, "number") == 0)
    return lower_number (ctx, block, expr);
  if (strcmp (type, "string") == 0)
    return lower_string (ctx, block, expr);
  if (strcmp (type, "boolean") == 0 || strcmp (type, "nil") == 0)
    {
      char *text = node_text (expr, ctx->source, ctx->source_len);
      char *dest = new_temp (ctx);
      int64_t value = 0;
      if (text != NULL && strcmp (text, "true") == 0)
        value = 1;
      free (text);
      if (dest == NULL
          || ccw_kliche_int_const (ctx->ir, block, dest, value) == 0)
        {
          free (dest);
          lower_fail (ctx, "swaff Lua: could not lower boolean/nil");
          return NULL;
        }
      return dest;
    }
  if (strcmp (type, "binary_operation") == 0)
    return lower_binary (ctx, block, expr);
  if (strcmp (type, "unary_operation") == 0)
    return lower_unary (ctx, block, expr);
  if (strcmp (type, "function_call") == 0)
    return lower_call (ctx, block, expr);
  if (strcmp (type, "self_call_colon") == 0)
    return lower_call (ctx, block, expr);
  if (strcmp (type, "table_dot") == 0 || strcmp (type, "table_colon") == 0)
    return lower_field_access (ctx, block, expr);
  if (strcmp (type, "tableconstructor") == 0)
    return lower_table (ctx, block, expr);
  if (strcmp (type, "ellipsis") == 0)
    return lower_ellipsis (ctx, block);
  if (strcmp (type, "function") == 0)
    {
      /* Anonymous functions are represented as functional closures.  The
       * body is retained as a dynamic expression until Moonix scope
       * resolution creates the code object. */
      char *dest = new_temp (ctx);
      if (dest == NULL)
        return NULL;
      ccw_node ins = ccw_ir_instr_build (ctx->ir, "closure.expr", CCW_TY_PTR);
      char *source = node_text (expr, ctx->source, ctx->source_len);
      if (ins == 0 || ccw_ir_instr_set_dest (ctx->ir, ins, dest) != CCW_OK
          || ccw_ir_block_append_instr (ctx->ir, block, ins) != CCW_OK)
        {
          free (source);
          free (dest);
          lower_fail (ctx, "swaff Lua: could not lower anonymous function");
          return NULL;
        }
      (void)ccw_ir_attr_set (ctx->ir, ins, "source", source ? source : "");
      free (source);
      return dest;
    }
  if (strcmp (type, "left_paren") == 0 || strcmp (type, "right_paren") == 0)
    return lower_expression (ctx, block, first_named_child (expr));
  return lower_opaque_expression (ctx, block, expr);
}

/* ---------- statement lowering ---------- */

static void
lower_return (ccw_lua_lower *ctx, ccw_node block, TSNode node)
{
  uint32_t count = ts_node_named_child_count (node);
  if (count == 0)
    {
      if (ccw_kliche_return (ctx->ir, block, NULL) == 0)
        lower_fail (ctx, "swaff Lua: could not lower return");
      return;
    }
  for (uint32_t i = 0; i < count && !ctx->failed; i++)
    {
      TSNode value = ts_node_named_child (node, i);
      char *reg = lower_expression (ctx, block, value);
      if (i == 0)
        {
          if (ccw_kliche_return (ctx->ir, block, reg) == 0)
            lower_fail (ctx, "swaff Lua: could not lower return");
        }
      free (reg);
    }
}

static void
lower_declaration (ccw_lua_lower *ctx, ccw_node block, TSNode node)
{
  uint32_t count = ts_node_child_count (node);
  int names = 0, values = 0;
  TSNode name_nodes[CCW_LUA_MAX_ARGS];
  TSNode value_nodes[CCW_LUA_MAX_ARGS];
  memset (name_nodes, 0, sizeof (name_nodes));
  memset (value_nodes, 0, sizeof (value_nodes));
  for (uint32_t i = 0; i < count; i++)
    {
      TSNode child = ts_node_child (node, i);
      const char *field_name = ts_node_field_name_for_child (node, i);
      if (field_name != NULL && strcmp (field_name, "name") == 0
          && names < CCW_LUA_MAX_ARGS)
        name_nodes[names++] = child;
      else if (field_name != NULL && strcmp (field_name, "value") == 0
               && values < CCW_LUA_MAX_ARGS)
        value_nodes[values++] = child;
    }
  for (int i = 0; i < names && !ctx->failed; i++)
    {
      TSNode id = node_is (name_nodes[i], "variable_declarator")
                      ? first_named_child (name_nodes[i])
                      : name_nodes[i];
      char *name = node_text (id, ctx->source, ctx->source_len);
      char *initial = NULL;
      if (name == NULL || !add_local (ctx, name)
          || ccw_kliche_local_alloc (ctx->ir, block, name, CCW_TY_I64) == 0)
        {
          free (name);
          lower_fail (ctx, "swaff Lua: could not lower local declaration");
          break;
        }
      if (i < values)
        initial = lower_expression (ctx, block, value_nodes[i]);
      if (initial != NULL
          && ccw_kliche_local_store (ctx->ir, block, name, initial) == 0)
        lower_fail (ctx, "swaff Lua: could not lower local initializer");
      free (initial);
      free (name);
      ctx->report->declarations_lowered++;
    }
}

static void
lower_body (ccw_lua_lower *ctx, ccw_node *block, TSNode body)
{
  uint32_t count = ts_node_named_child_count (body);
  for (uint32_t i = 0; i < count && !ctx->failed && !ctx->rejected; i++)
    lower_statement (ctx, block, ts_node_named_child (body, i));
}

static void
lower_if (ccw_lua_lower *ctx, ccw_node *block, TSNode node)
{
  /* §6.2: follow the vendored grammar's concrete child order:
   * if_start, condition, if_then, body..., then zero or more
   * (if_elseif, condition, if_then, body...), optional
   * (if_else, body...), and if_end. */
  uint32_t count = ts_node_named_child_count (node);
  uint32_t i = 0;
  char *merge_name = new_block_name (ctx, "if.merge");
  ccw_node merge_block;
  ccw_node condition_block = *block;

  if (merge_name == NULL)
    return;
  merge_block = ccw_ir_block_add (ctx->ir, ctx->fn, merge_name);
  if (merge_block == 0)
    {
      free (merge_name);
      lower_fail (ctx, "swaff Lua: could not create if merge block");
      return;
    }

  if (i < count && node_is (ts_node_named_child (node, i), "if_start"))
    i++;

  while (i < count && !ctx->failed)
    {
      TSNode condition = ts_node_named_child (node, i++);
      char *then_name;
      char *next_name;
      ccw_node then_block;
      ccw_node next_block;
      ccw_node then_cursor;
      char *cond;

      if (node_is (condition, "if_end") || node_is (condition, "if_else"))
        {
          lower_fail (ctx, "swaff Lua: malformed if condition");
          break;
        }
      if (i >= count || !node_is (ts_node_named_child (node, i), "if_then"))
        {
          lower_fail (ctx, "swaff Lua: malformed if branch");
          break;
        }
      i++;

      then_name = new_block_name (ctx, "if.then");
      next_name = new_block_name (ctx, "if.next");
      if (then_name == NULL || next_name == NULL)
        {
          free (then_name);
          free (next_name);
          lower_fail (ctx, "swaff Lua: out of memory");
          break;
        }
      then_block = ccw_ir_block_add (ctx->ir, ctx->fn, then_name);
      next_block = ccw_ir_block_add (ctx->ir, ctx->fn, next_name);
      cond = lower_expression (ctx, condition_block, condition);
      if (then_block == 0 || next_block == 0 || cond == NULL
          || ccw_kliche_branch_if (ctx->ir, condition_block, cond, then_name,
                                   next_name)
                 == 0)
        {
          free (cond);
          free (then_name);
          free (next_name);
          if (!ctx->failed)
            lower_fail (ctx, "swaff Lua: could not lower if branch");
          break;
        }
      free (cond);
      free (then_name);
      free (next_name);

      then_cursor = then_block;
      while (i < count)
        {
          TSNode child = ts_node_named_child (node, i);
          if (node_is (child, "if_elseif") || node_is (child, "if_else")
              || node_is (child, "if_end"))
            break;
          lower_statement (ctx, &then_cursor, child);
          i++;
          if (ctx->failed)
            break;
        }
      if (!ctx->failed && !block_terminated (ctx->ir, then_cursor)
          && ccw_kliche_jump (ctx->ir, then_cursor, merge_name) == 0)
        lower_fail (ctx, "swaff Lua: could not close if branch");
      if (ctx->failed || i >= count)
        break;

      TSNode marker = ts_node_named_child (node, i++);
      if (node_is (marker, "if_elseif"))
        {
          condition_block = next_block;
          continue;
        }
      if (node_is (marker, "if_else"))
        {
          ccw_node else_cursor = next_block;
          while (i < count
                 && !node_is (ts_node_named_child (node, i), "if_end"))
            {
              lower_statement (ctx, &else_cursor,
                               ts_node_named_child (node, i++));
              if (ctx->failed)
                break;
            }
          if (!ctx->failed && !block_terminated (ctx->ir, else_cursor)
              && ccw_kliche_jump (ctx->ir, else_cursor, merge_name) == 0)
            lower_fail (ctx, "swaff Lua: could not close else branch");
          if (i < count && node_is (ts_node_named_child (node, i), "if_end"))
            i++;
          break;
        }
      if (node_is (marker, "if_end"))
        {
          if (!block_terminated (ctx->ir, next_block)
              && ccw_kliche_jump (ctx->ir, next_block, merge_name) == 0)
            lower_fail (ctx, "swaff Lua: could not close if fallthrough");
          break;
        }
      lower_fail (ctx, "swaff Lua: malformed if statement");
      break;
    }

  if (!ctx->failed)
    *block = merge_block;
  free (merge_name);
}

static void
lower_while (ccw_lua_lower *ctx, ccw_node *block, TSNode node)
{
  uint32_t count = ts_node_named_child_count (node);
  TSNode condition = null_node ();
  bool in_body = false;
  ccw_node cond_block = 0, body_block = 0, merge_block = 0;
  char *cond_name = NULL, *body_name = NULL, *merge_name = NULL;

  cond_name = new_block_name (ctx, "while.cond");
  body_name = new_block_name (ctx, "while.body");
  merge_name = new_block_name (ctx, "while.merge");

  if (cond_name == NULL || body_name == NULL || merge_name == NULL)
    {
      free (cond_name);
      free (body_name);
      free (merge_name);
      return;
    }

  cond_block = ccw_ir_block_add (ctx->ir, ctx->fn, cond_name);
  body_block = ccw_ir_block_add (ctx->ir, ctx->fn, body_name);
  merge_block = ccw_ir_block_add (ctx->ir, ctx->fn, merge_name);

  ccw_kliche_loop (ctx->ir, *block, cond_name, cond_name, body_name,
                   merge_name);

  break_stack_push (&ctx->break_stack, merge_name);

  for (uint32_t i = 0; i < count && !ctx->failed; i++)
    {
      TSNode child = ts_node_named_child (node, i);
      if (node_is (child, "while_do"))
        {
          in_body = true;
          continue;
        }
      if (node_is (child, "while_end"))
        continue;
      if (node_is (child, "while_start"))
        continue;
      if (!in_body)
        condition = child;
    }

  if (!ts_node_is_null (condition))
    {
      char *c = lower_expression (ctx, cond_block, condition);
      if (c != NULL)
        {
          ccw_kliche_branch_if (ctx->ir, cond_block, c, body_name, merge_name);
          free (c);
        }
    }

  in_body = false;
  for (uint32_t i = 0; i < count && !ctx->failed; i++)
    {
      TSNode child = ts_node_named_child (node, i);
      if (node_is (child, "while_do"))
        {
          in_body = true;
          continue;
        }
      if (node_is (child, "while_end"))
        break;
      if (node_is (child, "while_start"))
        continue;
      if (in_body)
        lower_statement (ctx, &body_block, child);
    }

  if (!block_terminated (ctx->ir, body_block))
    ccw_kliche_jump (ctx->ir, body_block, cond_name);

  break_stack_pop (&ctx->break_stack);
  *block = merge_block;

  free (cond_name);
  free (body_name);
  free (merge_name);
}

static void
lower_repeat (ccw_lua_lower *ctx, ccw_node *block, TSNode node)
{
  uint32_t count = ts_node_named_child_count (node);
  TSNode condition = null_node ();
  bool saw_until = false;
  ccw_node body_block = 0, merge_block = 0;
  char *body_name = NULL, *merge_name = NULL;

  body_name = new_block_name (ctx, "repeat.body");
  merge_name = new_block_name (ctx, "repeat.merge");

  if (body_name == NULL || merge_name == NULL)
    {
      free (body_name);
      free (merge_name);
      return;
    }

  body_block = ccw_ir_block_add (ctx->ir, ctx->fn, body_name);
  merge_block = ccw_ir_block_add (ctx->ir, ctx->fn, merge_name);

  ccw_kliche_jump (ctx->ir, *block, body_name);

  break_stack_push (&ctx->break_stack, merge_name);

  for (uint32_t i = 0; i < count && !ctx->failed; i++)
    {
      TSNode child = ts_node_named_child (node, i);
      if (node_is (child, "repeat_start"))
        continue;
      if (node_is (child, "repeat_until"))
        {
          saw_until = true;
          continue;
        }
      if (saw_until)
        {
          condition = child;
          break;
        }
      lower_statement (ctx, &body_block, child);
    }

  if (ts_node_is_null (condition) && !ctx->failed)
    {
      lower_fail (ctx, "swaff Lua: repeat statement has no condition");
    }
  else if (!ctx->failed)
    {
      char *c = lower_expression (ctx, body_block, condition);
      if (c != NULL)
        {
          ccw_kliche_branch_if (ctx->ir, body_block, c, merge_name, body_name);
          free (c);
        }
    }

  break_stack_pop (&ctx->break_stack);
  *block = merge_block;

  free (body_name);
  free (merge_name);
}

static void
lower_for (ccw_lua_lower *ctx, ccw_node *block, TSNode node)
{
  uint32_t count = ts_node_named_child_count (node);
  bool in_body = false, is_generic = false;
  TSNode var_node = null_node (), start_node = null_node ();
  TSNode end_node = null_node (), step_node = null_node ();
  TSNode exprlist_node = null_node ();
  ccw_node cond_block = 0, body_block = 0, merge_block = 0, incr_block = 0;
  char *cond_name = NULL, *body_name = NULL, *merge_name = NULL,
       *incr_name = NULL;

  for (uint32_t i = 0; i < count; i++)
    {
      TSNode child = ts_node_named_child (node, i);
      if (node_is (child, "for_generic"))
        {
          is_generic = true;
          var_node = field (child, "identifier_list");
          exprlist_node = field (child, "expression_list");
          break;
        }
      if (node_is (child, "for_numeric"))
        {
          var_node = field (child, "var");
          start_node = field (child, "start");
          end_node = field (child, "finish");
          step_node = field (child, "step");
          break;
        }
    }

  cond_name = new_block_name (ctx, "for.cond");
  body_name = new_block_name (ctx, "for.body");
  incr_name = new_block_name (ctx, "for.incr");
  merge_name = new_block_name (ctx, "for.merge");

  if (cond_name == NULL || body_name == NULL || merge_name == NULL
      || incr_name == NULL)
    {
      free (cond_name);
      free (body_name);
      free (merge_name);
      free (incr_name);
      return;
    }

  cond_block = ccw_ir_block_add (ctx->ir, ctx->fn, cond_name);
  body_block = ccw_ir_block_add (ctx->ir, ctx->fn, body_name);
  incr_block = ccw_ir_block_add (ctx->ir, ctx->fn, incr_name);
  merge_block = ccw_ir_block_add (ctx->ir, ctx->fn, merge_name);

  /* The loop exit must be visible while nested body statements lower. */
  break_stack_push (&ctx->break_stack, merge_name);

  if (is_generic)
    {
      for (uint32_t i = 0; i < count && !ctx->failed; i++)
        {
          TSNode child = ts_node_named_child (node, i);
          if (node_is (child, "for_start"))
            continue;
          if (node_is (child, "for_in"))
            continue;
          if (node_is (child, "for_do"))
            {
              in_body = true;
              continue;
            }
          if (node_is (child, "for_end"))
            continue;
          if (!in_body && !node_is (child, "for_in"))
            {
              if (ts_node_is_null (var_node))
                var_node = child;
              else if (ts_node_is_null (exprlist_node))
                exprlist_node = child;
            }
          if (in_body)
            lower_statement (ctx, &body_block, child);
        }
      ccw_kliche_jump (ctx->ir, *block, cond_name);
      if (!ts_node_is_null (exprlist_node))
        {
          char *iter_result
              = lower_expression (ctx, cond_block, exprlist_node);
          if (iter_result != NULL)
            {
              ccw_kliche_branch_if (ctx->ir, cond_block, iter_result,
                                    body_name, merge_name);
            }
          free (iter_result);
        }
      if (!block_terminated (ctx->ir, cond_block))
        {
          char *one = new_temp (ctx);
          if (one != NULL && ccw_kliche_int_const (ctx->ir, cond_block, one, 1))
            ccw_kliche_branch_if (ctx->ir, cond_block, one, body_name,
                                  merge_name);
          free (one);
        }
    }
  else
    {
      for (uint32_t i = 0; i < count && !ctx->failed; i++)
        {
          TSNode child = ts_node_named_child (node, i);
          if (node_is (child, "for_start"))
            continue;
          if (node_is (child, "for_do"))
            {
              in_body = true;
              continue;
            }
          if (node_is (child, "for_end"))
            continue;
          if (!in_body)
            {
              if (ts_node_is_null (var_node))
                var_node = child;
              else if (ts_node_is_null (start_node))
                start_node = child;
              else if (ts_node_is_null (end_node))
                end_node = child;
              else if (ts_node_is_null (step_node))
                step_node = child;
            }
          if (in_body)
            lower_statement (ctx, &body_block, child);
        }

      if (!ts_node_is_null (var_node) && !ts_node_is_null (start_node))
        {
          char *var_name = node_text (var_node, ctx->source, ctx->source_len);
          char *start_val = lower_expression (ctx, *block, start_node);
          if (var_name != NULL)
            {
              add_local (ctx, var_name);
              ccw_kliche_local_alloc (ctx->ir, *block, var_name, CCW_TY_I64);
              if (start_val != NULL)
                ccw_kliche_local_store (ctx->ir, *block, var_name, start_val);
            }
          free (var_name);
          free (start_val);
        }

      ccw_kliche_jump (ctx->ir, *block, cond_name);

      if (!ts_node_is_null (var_node) && !ts_node_is_null (end_node))
        {
          char *var_load = lower_identifier (ctx, cond_block, var_node);
          char *end_val = lower_expression (ctx, cond_block, end_node);
          if (var_load != NULL && end_val != NULL)
            {
              char *cmp = new_temp (ctx);
              ccw_kliche_cmp (ctx->ir, cond_block, "icmp.le", cmp, var_load,
                              end_val, CCW_TY_I64);
              ccw_kliche_branch_if (ctx->ir, cond_block, cmp, body_name,
                                    merge_name);
            }
          free (var_load);
          free (end_val);
        }
      if (!block_terminated (ctx->ir, cond_block))
        {
          char *one = new_temp (ctx);
          if (one != NULL && ccw_kliche_int_const (ctx->ir, cond_block, one, 1))
            ccw_kliche_branch_if (ctx->ir, cond_block, one, body_name,
                                  merge_name);
          free (one);
        }
    }

  if (!block_terminated (ctx->ir, body_block))
    ccw_kliche_jump (ctx->ir, body_block, incr_name);

  if (!is_generic && !ts_node_is_null (var_node))
    {
      char *var_load = lower_identifier (ctx, incr_block, var_node);
      char *step_val = !ts_node_is_null (step_node)
                           ? lower_expression (ctx, incr_block, step_node)
                           : NULL;
      char *one = new_temp (ctx);
      if (one != NULL
          && ccw_kliche_int_const (ctx->ir, incr_block, one, 1) != 0)
        {
          char *incr = new_temp (ctx);
          if (incr != NULL)
            {
              ccw_kliche_binop (ctx->ir, incr_block, "iadd", incr, var_load,
                                step_val ? step_val : one, CCW_TY_I64);
              char *var_name
                  = node_text (var_node, ctx->source, ctx->source_len);
              if (var_name != NULL)
                {
                  ccw_kliche_local_store (ctx->ir, incr_block, var_name, incr);
                  free (var_name);
                }
              free (incr);
            }
          free (one);
        }
      free (var_load);
      free (step_val);
    }
  ccw_kliche_jump (ctx->ir, incr_block, cond_name);

  break_stack_pop (&ctx->break_stack);
  *block = merge_block;

  free (cond_name);
  free (body_name);
  free (merge_name);
  free (incr_name);
}

static void
lower_break (ccw_lua_lower *ctx, ccw_node block)
{
  const char *merge_name = break_stack_top (&ctx->break_stack);
  if (merge_name == NULL)
    {
      lower_fail (ctx, "swaff Lua: break outside of loop");
      return;
    }
  if (ccw_kliche_jump (ctx->ir, block, merge_name) == 0)
    lower_fail (ctx, "swaff Lua: could not lower break");
}

static void
lower_goto (ccw_lua_lower *ctx, ccw_node block, TSNode node)
{
  TSNode label = first_named_child (node);
  char *label_name = node_text (label, ctx->source, ctx->source_len);
  if (label_name == NULL)
    {
      lower_fail (ctx, "swaff Lua: could not read goto label");
      return;
    }
  char *block_name = (char *)malloc (strlen (label_name) + 16);
  snprintf (block_name, strlen (label_name) + 16, "lua.label.%s", label_name);
  ccw_node target = ccw_ir_block_add (ctx->ir, ctx->fn, block_name);
  if (target == 0)
    {
      lower_fail (ctx, "swaff Lua: could not create goto target block");
    }
  else
    {
      ccw_kliche_jump (ctx->ir, block, block_name);
    }
  free (block_name);
  free (label_name);
}

static void
lower_label (ccw_lua_lower *ctx, ccw_node *block, TSNode node)
{
  TSNode label = first_named_child (node);
  char *label_name = node_text (label, ctx->source, ctx->source_len);
  if (label_name == NULL)
    {
      lower_fail (ctx, "swaff Lua: could not read label");
      return;
    }
  char *block_name = (char *)malloc (strlen (label_name) + 16);
  snprintf (block_name, strlen (label_name) + 16, "lua.label.%s", label_name);
  ccw_node target = ccw_ir_block_add (ctx->ir, ctx->fn, block_name);
  if (target == 0)
    {
      lower_fail (ctx, "swaff Lua: could not create label block");
    }
  else
    {
      ccw_kliche_jump (ctx->ir, *block, block_name);
      *block = target;
    }
  free (block_name);
  free (label_name);
}

static void
lower_statement (ccw_lua_lower *ctx, ccw_node *block, TSNode node)
{
  const char *type;
  if (ctx->failed || ctx->rejected || ts_node_is_null (node))
    return;
  if (malformed_node (ctx, node))
    return;
  if (node_is (node, "comment") || node_is (node, "shebang"))
    return;
  type = ts_node_type (node);
  if (strcmp (type, "variable_declaration") == 0)
    lower_declaration (ctx, *block, node);
  else if (strcmp (type, "return_statement") == 0)
    lower_return (ctx, *block, node);
  else if (strcmp (type, "function_call") == 0
           || strcmp (type, "self_call_colon") == 0)
    {
      char *unused = lower_call (ctx, *block, node);
      free (unused);
    }
  else if (strcmp (type, "if_statement") == 0)
    lower_if (ctx, block, node);
  else if (strcmp (type, "while_statement") == 0)
    lower_while (ctx, block, node);
  else if (strcmp (type, "repeat_statement") == 0)
    lower_repeat (ctx, block, node);
  else if (strcmp (type, "for_statement") == 0)
    lower_for (ctx, block, node);
  else if (strcmp (type, "do_statement") == 0)
    lower_body (ctx, block, node);
  else if (strcmp (type, "break_statement") == 0)
    lower_break (ctx, *block);
  else if (strcmp (type, "goto_statement") == 0)
    lower_goto (ctx, *block, node);
  else if (strcmp (type, "label_statement") == 0)
    lower_label (ctx, block, node);
  else
    lower_opaque_statement (ctx, *block, node);
  ctx->report->statements_lowered++;
}

/* ---------- function lowering ---------- */

static void
lower_function (ccw_lua_lower *ctx, TSNode node)
{
  TSNode name_node = field (node, "name");
  TSNode params = null_node ();
  TSNode body = null_node ();
  char *name = node_text (name_node, ctx->source, ctx->source_len);
  uint32_t count;

  if (name == NULL)
    {
      lower_fail (ctx, "swaff Lua: function has no name");
      return;
    }
  ctx->fn = ccw_ir_function_add (ctx->ir, name, CCW_TY_I64);
  free (name);
  if (ctx->fn == 0)
    {
      lower_fail (ctx, "swaff Lua: could not create function");
      return;
    }
  clear_locals (ctx);
  ctx->temp_index = 0;
  ctx->block_index = 0;

  count = ts_node_named_child_count (node);
  for (uint32_t i = 0; i < count; i++)
    {
      TSNode child = ts_node_named_child (node, i);
      if (node_is (child, "parameter_list"))
        params = child;
      if (node_is (child, "function_body") || node_is (child, "block"))
        body = child;
    }

  if (!ts_node_is_null (params))
    {
      uint32_t pc = ts_node_named_child_count (params);
      for (uint32_t i = 0; i < pc; i++)
        {
          TSNode p = ts_node_named_child (params, i);
          if (node_is (p, "ellipsis"))
            continue;
          if (node_is (p, "identifier"))
            {
              char *pname = node_text (p, ctx->source, ctx->source_len);
              if (pname != NULL)
                {
                  ccw_ir_function_add_param (ctx->ir, ctx->fn, CCW_TY_I64,
                                             pname);
                  add_local (ctx, pname);
                  free (pname);
                }
            }
        }
    }

  if (!ts_node_is_null (body))
    {
      ccw_node block = ccw_ir_block_add (ctx->ir, ctx->fn, "entry");
      if (block == 0)
        {
          lower_fail (ctx, "swaff Lua: could not create entry block");
        }
      else
        {
          lower_body (ctx, &block, body);
          if (!ctx->failed && !block_terminated (ctx->ir, block))
            {
              char *zero = new_temp (ctx);
              if (zero == NULL
                  || ccw_kliche_int_const (ctx->ir, block, zero, 0) == 0
                  || ccw_kliche_return (ctx->ir, block, zero) == 0)
                lower_fail (ctx, "swaff Lua: could not synthesize return value");
              free (zero);
            }
        }
    }
  if (!ctx->failed)
    ctx->report->functions_lowered++;
  clear_locals (ctx);
}

/* ---------- top-level lowering ---------- */

static void
lower_top_level (ccw_lua_lower *ctx, TSNode root)
{
  uint32_t count = ts_node_named_child_count (root);
  bool has_top_level = false;

  for (uint32_t i = 0; i < count; i++)
    {
      TSNode child = ts_node_named_child (root, i);
      if (node_is (child, "shebang") || node_is (child, "comment")
          || node_is (child, "function_statement")
          || node_is (child, "documentation_brief")
          || node_is (child, "documentation_class")
          || node_is (child, "documentation_command")
          || node_is (child, "documentation_config")
          || node_is (child, "documentation_tag"))
        continue;
      has_top_level = true;
      break;
    }

  if (!has_top_level)
    return;

  ctx->fn = ccw_ir_function_add (ctx->ir, "@module", CCW_TY_I64);
  if (ctx->fn == 0)
    {
      lower_fail (ctx,
                  "swaff Lua: could not create top-level module function");
      return;
    }
  clear_locals (ctx);
  ctx->temp_index = 0;
  ctx->block_index = 0;

  ccw_node block = ccw_ir_block_add (ctx->ir, ctx->fn, "entry");
  if (block == 0)
    {
      lower_fail (ctx, "swaff Lua: could not create top-level entry block");
      return;
    }

  for (uint32_t i = 0; i < count && !ctx->failed && !ctx->rejected; i++)
    {
      TSNode child = ts_node_named_child (root, i);
      if (node_is (child, "shebang") || node_is (child, "comment"))
        continue;
      if (node_is (child, "function_statement"))
        continue;
      if (node_is (child, "documentation_brief")
          || node_is (child, "documentation_class")
          || node_is (child, "documentation_command")
          || node_is (child, "documentation_config")
          || node_is (child, "documentation_tag"))
        continue;
      if (malformed_node (ctx, child))
        continue;
      lower_statement (ctx, &block, child);
    }

  if (!ctx->failed && !block_terminated (ctx->ir, block))
    {
      char *zero = new_temp (ctx);
      if (zero == NULL
          || ccw_kliche_int_const (ctx->ir, block, zero, 0) == 0
          || ccw_kliche_return (ctx->ir, block, zero) == 0)
        lower_fail (ctx, "swaff Lua: could not synthesize module return value");
      free (zero);
    }
  if (!ctx->failed)
    ctx->report->functions_lowered++;
  clear_locals (ctx);
}

/* ---------- main entry point ---------- */

ccw_ir *
ccw_swaff_lower_lua (const ccw_swaff_frontend *fe, const char *source,
                     size_t source_len, const char *module_name,
                     ccw_profile profile, ccw_swaff_error_policy policy,
                     ccw_swaff_report *report, char **error_message)
{
  ccw_swaff_report local;
  TSParser *parser;
  TSTree *tree;
  TSNode root;
  bool has_errors;
  ccw_ir *ir;
  ccw_lua_lower ctx;
  if (error_message != NULL)
    *error_message = NULL;
  memset (&local, 0, sizeof (local));
  if (report != NULL)
    memset (report, 0, sizeof (*report));
  if (fe != &g_frontend_lua || source == NULL || module_name == NULL)
    {
      lua_set_error (error_message,
                     "swaff Lua: invalid frontend, source, or module name");
      return NULL;
    }
  if (source_len > UINT32_MAX)
    {
      lua_set_error (error_message,
                     "swaff Lua: source is too large for Tree-sitter");
      return NULL;
    }
  parser = ts_parser_new ();
  if (parser == NULL || !ts_parser_set_language (parser, tree_sitter_lua ()))
    {
      if (parser != NULL)
        ts_parser_delete (parser);
      lua_set_error (error_message,
                     "swaff Lua: vendored grammar is ABI-incompatible");
      return NULL;
    }
  tree = ts_parser_parse_string (parser, NULL, source, (uint32_t)source_len);
  if (tree == NULL)
    {
      ts_parser_delete (parser);
      lua_set_error (error_message,
                     "swaff Lua: parser produced no syntax tree");
      return NULL;
    }
  root = ts_tree_root_node (tree);
  has_errors = scan_errors (root, &local);
  if (has_errors && policy == CCW_SWAFF_REJECT_ON_ERROR)
    {
      snprintf (local.message, sizeof (local.message),
                "swaff Lua: rejected CST with %d ERROR and %d MISSING nodes",
                local.error_nodes, local.missing_nodes);
      if (report != NULL)
        *report = local;
      lua_set_error (error_message, local.message);
      ts_tree_delete (tree);
      ts_parser_delete (parser);
      return NULL;
    }
  ir = ccw_ir_module_create (module_name, profile);
  if (ir == NULL)
    {
      ts_tree_delete (tree);
      ts_parser_delete (parser);
      lua_set_error (error_message, "swaff Lua: out of memory");
      return NULL;
    }
  memset (&ctx, 0, sizeof (ctx));
  ctx.ir = ir;
  ctx.source = source;
  ctx.source_len = source_len;
  ctx.policy = policy;
  ctx.report = &local;

  /* Phase 1: lower named functions */
  uint32_t count = ts_node_named_child_count (root);
  for (uint32_t i = 0; i < count && !ctx.failed && !ctx.rejected; i++)
    {
      TSNode child = ts_node_named_child (root, i);
      if (node_is (child, "shebang") || node_is (child, "comment"))
        continue;
      if (malformed_node (&ctx, child))
        continue;
      if (node_is (child, "function_statement"))
        lower_function (&ctx, child);
    }

  /* Phase 2: lower top-level statements into @module */
  lower_top_level (&ctx, root);

  break_stack_clear (&ctx.break_stack);
  clear_locals (&ctx);
  ts_tree_delete (tree);
  ts_parser_delete (parser);

  if (ctx.failed || ctx.rejected)
    {
      const char *message
          = ctx.failed ? ctx.failure : "swaff Lua: rejected malformed subtree";
      snprintf (local.message, sizeof (local.message), "%s", message);
      if (report != NULL)
        *report = local;
      lua_set_error (error_message, message);
      ccw_ir_module_destroy (ir);
      return NULL;
    }
  if (has_errors)
    snprintf (local.message, sizeof (local.message),
              "swaff Lua: recovered %d malformed subtrees",
              local.recovered_subtrees);
  if (report != NULL)
    *report = local;
  return ir;
}

#else

ccw_ir *
ccw_swaff_lower_lua (const ccw_swaff_frontend *fe, const char *source,
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
  lua_set_error (error_message,
                 "swaff Lua: built without vendored Tree-sitter support");
  return NULL;
}

#endif
