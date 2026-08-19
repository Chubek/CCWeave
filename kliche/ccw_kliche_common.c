/* Shared emit helper for Kliche stereotypes. */

#include "ccw_kliche_common.h"

ccw_node
ccw_kliche_emit (ccw_ir *ir, ccw_node blk, const char *opcode,
                 ccw_ir_type type, const char *dest,
                 const ccw_kliche_opnd *operands)
{
  ccw_node ins = ccw_ir_instr_build (ir, opcode, type);
  if (ins == 0)
    return 0;
  if (dest != NULL)
    ccw_ir_instr_set_dest (ir, ins, dest);

  for (const ccw_kliche_opnd *o = operands; o != NULL && o->kind != CCW_KO_END;
       o++)
    {
      ccw_node opnd = 0;
      switch (o->kind)
        {
        case CCW_KO_REG:
          opnd = ccw_ir_operand_reg (ir, o->name);
          break;
        case CCW_KO_FUNC:
          opnd = ccw_ir_operand_func (ir, o->name);
          break;
        case CCW_KO_BLOCK:
          opnd = ccw_ir_operand_block (ir, o->name);
          break;
        case CCW_KO_INT:
          opnd = ccw_ir_operand_const_int (ir, CCW_TY_I64, o->value);
          break;
        case CCW_KO_END:
          break;
        }
      if (opnd == 0 || ccw_ir_instr_add_operand (ir, ins, opnd) != CCW_OK)
        return 0;
    }
  if (blk != 0 && ccw_ir_block_append_instr (ir, blk, ins) != CCW_OK)
    return 0;
  return ins;
}
