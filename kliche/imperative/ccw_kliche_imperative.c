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

ccw_node ccw_kliche_int_const(ccw_ir *ir, ccw_node blk, const char *dest,
                              int64_t value)
{
    ccw_kliche_opnd ops[] = { CCW_K_INT(value), CCW_K_END };
    return ccw_kliche_emit(ir, blk, "iconst", CCW_TY_I64, dest, ops);
}

ccw_node ccw_kliche_unary(ccw_ir *ir, ccw_node blk, const char *opcode,
                          const char *dest, const char *operand_reg,
                          ccw_ir_type type)
{
    ccw_kliche_opnd ops[] = { CCW_K_REG(operand_reg), CCW_K_END };
    return ccw_kliche_emit(ir, blk, opcode, type, dest, ops);
}

ccw_node ccw_kliche_binary(ccw_ir *ir, ccw_node blk, const char *opcode,
                           const char *dest, const char *left_reg,
                           const char *right_reg, ccw_ir_type type)
{
    ccw_kliche_opnd ops[] = {
        CCW_K_REG(left_reg), CCW_K_REG(right_reg), CCW_K_END
    };
    return ccw_kliche_emit(ir, blk, opcode, type, dest, ops);
}

ccw_node ccw_kliche_call(ccw_ir *ir, ccw_node blk, const char *dest,
                         const char *callee, const char *const *arg_regs,
                         int arg_count, ccw_ir_type result_type)
{
    if (arg_count < 0) return 0;
    ccw_node ins = ccw_ir_instr_build(ir, "call", result_type);
    if (ins == 0) return 0;
    if (dest != NULL && ccw_ir_instr_set_dest(ir, ins, dest) != CCW_OK) return 0;

    ccw_node target = ccw_ir_operand_func(ir, callee);
    if (target == 0 || ccw_ir_instr_add_operand(ir, ins, target) != CCW_OK) return 0;
    for (int i = 0; i < arg_count; i++) {
        ccw_node arg = ccw_ir_operand_reg(ir, arg_regs[i]);
        if (arg == 0 || ccw_ir_instr_add_operand(ir, ins, arg) != CCW_OK) return 0;
    }
    if (blk != 0 && ccw_ir_block_append_instr(ir, blk, ins) != CCW_OK) return 0;
    return ins;
}

ccw_node ccw_kliche_return(ccw_ir *ir, ccw_node blk, const char *value_reg)
{
    ccw_kliche_opnd ops[] = { CCW_K_REG(value_reg), CCW_K_END };
    if (value_reg == NULL) {
        ccw_kliche_opnd no_ops[] = { CCW_K_END };
        return ccw_kliche_emit(ir, blk, "ret", CCW_TY_VOID, NULL, no_ops);
    }
    return ccw_kliche_emit(ir, blk, "ret", CCW_TY_VOID, NULL, ops);
}
