/* The Core Accessor Set (GlueSTD.h). Every accessor is a thin wrapper
 * over the IR C API: chatty-but-trivial by design, no batching. */

#include "ccw_host_accessors.h"
#include "ccw_ir.h"
#include "kstring.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ccw_edit_hook g_edit_hook = NULL;
static void *g_edit_hook_data = NULL;

static bool edit_allowed (ccw_ir *ir, ccw_edit_kind kind, ccw_node target,
                          ccw_node incoming);

void
ccw_host_set_edit_hook (ccw_edit_hook hook, void *user_data)
{
  g_edit_hook = hook;
  g_edit_hook_data = user_data;
}

static ccw_status
acc_fail (char **error_message, const char *msg)
{
  if (error_message != NULL)
    {
      kstring_t copy = { 0, 0, NULL };
      if (msg != NULL && kputs (msg, &copy) != EOF)
        *error_message = ks_release (&copy);
      else
        *error_message = NULL;
    }
  return CCW_ERR_ACCESSOR;
}

/* Args arrive as ccw_val; node ids reach Scheme as integers, so accept
 * both CCW_T_NODE and CCW_T_INT for node-typed parameters. */
static bool
arg_node (const ccw_val *v, ccw_node *out)
{
  if (v->type == CCW_T_NODE)
    {
      *out = v->as.node;
      return true;
    }
  if (v->type == CCW_T_INT && v->as.i >= 0)
    {
      *out = (ccw_node)v->as.i;
      return true;
    }
  return false;
}

static bool
arg_index (const ccw_val *v, int *out)
{
  if (v->type != CCW_T_INT)
    return false;
  *out = (int)v->as.i;
  return true;
}

static bool
arg_symbol (const ccw_val *v, const char **out)
{
  if (v->type != CCW_T_SYMBOL && v->type != CCW_T_STRING)
    return false;
  *out = v->as.s;
  return true;
}

static bool
arg_string (const ccw_val *v, const char **out)
{
  if (v->type != CCW_T_STRING && v->type != CCW_T_SYMBOL)
    return false;
  *out = v->as.s;
  return true;
}

#define ACC_SIG(name)                                                         \
  static ccw_status name (void *host_ctx, ccw_ir *ir, const ccw_val *args,    \
                          int nargs, ccw_val *result, char **error_message)

#define ACC_UNUSED                                                            \
  (void)host_ctx;                                                             \
  (void)args;                                                                 \
  (void)nargs;

/* ---------- reflection ---------- */

ACC_SIG (acc_glue_has)
{
  (void)host_ctx;
  (void)ir;
  const char *name = NULL;
  if (nargs != 1 || !arg_symbol (&args[0], &name))
    return acc_fail (error_message, "expects one symbol");
  /* Portable kernels feature-test with this before using extensions.
   * Every Core Accessor Set name answers #t. */
  static const char *const core[]
      = { "glue-has?",          "ir-profile",           "ir-function-count",
          "ir-function-ref",    "function-name",        "function-block-count",
          "function-block-ref", "block-instr-count",    "block-instr-ref",
          "instr-opcode",       "instr-terminator?",    "instr-operand-count",
          "instr-operand",      "operand-const?",       "const-int-value",
          "instr-build",
          "instr-replace!",     "instr-insert-before!", "instr-delete!",
          "const-int-build",    "syscall-build",        "io-read-build",
          "io-write-build",     "io-close-build",       "io-open-build",
          "node-kind",          "operand-kind",         "operand-name",
          "instr-dest",         "instr-set-dest!",      "operand-reg-build",
          "instr-set-operand!", "analysis-put!",        "block-succ-count",
          "block-succ-ref",     "block-pred-count",     "block-pred-ref",
          "block-delete!",      "block-merge!" };
  bool found = false;
  for (size_t i = 0; i < sizeof (core) / sizeof (core[0]); i++)
    if (strcmp (core[i], name) == 0)
      {
        found = true;
        break;
      }
  *result = ccw_bool (found);
  return CCW_OK;
}

ACC_SIG (acc_ir_profile)
{
  ACC_UNUSED (void) error_message;
  *result = ccw_symbol (ccw_profile_name (ccw_ir_module_profile (ir)));
  return CCW_OK;
}

/* ---------- navigation ---------- */

ACC_SIG (acc_ir_function_count)
{
  ACC_UNUSED (void) error_message;
  *result = ccw_int (ccw_ir_function_count (ir));
  return CCW_OK;
}

ACC_SIG (acc_ir_function_ref)
{
  (void)host_ctx;
  int idx = 0;
  if (nargs != 1 || !arg_index (&args[0], &idx))
    return acc_fail (error_message, "expects one integer index");
  ccw_node fn = ccw_ir_function_ref (ir, idx);
  if (fn == 0)
    return acc_fail (error_message, "function index out of range");
  *result = ccw_node_val (fn);
  return CCW_OK;
}

ACC_SIG (acc_function_name)
{
  (void)host_ctx;
  ccw_node fn = 0;
  if (nargs != 1 || !arg_node (&args[0], &fn))
    return acc_fail (error_message, "expects one function node");
  const char *name = ccw_ir_function_name (ir, fn);
  if (name == NULL)
    return acc_fail (error_message, "not a function node");
  *result = ccw_string (name);
  return CCW_OK;
}

ACC_SIG (acc_function_block_count)
{
  (void)host_ctx;
  ccw_node fn = 0;
  if (nargs != 1 || !arg_node (&args[0], &fn))
    return acc_fail (error_message, "expects one function node");
  if (ccw_ir_node_kind (ir, fn) != CCW_NODE_FUNCTION)
    return acc_fail (error_message, "not a function node");
  *result = ccw_int (ccw_ir_function_block_count (ir, fn));
  return CCW_OK;
}

ACC_SIG (acc_function_block_ref)
{
  (void)host_ctx;
  ccw_node fn = 0;
  int idx = 0;
  if (nargs != 2 || !arg_node (&args[0], &fn) || !arg_index (&args[1], &idx))
    return acc_fail (error_message, "expects a function node and an index");
  ccw_node blk = ccw_ir_function_block_ref (ir, fn, idx);
  if (blk == 0)
    return acc_fail (error_message, "block index out of range");
  *result = ccw_node_val (blk);
  return CCW_OK;
}

ACC_SIG (acc_block_instr_count)
{
  (void)host_ctx;
  ccw_node blk = 0;
  if (nargs != 1 || !arg_node (&args[0], &blk))
    return acc_fail (error_message, "expects one block node");
  if (ccw_ir_node_kind (ir, blk) != CCW_NODE_BLOCK)
    return acc_fail (error_message, "not a block node");
  *result = ccw_int (ccw_ir_block_instr_count (ir, blk));
  return CCW_OK;
}

ACC_SIG (acc_block_instr_ref)
{
  (void)host_ctx;
  ccw_node blk = 0;
  int idx = 0;
  if (nargs != 2 || !arg_node (&args[0], &blk) || !arg_index (&args[1], &idx))
    return acc_fail (error_message, "expects a block node and an index");
  ccw_node ins = ccw_ir_block_instr_ref (ir, blk, idx);
  if (ins == 0)
    return acc_fail (error_message, "instruction index out of range");
  *result = ccw_node_val (ins);
  return CCW_OK;
}

ACC_SIG (acc_block_succ_count)
{
  (void)host_ctx;
  ccw_node block = 0;
  if (nargs != 1 || !arg_node (&args[0], &block)
      || ccw_ir_node_kind (ir, block) != CCW_NODE_BLOCK)
    return acc_fail (error_message, "expects one block node");
  *result = ccw_int (ccw_ir_block_successor_count (ir, block));
  return CCW_OK;
}

ACC_SIG (acc_block_succ_ref)
{
  (void)host_ctx;
  ccw_node block = 0;
  int index = 0;
  if (nargs != 2 || !arg_node (&args[0], &block)
      || !arg_index (&args[1], &index))
    return acc_fail (error_message, "expects a block node and an index");
  ccw_node successor = ccw_ir_block_successor_ref (ir, block, index);
  if (successor == 0)
    return acc_fail (error_message, "successor index out of range");
  *result = ccw_node_val (successor);
  return CCW_OK;
}

ACC_SIG (acc_block_pred_count)
{
  (void)host_ctx;
  ccw_node block = 0;
  if (nargs != 1 || !arg_node (&args[0], &block)
      || ccw_ir_node_kind (ir, block) != CCW_NODE_BLOCK)
    return acc_fail (error_message, "expects one block node");
  *result = ccw_int (ccw_ir_block_predecessor_count (ir, block));
  return CCW_OK;
}

ACC_SIG (acc_block_pred_ref)
{
  (void)host_ctx;
  ccw_node block = 0;
  int index = 0;
  if (nargs != 2 || !arg_node (&args[0], &block)
      || !arg_index (&args[1], &index))
    return acc_fail (error_message, "expects a block node and an index");
  ccw_node predecessor = ccw_ir_block_predecessor_ref (ir, block, index);
  if (predecessor == 0)
    return acc_fail (error_message, "predecessor index out of range");
  *result = ccw_node_val (predecessor);
  return CCW_OK;
}

ACC_SIG (acc_block_delete)
{
  (void)host_ctx;
  ccw_node block = 0;
  if (nargs != 1 || !arg_node (&args[0], &block))
    return acc_fail (error_message, "expects one block node");
  if (!edit_allowed (ir, CCW_EDIT_BLOCK_DELETE, block, 0))
    return acc_fail (error_message, "edit rejected by host");
  if (ccw_ir_block_delete (ir, block) != CCW_OK)
    return acc_fail (error_message, "block deletion requires no predecessors");
  *result = ccw_nil ();
  return CCW_OK;
}

ACC_SIG (acc_block_merge)
{
  (void)host_ctx;
  ccw_node first = 0, second = 0;
  if (nargs != 2 || !arg_node (&args[0], &first)
      || !arg_node (&args[1], &second))
    return acc_fail (error_message, "expects two block nodes");
  if (!edit_allowed (ir, CCW_EDIT_BLOCK_DELETE, first, second)
      || ccw_ir_block_merge (ir, first, second) != CCW_OK)
    return acc_fail (error_message, "blocks are not linearly mergeable");
  *result = ccw_nil ();
  return CCW_OK;
}

/* ---------- inspection ---------- */

ACC_SIG (acc_instr_opcode)
{
  (void)host_ctx;
  ccw_node ins = 0;
  if (nargs != 1 || !arg_node (&args[0], &ins))
    return acc_fail (error_message, "expects one instruction node");
  const char *op = ccw_ir_instr_opcode (ir, ins);
  if (op == NULL)
    return acc_fail (error_message, "not an instruction node");
  *result = ccw_symbol (op);
  return CCW_OK;
}

ACC_SIG (acc_instr_operand_count)
{
  (void)host_ctx;
  ccw_node ins = 0;
  if (nargs != 1 || !arg_node (&args[0], &ins))
    return acc_fail (error_message, "expects one instruction node");
  if (ccw_ir_node_kind (ir, ins) != CCW_NODE_INSTR)
    return acc_fail (error_message, "not an instruction node");
  *result = ccw_int (ccw_ir_instr_operand_count (ir, ins));
  return CCW_OK;
}

ACC_SIG (acc_instr_operand)
{
  (void)host_ctx;
  ccw_node ins = 0;
  int idx = 0;
  if (nargs != 2 || !arg_node (&args[0], &ins) || !arg_index (&args[1], &idx))
    return acc_fail (error_message,
                     "expects an instruction node and an index");
  ccw_node opnd = ccw_ir_instr_operand (ir, ins, idx);
  if (opnd == 0)
    return acc_fail (error_message, "operand index out of range");
  *result = ccw_node_val (opnd);
  return CCW_OK;
}

ACC_SIG (acc_operand_const)
{
  (void)host_ctx;
  ccw_node n = 0;
  if (nargs != 1 || !arg_node (&args[0], &n))
    return acc_fail (error_message, "expects one operand node");
  *result = ccw_bool (ccw_ir_operand_is_const (ir, n));
  return CCW_OK;
}

ACC_SIG (acc_instr_terminator)
{
  (void)host_ctx;
  ccw_node ins = 0;
  if (nargs != 1 || !arg_node (&args[0], &ins))
    return acc_fail (error_message, "expects one instruction node");
  *result = ccw_bool (ccw_ir_instr_is_terminator (ir, ins));
  return CCW_OK;
}

ACC_SIG (acc_const_int_value)
{
  (void)host_ctx;
  ccw_node n = 0;
  int64_t value = 0;
  if (nargs != 1 || !arg_node (&args[0], &n))
    return acc_fail (error_message, "expects one operand node");
  if (ccw_ir_const_int_value (ir, n, &value) != CCW_OK)
    return acc_fail (error_message, "operand is not an integer constant");
  *result = ccw_int (value);
  return CCW_OK;
}

/* ---------- approved extension set: Phase 1 ---------- */

static const char *
node_kind_name (ccw_node_kind kind)
{
  switch (kind)
    {
    case CCW_NODE_FUNCTION:
      return "function";
    case CCW_NODE_BLOCK:
      return "block";
    case CCW_NODE_INSTR:
      return "instr";
    case CCW_NODE_OPERAND:
      return "operand";
    default:
      return "dead";
    }
}

static const char *
operand_kind_name (ccw_operand_kind kind)
{
  switch (kind)
    {
    case CCW_OPND_REG:
      return "reg";
    case CCW_OPND_CONST_INT:
      return "const-int";
    case CCW_OPND_CONST_FLOAT:
      return "const-float";
    case CCW_OPND_FUNC:
      return "func";
    case CCW_OPND_BLOCK:
      return "block";
    default:
      return "unknown";
    }
}

ACC_SIG (acc_node_kind)
{
  (void)host_ctx;
  ccw_node node = 0;
  if (nargs != 1 || !arg_node (&args[0], &node))
    return acc_fail (error_message, "expects one node");
  if (ccw_ir_node_kind (ir, node) == CCW_NODE_DEAD)
    return acc_fail (error_message, "unknown node");
  *result = ccw_symbol (node_kind_name (ccw_ir_node_kind (ir, node)));
  return CCW_OK;
}

ACC_SIG (acc_operand_kind)
{
  (void)host_ctx;
  ccw_node node = 0;
  if (nargs != 1 || !arg_node (&args[0], &node))
    return acc_fail (error_message, "expects one operand node");
  if (ccw_ir_node_kind (ir, node) != CCW_NODE_OPERAND)
    return acc_fail (error_message, "not an operand node");
  *result = ccw_symbol (operand_kind_name (ccw_ir_operand_kind (ir, node)));
  return CCW_OK;
}

ACC_SIG (acc_operand_name)
{
  (void)host_ctx;
  ccw_node node = 0;
  if (nargs != 1 || !arg_node (&args[0], &node))
    return acc_fail (error_message, "expects one operand node");
  const char *name = ccw_ir_operand_name (ir, node);
  *result = name ? ccw_string (name) : ccw_nil ();
  return CCW_OK;
}

ACC_SIG (acc_instr_dest)
{
  (void)host_ctx;
  ccw_node ins = 0;
  if (nargs != 1 || !arg_node (&args[0], &ins))
    return acc_fail (error_message, "expects one instruction node");
  const char *dest = ccw_ir_instr_dest (ir, ins);
  if (ccw_ir_node_kind (ir, ins) != CCW_NODE_INSTR)
    return acc_fail (error_message, "not an instruction node");
  *result = dest ? ccw_string (dest) : ccw_nil ();
  return CCW_OK;
}

ACC_SIG (acc_instr_set_dest)
{
  (void)host_ctx;
  ccw_node ins = 0;
  const char *name = NULL;
  if (nargs != 2 || !arg_node (&args[0], &ins)
      || !arg_string (&args[1], &name))
    return acc_fail (error_message, "expects an instruction node and a name");
  if (ccw_ir_instr_set_dest (ir, ins, name) != CCW_OK)
    return acc_fail (error_message, "could not set instruction destination");
  *result = ccw_nil ();
  return CCW_OK;
}

ACC_SIG (acc_operand_reg_build)
{
  (void)host_ctx;
  const char *name = NULL;
  if (nargs != 1 || !arg_string (&args[0], &name))
    return acc_fail (error_message, "expects one register name");
  ccw_node node = ccw_ir_operand_reg (ir, name);
  if (node == 0)
    return acc_fail (error_message, "could not build register operand");
  *result = ccw_node_val (node);
  return CCW_OK;
}

ACC_SIG (acc_instr_set_operand)
{
  (void)host_ctx;
  ccw_node ins = 0, operand = 0;
  int index = 0;
  if (nargs != 3 || !arg_node (&args[0], &ins) || !arg_index (&args[1], &index)
      || !arg_node (&args[2], &operand))
    return acc_fail (error_message,
                     "expects an instruction node, index, and operand");
  if (ccw_ir_instr_set_operand (ir, ins, index, operand) != CCW_OK)
    return acc_fail (error_message, "invalid instruction operand");
  *result = ccw_nil ();
  return CCW_OK;
}

static bool
fact_value_text (const ccw_val *value, char *text, size_t size)
{
  switch (value->type)
    {
    case CCW_T_NIL:
      return snprintf (text, size, "nil") > 0;
    case CCW_T_BOOL:
      return snprintf (text, size, "%s", value->as.b ? "true" : "false") > 0;
    case CCW_T_INT:
      return snprintf (text, size, "%lld", (long long)value->as.i) > 0;
    case CCW_T_FLOAT:
      return snprintf (text, size, "%.17g", value->as.f) > 0;
    case CCW_T_STRING:
    case CCW_T_SYMBOL:
      return snprintf (text, size, "%s", value->as.s) > 0;
    case CCW_T_NODE:
      return snprintf (text, size, "%llu", (unsigned long long)value->as.node)
             > 0;
    }
  return false;
}

ACC_SIG (acc_analysis_put)
{
  (void)host_ctx;
  const char *capability = NULL, *key = NULL;
  ccw_node subject = 0;
  char attr_key[512], value[256];
  if (nargs != 4 || !arg_symbol (&args[0], &capability)
      || !arg_node (&args[1], &subject) || !arg_symbol (&args[2], &key))
    return acc_fail (error_message,
                     "expects capability, subject, key, and scalar value");
  if (ccw_ir_node_kind (ir, subject) == CCW_NODE_DEAD
      || !fact_value_text (&args[3], value, sizeof (value)))
    return acc_fail (error_message, "invalid analysis fact");
  if (snprintf (attr_key, sizeof (attr_key), "analysis.%s.%s", capability, key)
      >= (int)sizeof (attr_key))
    return acc_fail (error_message, "analysis fact key is too long");
  if (ccw_ir_attr_set (ir, subject, attr_key, value) != CCW_OK)
    return acc_fail (error_message, "could not store analysis fact");
  *result = ccw_nil ();
  return CCW_OK;
}

/* ---------- mutation: the only channel, host-mediated ---------- */

ACC_SIG (acc_instr_build)
{
  (void)host_ctx;
  const char *opcode = NULL;
  if (nargs < 1 || !arg_symbol (&args[0], &opcode))
    return acc_fail (error_message,
                     "expects an opcode symbol and operand nodes");

  /* A detached instruction inherits the type of its first operand, or
   * i64 when built purely from constants. */
  ccw_ir_type type = CCW_TY_I64;
  for (int i = 1; i < nargs; i++)
    {
      ccw_node opnd = 0;
      if (!arg_node (&args[i], &opnd))
        return acc_fail (error_message, "operands must be nodes");
      if (ccw_ir_node_kind (ir, opnd) != CCW_NODE_OPERAND)
        return acc_fail (error_message, "operand is not an operand node");
      if (i == 1)
        {
          ccw_ir_type t = ccw_ir_operand_type (ir, opnd);
          if (t != CCW_TY_VOID)
            type = t;
        }
    }

  ccw_node ins = ccw_ir_instr_build (ir, opcode, type);
  if (ins == 0)
    return acc_fail (error_message, "could not build instruction");
  for (int i = 1; i < nargs; i++)
    {
      ccw_node opnd = 0;
      arg_node (&args[i], &opnd);
      if (ccw_ir_instr_add_operand (ir, ins, opnd) != CCW_OK)
        return acc_fail (error_message, "could not attach operand");
    }
  *result = ccw_node_val (ins);
  return CCW_OK;
}

ACC_SIG (acc_const_int_build)
{
  (void)host_ctx;
  if (nargs != 1 || args[0].type != CCW_T_INT)
    return acc_fail (error_message, "expects one integer");
  ccw_node n = ccw_ir_operand_const_int (ir, CCW_TY_I64, args[0].as.i);
  if (n == 0)
    return acc_fail (error_message, "could not build constant");
  *result = ccw_node_val (n);
  return CCW_OK;
}

ACC_SIG (acc_syscall_build)
{
  (void)host_ctx;
  ccw_node ins;
  ccw_node number_node;
  if (nargs < 1 || nargs > 7 || args[0].type != CCW_T_INT)
    return acc_fail (error_message,
                     "expects syscall number and at most six operands");
  ins = ccw_ir_instr_build (ir, CCW_OP_SYSCALL, CCW_TY_I64);
  if (ins == 0)
    return acc_fail (error_message, "could not build syscall");
  number_node = ccw_ir_operand_const_int (ir, CCW_TY_I64, args[0].as.i);
  if (number_node == 0
      || ccw_ir_instr_add_operand (ir, ins, number_node) != CCW_OK)
    return acc_fail (error_message, "could not attach syscall number");
  for (int i = 1; i < nargs; ++i)
    {
      ccw_node operand = 0;
      if (!arg_node (&args[i], &operand)
          || ccw_ir_node_kind (ir, operand) != CCW_NODE_OPERAND
          || ccw_ir_instr_add_operand (ir, ins, operand) != CCW_OK)
        return acc_fail (error_message, "syscall operands must be nodes");
    }
  *result = ccw_node_val (ins);
  return CCW_OK;
}

static ccw_status
build_io (ccw_ir *ir, const ccw_val *args, int nargs, const char *opcode,
          int expected, ccw_val *result, char **error_message)
{
  ccw_node operands[3];
  ccw_node ins;
  if (nargs != expected)
    return acc_fail (error_message, "incorrect I/O wrapper arity");
  for (int i = 0; i < expected; ++i)
    if (!arg_node (&args[i], &operands[i])
        || ccw_ir_node_kind (ir, operands[i]) != CCW_NODE_OPERAND)
      return acc_fail (error_message, "I/O wrapper operands must be nodes");
  ins = ccw_ir_instr_build (ir, opcode, CCW_TY_I64);
  if (ins == 0)
    return acc_fail (error_message, "could not build I/O wrapper");
  for (int i = 0; i < expected; ++i)
    if (ccw_ir_instr_add_operand (ir, ins, operands[i]) != CCW_OK)
      return acc_fail (error_message, "could not attach I/O wrapper operand");
  *result = ccw_node_val (ins);
  return CCW_OK;
}

ACC_SIG (acc_io_read_build)
{
  (void)host_ctx;
  return build_io (ir, args, nargs, CCW_OP_IO_READ, 3, result, error_message);
}

ACC_SIG (acc_io_write_build)
{
  (void)host_ctx;
  return build_io (ir, args, nargs, CCW_OP_IO_WRITE, 3, result, error_message);
}

ACC_SIG (acc_io_close_build)
{
  (void)host_ctx;
  return build_io (ir, args, nargs, CCW_OP_IO_CLOSE, 1, result, error_message);
}

ACC_SIG (acc_io_open_build)
{
  (void)host_ctx;
  return build_io (ir, args, nargs, CCW_OP_IO_OPEN, 3, result, error_message);
}

static bool
edit_allowed (ccw_ir *ir, ccw_edit_kind kind, ccw_node target,
              ccw_node incoming)
{
  if (g_edit_hook == NULL)
    return true;
  return g_edit_hook (g_edit_hook_data, ir, kind, target, incoming);
}

ACC_SIG (acc_instr_replace)
{
  (void)host_ctx;
  ccw_node old_ins = 0, new_ins = 0;
  if (nargs != 2 || !arg_node (&args[0], &old_ins)
      || !arg_node (&args[1], &new_ins))
    return acc_fail (error_message, "expects two instruction nodes");
  if (!edit_allowed (ir, CCW_EDIT_REPLACE, old_ins, new_ins))
    return acc_fail (error_message, "edit rejected by host");
  if (ccw_ir_instr_replace (ir, old_ins, new_ins) != CCW_OK)
    return acc_fail (error_message,
                     "replacement failed: check attachment state");
  *result = ccw_nil ();
  return CCW_OK;
}

ACC_SIG (acc_instr_insert_before)
{
  (void)host_ctx;
  ccw_node anchor = 0, new_ins = 0;
  if (nargs != 2 || !arg_node (&args[0], &anchor)
      || !arg_node (&args[1], &new_ins))
    return acc_fail (error_message, "expects two instruction nodes");
  if (!edit_allowed (ir, CCW_EDIT_INSERT_BEFORE, anchor, new_ins))
    return acc_fail (error_message, "edit rejected by host");
  if (ccw_ir_instr_insert_before (ir, anchor, new_ins) != CCW_OK)
    return acc_fail (error_message,
                     "insertion failed: check attachment state");
  *result = ccw_nil ();
  return CCW_OK;
}

ACC_SIG (acc_instr_delete)
{
  (void)host_ctx;
  ccw_node ins = 0;
  if (nargs != 1 || !arg_node (&args[0], &ins))
    return acc_fail (error_message, "expects one instruction node");
  if (!edit_allowed (ir, CCW_EDIT_DELETE, ins, 0))
    return acc_fail (error_message, "edit rejected by host");
  if (ccw_ir_instr_delete (ir, ins) != CCW_OK)
    return acc_fail (error_message,
                     "deletion failed: instruction is not attached");
  *result = ccw_nil ();
  return CCW_OK;
}

/* ---------- registration ---------- */

ccw_status
ccw_host_register_core_accessors (ccw_executor *ex)
{
  struct
  {
    const char *name;
    int min;
    int max;
    ccw_accessor_fn fn;
  } table[] = {
    { "glue-has?", 1, 1, acc_glue_has },
    { "ir-profile", 0, 0, acc_ir_profile },
    { "ir-function-count", 0, 0, acc_ir_function_count },
    { "ir-function-ref", 1, 1, acc_ir_function_ref },
    { "function-name", 1, 1, acc_function_name },
    { "function-block-count", 1, 1, acc_function_block_count },
    { "function-block-ref", 2, 2, acc_function_block_ref },
    { "block-instr-count", 1, 1, acc_block_instr_count },
    { "block-instr-ref", 2, 2, acc_block_instr_ref },
    { "block-succ-count", 1, 1, acc_block_succ_count },
    { "block-succ-ref", 2, 2, acc_block_succ_ref },
    { "block-pred-count", 1, 1, acc_block_pred_count },
    { "block-pred-ref", 2, 2, acc_block_pred_ref },
    { "block-delete!", 1, 1, acc_block_delete },
    { "block-merge!", 2, 2, acc_block_merge },
    { "instr-opcode", 1, 1, acc_instr_opcode },
    { "instr-terminator?", 1, 1, acc_instr_terminator },
    { "instr-operand-count", 1, 1, acc_instr_operand_count },
    { "instr-operand", 2, 2, acc_instr_operand },
    { "operand-const?", 1, 1, acc_operand_const },
    { "const-int-value", 1, 1, acc_const_int_value },
    { "node-kind", 1, 1, acc_node_kind },
    { "operand-kind", 1, 1, acc_operand_kind },
    { "operand-name", 1, 1, acc_operand_name },
    { "instr-dest", 1, 1, acc_instr_dest },
    { "instr-set-dest!", 2, 2, acc_instr_set_dest },
    { "operand-reg-build", 1, 1, acc_operand_reg_build },
    { "instr-set-operand!", 3, 3, acc_instr_set_operand },
    { "analysis-put!", 4, 4, acc_analysis_put },
    { "instr-build", 1, -1, acc_instr_build },
    { "instr-replace!", 2, 2, acc_instr_replace },
    { "instr-insert-before!", 2, 2, acc_instr_insert_before },
    { "instr-delete!", 1, 1, acc_instr_delete },
    { "const-int-build", 1, 1, acc_const_int_build },
    { "syscall-build", 1, 7, acc_syscall_build },
    { "io-read-build", 3, 3, acc_io_read_build },
    { "io-write-build", 3, 3, acc_io_write_build },
    { "io-close-build", 1, 1, acc_io_close_build },
    { "io-open-build", 3, 3, acc_io_open_build },
  };
  for (size_t i = 0; i < sizeof (table) / sizeof (table[0]); i++)
    {
      ccw_status st = ccw_glue_register (ex, table[i].name, table[i].min,
                                         table[i].max, table[i].fn, NULL);
      if (st != CCW_OK)
        return st;
    }
  return CCW_OK;
}
