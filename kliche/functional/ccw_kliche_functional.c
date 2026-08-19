/* Functional stereotype: closures as records, application as an
 * indirect call through the closure's code pointer. */

#include "../ccw_kliche_common.h"

ccw_node
ccw_kliche_closure_alloc (ccw_ir *ir, ccw_node blk, const char *dest,
                          const char *code_symbol, int captured_count)
{
  ccw_kliche_opnd ops[]
      = { CCW_K_FUNC (code_symbol), CCW_K_INT (captured_count), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "closure.alloc", CCW_TY_PTR, dest, ops);
}

ccw_node
ccw_kliche_closure_capture (ccw_ir *ir, ccw_node blk, const char *closure_reg,
                            int slot, const char *value_reg)
{
  ccw_kliche_opnd ops[] = { CCW_K_REG (closure_reg), CCW_K_INT (slot),
                            CCW_K_REG (value_reg), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "closure.capture", CCW_TY_VOID, NULL, ops);
}

ccw_node
ccw_kliche_closure_ref (ccw_ir *ir, ccw_node blk, const char *dest,
                        const char *closure_reg, int slot)
{
  ccw_kliche_opnd ops[]
      = { CCW_K_REG (closure_reg), CCW_K_INT (slot), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "closure.ref", CCW_TY_I64, dest, ops);
}

ccw_node
ccw_kliche_closure_apply (ccw_ir *ir, ccw_node blk, const char *dest,
                          const char *closure_reg, const char *arg_reg)
{
  ccw_kliche_opnd ops[]
      = { CCW_K_REG (closure_reg), CCW_K_REG (arg_reg), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "call.indirect", CCW_TY_I64, dest, ops);
}
