/* Imperative stereotype: mutable locals as slots, structured control
 * flow lowered to branches. */

#include "../ccw_kliche_common.h"

ccw_node ccw_kliche_local_alloc(ccw_ir *ir, ccw_node blk, const char *dest,
                                ccw_ir_type type)
{
    ccw_kliche_opnd ops[] = { CCW_K_INT((int64_t)type), CCW_K_END };
    return ccw_kliche_emit(ir, blk, "local.alloc", CCW_TY_PTR, dest, ops);
}

ccw_node ccw_kliche_local_store(ccw_ir *ir, ccw_node blk,
                                const char *slot_reg, const char *value_reg)
{
    ccw_kliche_opnd ops[] = { CCW_K_REG(slot_reg), CCW_K_REG(value_reg), CCW_K_END };
    return ccw_kliche_emit(ir, blk, "local.store", CCW_TY_VOID, NULL, ops);
}

ccw_node ccw_kliche_local_load(ccw_ir *ir, ccw_node blk, const char *dest,
                               const char *slot_reg, ccw_ir_type type)
{
    ccw_kliche_opnd ops[] = { CCW_K_REG(slot_reg), CCW_K_END };
    return ccw_kliche_emit(ir, blk, "local.load", type, dest, ops);
}

ccw_node ccw_kliche_branch_if(ccw_ir *ir, ccw_node blk, const char *cond_reg,
                              const char *then_block, const char *else_block)
{
    ccw_kliche_opnd ops[] = { CCW_K_REG(cond_reg), CCW_K_BLOCK(then_block),
                              CCW_K_BLOCK(else_block), CCW_K_END };
    return ccw_kliche_emit(ir, blk, "br.cond", CCW_TY_VOID, NULL, ops);
}

ccw_node ccw_kliche_jump(ccw_ir *ir, ccw_node blk, const char *target_block)
{
    ccw_kliche_opnd ops[] = { CCW_K_BLOCK(target_block), CCW_K_END };
    return ccw_kliche_emit(ir, blk, "br", CCW_TY_VOID, NULL, ops);
}
