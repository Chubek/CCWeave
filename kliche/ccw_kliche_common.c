/* Shared emit helper for Kliche stereotypes (§6.1).
 * Every emit validates inputs before touching the IR; null pointers
 * and invalid operands produce 0 immediately. */

#include "ccw_kliche_common.h"

#include <stddef.h>

/* Validate that ir and blk are non-null, and that opcode is non-null. */
static bool
emit_preflight (ccw_ir *ir, ccw_node blk, const char *opcode)
{
  return ir != NULL && blk != 0 && opcode != NULL;
}

/* Resolve a single operand node; returns 0 and sets *err on failure. */
static ccw_node
resolve_operand (ccw_ir *ir, const ccw_kliche_opnd *o, bool *err)
{
  switch (o->kind)
    {
    case CCW_KO_REG:
      if (o->name == NULL)
        {
          *err = true;
          return 0;
        }
      return ccw_ir_operand_reg (ir, o->name);
    case CCW_KO_FUNC:
      if (o->name == NULL)
        {
          *err = true;
          return 0;
        }
      return ccw_ir_operand_func (ir, o->name);
    case CCW_KO_BLOCK:
      if (o->name == NULL)
        {
          *err = true;
          return 0;
        }
      return ccw_ir_operand_block (ir, o->name);
    case CCW_KO_INT:
      return ccw_ir_operand_const_int (ir, CCW_TY_I64, o->value);
    case CCW_KO_FLOAT:
      return ccw_ir_operand_const_float (ir, CCW_TY_F64, o->fvalue);
    case CCW_KO_STR:
      /* String constants are lowered to a global symbol reference;
       * the adapter is responsible for emitting the string data. */
      return ccw_ir_operand_func (ir, o->name);
    case CCW_KO_END:
      return 0;
    }
  *err = true;
  return 0;
}

ccw_node
ccw_kliche_emit (ccw_ir *ir, ccw_node blk, const char *opcode,
                 ccw_ir_type type, const char *dest,
                 const ccw_kliche_opnd *operands)
{
  if (!emit_preflight (ir, blk, opcode))
    return 0;

  ccw_node ins = ccw_ir_instr_build (ir, opcode, type);
  if (ins == 0)
    return 0;
  if (dest != NULL)
    {
      if (ccw_ir_instr_set_dest (ir, ins, dest) != CCW_OK)
        return 0;
    }

  for (const ccw_kliche_opnd *o = operands; o != NULL && o->kind != CCW_KO_END;
       o++)
    {
      bool err = false;
      ccw_node opnd = resolve_operand (ir, o, &err);
      if (err)
        return 0;
      if (opnd == 0 || ccw_ir_instr_add_operand (ir, ins, opnd) != CCW_OK)
        return 0;
    }

  if (ccw_ir_block_append_instr (ir, blk, ins) != CCW_OK)
    return 0;
  return ins;
}

ccw_node
ccw_kliche_emit_multi (ccw_ir *ir, ccw_node blk,
                       const ccw_kliche_emit_spec *specs, int count)
{
  if (ir == NULL || blk == 0 || specs == NULL || count <= 0)
    return 0;

  /* Build detached instructions first, then append all to the block.
   * If any instruction fails to build, return 0 immediately; the partial
   * nodes are unreachable and will be cleaned up when the module is freed. */
  ccw_node last = 0;
  for (int i = 0; i < count; i++)
    {
      if (specs[i].opcode == NULL)
        return 0;
      ccw_node ins
          = ccw_ir_instr_build (ir, specs[i].opcode, specs[i].type);
      if (ins == 0)
        return 0;
      if (specs[i].dest != NULL
          && ccw_ir_instr_set_dest (ir, ins, specs[i].dest) != CCW_OK)
        return 0;

      if (specs[i].operands != NULL)
        {
          for (const ccw_kliche_opnd *o = specs[i].operands;
               o->kind != CCW_KO_END; o++)
            {
              bool err = false;
              ccw_node opnd = resolve_operand (ir, o, &err);
              if (err || opnd == 0
                  || ccw_ir_instr_add_operand (ir, ins, opnd) != CCW_OK)
                return 0;
            }
        }
      if (ccw_ir_block_append_instr (ir, blk, ins) != CCW_OK)
        return 0;
      last = ins;
    }
  return last;
}

ccw_node
ccw_kliche_binop (ccw_ir *ir, ccw_node blk, const char *opcode,
                  const char *dest, const char *lhs, const char *rhs,
                  ccw_ir_type type)
{
  ccw_kliche_opnd ops[]
      = { CCW_K_REG (lhs), CCW_K_REG (rhs), CCW_K_END };
  return ccw_kliche_emit (ir, blk, opcode, type, dest, ops);
}

ccw_node
ccw_kliche_cmp (ccw_ir *ir, ccw_node blk, const char *pred, const char *dest,
                const char *lhs, const char *rhs, ccw_ir_type type)
{
  (void)type;
  ccw_kliche_opnd ops[]
      = { CCW_K_REG (lhs), CCW_K_REG (rhs), CCW_K_END };
  return ccw_kliche_emit (ir, blk, pred, CCW_TY_I1, dest, ops);
}

ccw_node ccw_kliche_alloca (ccw_ir *ir, ccw_node blk, const char *dest,
                            int64_t byte_count)
{
  ccw_kliche_opnd ops[] = { CCW_K_INT (byte_count), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "alloca", CCW_TY_PTR, dest, ops);
}

ccw_node ccw_kliche_gep (ccw_ir *ir, ccw_node blk, const char *dest,
                         const char *base_reg, int64_t offset)
{
  ccw_kliche_opnd ops[]
      = { CCW_K_REG (base_reg), CCW_K_INT (offset), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "gep", CCW_TY_PTR, dest, ops);
}

ccw_node ccw_kliche_load (ccw_ir *ir, ccw_node blk, const char *dest,
                          const char *src_reg, ccw_ir_type type)
{
  ccw_kliche_opnd ops[] = { CCW_K_REG (src_reg), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "load", type, dest, ops);
}

ccw_node ccw_kliche_store (ccw_ir *ir, ccw_node blk, const char *dst_reg,
                           const char *value_reg, ccw_ir_type type)
{
  (void)type;
  ccw_kliche_opnd ops[]
      = { CCW_K_REG (dst_reg), CCW_K_REG (value_reg), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "store", CCW_TY_VOID, NULL, ops);
}
