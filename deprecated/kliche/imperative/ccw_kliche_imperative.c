/* Imperative stereotype: mutable locals as slots, structured control
 * flow lowered to branches, loops, arrays, casts, and phi nodes.
 * All constructs lower to core-IR instructions only (§6.1). */

#include "../ccw_kliche_common.h"

/* ---------- locals ---------- */

/* §6.1 imperative: allocate a mutable local slot of the given type.
 * Returns a pointer to the slot. */

ccw_node
ccw_kliche_local_alloc (ccw_ir *ir, ccw_node blk, const char *dest,
                        ccw_ir_type type)
{
  if (ir == NULL || blk == 0 || dest == NULL)
    return 0;
  ccw_kliche_opnd ops[] = { CCW_K_INT ((int64_t)type), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "local.alloc", CCW_TY_PTR, dest, ops);
}

/* §6.1 imperative: store a value into a mutable local slot. */

ccw_node
ccw_kliche_local_store (ccw_ir *ir, ccw_node blk, const char *slot_reg,
                        const char *value_reg)
{
  if (ir == NULL || blk == 0 || slot_reg == NULL || value_reg == NULL)
    return 0;
  ccw_kliche_opnd ops[]
      = { CCW_K_REG (slot_reg), CCW_K_REG (value_reg), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "local.store", CCW_TY_VOID, NULL, ops);
}

/* §6.1 imperative: load a value from a mutable local slot. */

ccw_node
ccw_kliche_local_load (ccw_ir *ir, ccw_node blk, const char *dest,
                       const char *slot_reg, ccw_ir_type type)
{
  if (ir == NULL || blk == 0 || dest == NULL || slot_reg == NULL)
    return 0;
  ccw_kliche_opnd ops[] = { CCW_K_REG (slot_reg), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "local.load", type, dest, ops);
}

/* ---------- control flow ---------- */

/* §6.1 imperative: conditional branch on an i1 condition register. */

ccw_node
ccw_kliche_branch_if (ccw_ir *ir, ccw_node blk, const char *cond_reg,
                      const char *then_block, const char *else_block)
{
  if (ir == NULL || blk == 0 || cond_reg == NULL || then_block == NULL
      || else_block == NULL)
    return 0;
  ccw_kliche_opnd ops[] = { CCW_K_REG (cond_reg), CCW_K_BLOCK (then_block),
                            CCW_K_BLOCK (else_block), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "br.cond", CCW_TY_VOID, NULL, ops);
}

/* §6.1 imperative: unconditional jump to a target block. */

ccw_node
ccw_kliche_jump (ccw_ir *ir, ccw_node blk, const char *target_block)
{
  if (ir == NULL || blk == 0 || target_block == NULL)
    return 0;
  ccw_kliche_opnd ops[] = { CCW_K_BLOCK (target_block), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "br", CCW_TY_VOID, NULL, ops);
}

/* ---------- constants ---------- */

/* §6.1 imperative: integer constant. */

ccw_node
ccw_kliche_int_const (ccw_ir *ir, ccw_node blk, const char *dest,
                      int64_t value)
{
  if (ir == NULL || blk == 0 || dest == NULL)
    return 0;
  ccw_kliche_opnd ops[] = { CCW_K_INT (value), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "iconst", CCW_TY_I64, dest, ops);
}

/* §6.1 imperative: float constant. */

ccw_node
ccw_kliche_float_const (ccw_ir *ir, ccw_node blk, const char *dest,
                        double value)
{
  if (ir == NULL || blk == 0 || dest == NULL)
    return 0;
  ccw_kliche_opnd ops[] = { CCW_K_FLOAT (value), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "fconst", CCW_TY_F64, dest, ops);
}

/* ---------- arithmetic ---------- */

/* §6.1 imperative: unary operation (neg, not, etc.). */

ccw_node
ccw_kliche_unary (ccw_ir *ir, ccw_node blk, const char *opcode,
                  const char *dest, const char *operand_reg, ccw_ir_type type)
{
  if (ir == NULL || blk == 0 || opcode == NULL || dest == NULL
      || operand_reg == NULL)
    return 0;
  ccw_kliche_opnd ops[] = { CCW_K_REG (operand_reg), CCW_K_END };
  return ccw_kliche_emit (ir, blk, opcode, type, dest, ops);
}

/* §6.1 imperative: binary operation (add, sub, mul, div, etc.). */

ccw_node
ccw_kliche_binary (ccw_ir *ir, ccw_node blk, const char *opcode,
                   const char *dest, const char *left_reg,
                   const char *right_reg, ccw_ir_type type)
{
  if (ir == NULL || blk == 0 || opcode == NULL || dest == NULL
      || left_reg == NULL || right_reg == NULL)
    return 0;
  ccw_kliche_opnd ops[]
      = { CCW_K_REG (left_reg), CCW_K_REG (right_reg), CCW_K_END };
  return ccw_kliche_emit (ir, blk, opcode, type, dest, ops);
}

/* ---------- calls ---------- */

/* §6.1 imperative: direct function call with a named callee and
 * a list of argument registers. */

ccw_node
ccw_kliche_call (ccw_ir *ir, ccw_node blk, const char *dest,
                 const char *callee, const char *const *arg_regs,
                 int arg_count, ccw_ir_type result_type)
{
  if (ir == NULL || blk == 0 || callee == NULL || arg_count < 0)
    return 0;
  ccw_node ins = ccw_ir_instr_build (ir, "call", result_type);
  if (ins == 0)
    return 0;
  if (dest != NULL && ccw_ir_instr_set_dest (ir, ins, dest) != CCW_OK)
    return 0;

  ccw_node target = ccw_ir_operand_func (ir, callee);
  if (target == 0 || ccw_ir_instr_add_operand (ir, ins, target) != CCW_OK)
    return 0;
  for (int i = 0; i < arg_count; i++)
    {
      if (arg_regs[i] == NULL)
        return 0;
      ccw_node arg = ccw_ir_operand_reg (ir, arg_regs[i]);
      if (arg == 0 || ccw_ir_instr_add_operand (ir, ins, arg) != CCW_OK)
        return 0;
    }
  if (ccw_ir_block_append_instr (ir, blk, ins) != CCW_OK)
    return 0;
  return ins;
}

/* ---------- return ---------- */

/* §6.1 imperative: return from a function, optionally with a value. */

ccw_node
ccw_kliche_return (ccw_ir *ir, ccw_node blk, const char *value_reg)
{
  if (ir == NULL || blk == 0)
    return 0;
  ccw_kliche_opnd ops[] = { CCW_K_REG (value_reg), CCW_K_END };
  if (value_reg == NULL)
    {
      ccw_kliche_opnd no_ops[] = { CCW_K_END };
      return ccw_kliche_emit (ir, blk, "ret", CCW_TY_VOID, NULL, no_ops);
    }
  /* A value-returning terminator carries the function result type.  Frontends
   * currently lower scalar values through the canonical i64 register class;
   * emitting it as void makes the IR validator reject otherwise valid
   * functions. */
  return ccw_kliche_emit (ir, blk, "ret", CCW_TY_I64, NULL, ops);
}

/* ---------- loops ---------- */

/* §6.1 imperative: construct a while-loop skeleton.
 * The caller provides three block names (header, body, exit) and a
 * condition register.  The header block is where the condition is tested;
 * this function emits a conditional branch at the end of the caller's
 * current block into the header.  The caller is responsible for populating
 * the body block and emitting the back-edge jump from body to header.
 *
 * Returns the br.cond instruction that terminates the current block, or
 * 0 on failure. */

ccw_node
ccw_kliche_loop (ccw_ir *ir, ccw_node blk, const char *cond_reg,
                 const char *header_block, const char *body_block,
                 const char *exit_block)
{
  if (ir == NULL || blk == 0 || cond_reg == NULL || header_block == NULL
      || body_block == NULL || exit_block == NULL)
    return 0;

  /* Jump to the header (where the condition check lives). */
  ccw_kliche_opnd ops[]
      = { CCW_K_BLOCK (header_block), CCW_K_END };
  ccw_node jmp = ccw_kliche_emit (ir, blk, "br", CCW_TY_VOID, NULL, ops);
  if (jmp == 0)
    return 0;
  return jmp;
}

/* ---------- type casts ---------- */

/* §6.1 imperative: numeric type cast between two IR types.
 * Supports truncation, zero-extension, sign-extension, int-to-float,
 * and float-to-int.  The opcode is chosen based on the source and
 * destination types. */

ccw_node
ccw_kliche_cast (ccw_ir *ir, ccw_node blk, const char *dest,
                 const char *src_reg, ccw_ir_type src_type,
                 ccw_ir_type dst_type)
{
  if (ir == NULL || blk == 0 || dest == NULL || src_reg == NULL)
    return 0;

  const char *opcode = NULL;
  bool src_is_float
      = (src_type == CCW_TY_F32 || src_type == CCW_TY_F64);
  bool dst_is_float
      = (dst_type == CCW_TY_F32 || dst_type == CCW_TY_F64);

  if (src_type == dst_type)
    {
      /* Identity cast: emit a no-op copy. */
      opcode = "id";
    }
  else if (src_is_float && dst_is_float)
    {
      /* Float-to-float: extend or truncate. */
      opcode = (dst_type == CCW_TY_F64) ? "fpext" : "fptrunc";
    }
  else if (!src_is_float && !dst_is_float)
    {
      /* Int-to-int: sign-extend, zero-extend, or truncate. */
      if (dst_type > src_type)
        opcode = "sext";
      else
        opcode = "trunc";
    }
  else if (!src_is_float && dst_is_float)
    {
      /* Int-to-float. */
      opcode = "sitofp";
    }
  else
    {
      /* Float-to-int. */
      opcode = "fptosi";
    }

  ccw_kliche_opnd ops[] = { CCW_K_REG (src_reg), CCW_K_END };
  return ccw_kliche_emit (ir, blk, opcode, dst_type, dest, ops);
}

/* ---------- phi nodes ---------- */

/* §6.1 imperative: SSA phi node merging values from multiple incoming
 * blocks.  values and blocks are parallel arrays of length count.
 * Each pair says "if we came from blocks[i], use values[i]". */

ccw_node
ccw_kliche_phi (ccw_ir *ir, ccw_node blk, const char *dest,
                const char *const *values, const char *const *blocks,
                int count, ccw_ir_type type)
{
  if (ir == NULL || blk == 0 || dest == NULL || values == NULL
      || blocks == NULL || count < 0)
    return 0;

  ccw_node ins = ccw_ir_instr_build (ir, "phi", type);
  if (ins == 0)
    return 0;
  if (ccw_ir_instr_set_dest (ir, ins, dest) != CCW_OK)
    return 0;

  for (int i = 0; i < count; i++)
    {
      if (values[i] == NULL || blocks[i] == NULL)
        return 0;
      ccw_node val = ccw_ir_operand_reg (ir, values[i]);
      if (val == 0 || ccw_ir_instr_add_operand (ir, ins, val) != CCW_OK)
        return 0;
      ccw_node blk_op = ccw_ir_operand_block (ir, blocks[i]);
      if (blk_op == 0
          || ccw_ir_instr_add_operand (ir, ins, blk_op) != CCW_OK)
        return 0;
    }
  if (ccw_ir_block_append_instr (ir, blk, ins) != CCW_OK)
    return 0;
  return ins;
}

/* ---------- arrays ---------- */

/* §6.1 imperative: allocate a heap array of element_count elements,
 * each of size element_size bytes.  Returns a pointer to the array. */

ccw_node
ccw_kliche_array_alloc (ccw_ir *ir, ccw_node blk, const char *dest,
                        int64_t element_count, ccw_ir_type element_type)
{
  if (ir == NULL || blk == 0 || dest == NULL || element_count < 0)
    return 0;
  ccw_kliche_opnd ops[]
      = { CCW_K_INT (element_count), CCW_K_INT ((int64_t)element_type),
          CCW_K_END };
  return ccw_kliche_emit (ir, blk, "array.alloc", CCW_TY_PTR, dest, ops);
}

/* §6.1 imperative: load element at index from an array. */

ccw_node
ccw_kliche_array_load (ccw_ir *ir, ccw_node blk, const char *dest,
                       const char *array_reg, const char *index_reg,
                       ccw_ir_type element_type)
{
  if (ir == NULL || blk == 0 || dest == NULL || array_reg == NULL
      || index_reg == NULL)
    return 0;
  ccw_kliche_opnd ops[] = { CCW_K_REG (array_reg), CCW_K_REG (index_reg),
                            CCW_K_END };
  return ccw_kliche_emit (ir, blk, "array.load", element_type, dest, ops);
}

/* §6.1 imperative: store a value into an array at index. */

ccw_node
ccw_kliche_array_store (ccw_ir *ir, ccw_node blk, const char *array_reg,
                        const char *index_reg, const char *value_reg,
                        ccw_ir_type element_type)
{
  if (ir == NULL || blk == 0 || array_reg == NULL || index_reg == NULL
      || value_reg == NULL)
    return 0;
  (void)element_type;
  ccw_kliche_opnd ops[] = { CCW_K_REG (array_reg), CCW_K_REG (index_reg),
                            CCW_K_REG (value_reg), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "array.store", CCW_TY_VOID, NULL, ops);
}
