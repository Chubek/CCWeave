/* Functional stereotype (§6.1): closures as heap-allocated records,
 * algebraic data types (tagged unions), pattern-matching dispatch,
 * and partial application.  All constructs lower to core-IR instructions
 * only; no profile-specific constructs are emitted. */

#include "../ccw_kliche_common.h"

/* ---------- closures ---------- */

/* §6.1 functional: closure.alloc builds a heap-allocated record holding
 * a code pointer and captured_count captured values.  The layout is:
 *   [code_ptr][capture_0]...[capture_{n-1}]
 * Each slot is pointer-sized. */

ccw_node
ccw_kliche_closure_alloc (ccw_ir *ir, ccw_node blk, const char *dest,
                          const char *code_symbol, int captured_count)
{
  if (ir == NULL || blk == 0 || dest == NULL || code_symbol == NULL
      || captured_count < 0)
    return 0;

  ccw_kliche_opnd ops[]
      = { CCW_K_FUNC (code_symbol), CCW_K_INT (captured_count), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "closure.alloc", CCW_TY_PTR, dest, ops);
}

/* §6.1 functional: store a captured value into the closure at slot index.
 * The closure record is indexed: slot 0 = first capture, etc. */

ccw_node
ccw_kliche_closure_capture (ccw_ir *ir, ccw_node blk, const char *closure_reg,
                            int slot, const char *value_reg)
{
  if (ir == NULL || blk == 0 || closure_reg == NULL || value_reg == NULL
      || slot < 0)
    return 0;

  ccw_kliche_opnd ops[] = { CCW_K_REG (closure_reg), CCW_K_INT (slot),
                            CCW_K_REG (value_reg), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "closure.capture", CCW_TY_VOID, NULL, ops);
}

/* §6.1 functional: read a captured value from the closure. */

ccw_node
ccw_kliche_closure_ref (ccw_ir *ir, ccw_node blk, const char *dest,
                        const char *closure_reg, int slot)
{
  if (ir == NULL || blk == 0 || dest == NULL || closure_reg == NULL
      || slot < 0)
    return 0;

  ccw_kliche_opnd ops[]
      = { CCW_K_REG (closure_reg), CCW_K_INT (slot), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "closure.ref", CCW_TY_I64, dest, ops);
}

/* §6.1 functional: apply a closure by extracting the code pointer and
 * performing an indirect call with the receiver and argument. */

ccw_node
ccw_kliche_closure_apply (ccw_ir *ir, ccw_node blk, const char *dest,
                          const char *closure_reg, const char *arg_reg)
{
  if (ir == NULL || blk == 0 || closure_reg == NULL)
    return 0;

  ccw_kliche_opnd ops[]
      = { CCW_K_REG (closure_reg), CCW_K_REG (arg_reg), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "call.indirect", CCW_TY_I64, dest, ops);
}

/* ---------- algebraic data types (tagged unions) ---------- */

/* §6.1 functional: allocate a tagged record.  The layout is:
 *   [tag: i64][field_0]...[field_{n-1}]
 * The tag is an integer discriminator; field_count is the number of
 * value-carrying fields beyond the tag. */

ccw_node
ccw_kliche_record_alloc (ccw_ir *ir, ccw_node blk, const char *dest,
                         int64_t tag, int field_count)
{
  if (ir == NULL || blk == 0 || dest == NULL || field_count < 0)
    return 0;

  ccw_kliche_opnd ops[]
      = { CCW_K_INT (tag), CCW_K_INT (field_count), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "record.alloc", CCW_TY_PTR, dest, ops);
}

/* §6.1 functional: read the tag of a tagged record. */

ccw_node
ccw_kliche_record_tag (ccw_ir *ir, ccw_node blk, const char *dest,
                       const char *record_reg)
{
  if (ir == NULL || blk == 0 || dest == NULL || record_reg == NULL)
    return 0;

  ccw_kliche_opnd ops[] = { CCW_K_REG (record_reg), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "record.tag", CCW_TY_I64, dest, ops);
}

/* §6.1 functional: read a data field from a tagged record.
 * field_idx is 0-based, counting data fields only (tag excluded). */

ccw_node
ccw_kliche_record_field_get (ccw_ir *ir, ccw_node blk, const char *dest,
                             const char *record_reg, int field_idx,
                             ccw_ir_type type)
{
  if (ir == NULL || blk == 0 || dest == NULL || record_reg == NULL
      || field_idx < 0)
    return 0;

  ccw_kliche_opnd ops[]
      = { CCW_K_REG (record_reg), CCW_K_INT (field_idx), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "record.field.get", type, dest, ops);
}

/* §6.1 functional: write a data field into a tagged record. */

ccw_node
ccw_kliche_record_field_set (ccw_ir *ir, ccw_node blk, const char *record_reg,
                             int field_idx, const char *value_reg)
{
  if (ir == NULL || blk == 0 || record_reg == NULL || value_reg == NULL
      || field_idx < 0)
    return 0;

  ccw_kliche_opnd ops[] = { CCW_K_REG (record_reg), CCW_K_INT (field_idx),
                            CCW_K_REG (value_reg), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "record.field.set", CCW_TY_VOID, NULL,
                          ops);
}

/* ---------- pattern matching dispatch ---------- */

/* §6.1 functional: multi-way branch on a tagged record's discriminator.
 * tag_reg holds the extracted tag value.  case_tags is a 0-terminated
 * array of tag values; case_blocks is the corresponding array of block
 * names.  default_block is the fallthrough (e.g. for wildcard patterns).
 *
 * This emits a single switch instruction with (tag, default, case*) operands. */

ccw_node
ccw_kliche_tag_switch (ccw_ir *ir, ccw_node blk, const char *tag_reg,
                       const int64_t *case_tags,
                       const char *const *case_blocks, int case_count,
                       const char *default_block)
{
  if (ir == NULL || blk == 0 || tag_reg == NULL || case_tags == NULL
      || case_blocks == NULL || case_count < 0 || default_block == NULL)
    return 0;

  /* Emit a single switch instruction: operands are (tag, default_block,
   * case_val_0, case_block_0, case_val_1, case_block_1, ...). */
  ccw_node ins = ccw_ir_instr_build (ir, "switch", CCW_TY_VOID);
  if (ins == 0)
    return 0;

  ccw_node tag_op = ccw_ir_operand_reg (ir, tag_reg);
  if (tag_op == 0 || ccw_ir_instr_add_operand (ir, ins, tag_op) != CCW_OK)
    return 0;

  ccw_node def_op = ccw_ir_operand_block (ir, default_block);
  if (def_op == 0 || ccw_ir_instr_add_operand (ir, ins, def_op) != CCW_OK)
    return 0;

  for (int i = 0; i < case_count; i++)
    {
      if (case_blocks[i] == NULL)
        return 0;
      ccw_node val = ccw_ir_operand_const_int (ir, CCW_TY_I64, case_tags[i]);
      if (val == 0
          || ccw_ir_instr_add_operand (ir, ins, val) != CCW_OK)
        return 0;
      ccw_node blk_op = ccw_ir_operand_block (ir, case_blocks[i]);
      if (blk_op == 0
          || ccw_ir_instr_add_operand (ir, ins, blk_op) != CCW_OK)
        return 0;
    }

  if (ccw_ir_block_append_instr (ir, blk, ins) != CCW_OK)
    return 0;
  return ins;
}
