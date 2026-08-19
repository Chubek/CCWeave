/* OOP stereotype: object headers, vtable layout and dispatch, and
 * exception frames. Profile-agnostic: an On1x host may refine
 * vtable.dispatch into a dynamic dispatch site with inline caches. */

#include "../ccw_kliche_common.h"

ccw_node
ccw_kliche_object_alloc (ccw_ir *ir, ccw_node blk, const char *dest,
                         const char *class_symbol, int field_count)
{
  ccw_kliche_opnd ops[]
      = { CCW_K_FUNC (class_symbol), CCW_K_INT (field_count), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "object.alloc", CCW_TY_PTR, dest, ops);
}

ccw_node
ccw_kliche_vtable_load (ccw_ir *ir, ccw_node blk, const char *dest,
                        const char *object_reg)
{
  ccw_kliche_opnd ops[] = { CCW_K_REG (object_reg), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "vtable.load", CCW_TY_PTR, dest, ops);
}

ccw_node
ccw_kliche_vtable_dispatch (ccw_ir *ir, ccw_node blk, const char *dest,
                            const char *vtable_reg, int slot,
                            const char *receiver_reg)
{
  ccw_kliche_opnd ops[] = { CCW_K_REG (vtable_reg), CCW_K_INT (slot),
                            CCW_K_REG (receiver_reg), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "vtable.dispatch", CCW_TY_I64, dest, ops);
}

ccw_node
ccw_kliche_frame_push (ccw_ir *ir, ccw_node blk, const char *handler_block)
{
  ccw_kliche_opnd ops[] = { CCW_K_BLOCK (handler_block), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "frame.push", CCW_TY_VOID, NULL, ops);
}

ccw_node
ccw_kliche_frame_pop (ccw_ir *ir, ccw_node blk)
{
  ccw_kliche_opnd ops[] = { CCW_K_END };
  return ccw_kliche_emit (ir, blk, "frame.pop", CCW_TY_VOID, NULL, ops);
}
