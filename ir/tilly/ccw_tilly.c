/* Tilly profile constructs and validation (§5.3). */

#include "ccw_tilly.h"
#include "../ccw_ir_internal.h"
#include "../on1x/ccw_on1x.h"

#include <stdio.h>
#include <string.h>

bool
ccw_tilly_is_profile_opcode (const char *opcode)
{
  if (opcode == NULL)
    return false;
  return strcmp (opcode, CCW_TILLY_OP_CALL_STATIC) == 0
         || strcmp (opcode, CCW_TILLY_OP_RELOC) == 0;
}

bool
ccw_tilly_is_profile_attr (const char *key)
{
  if (key == NULL)
    return false;
  return strcmp (key, CCW_TILLY_ATTR_LINK_SECTION) == 0
         || strcmp (key, CCW_TILLY_ATTR_LAYOUT) == 0;
}

ccw_node
ccw_tilly_build_call_static (ccw_ir *ir, ccw_node blk, const char *dest,
                             ccw_ir_type type, const char *callee)
{
  ccw_node ins = ccw_ir_instr_build (ir, CCW_TILLY_OP_CALL_STATIC, type);
  if (ins == 0)
    return 0;
  if (dest != NULL)
    ccw_ir_instr_set_dest (ir, ins, dest);
  ccw_node target = ccw_ir_operand_func (ir, callee);
  if (target == 0 || ccw_ir_instr_add_operand (ir, ins, target) != CCW_OK)
    return 0;
  if (blk != 0 && ccw_ir_block_append_instr (ir, blk, ins) != CCW_OK)
    return 0;
  return ins;
}

ccw_node
ccw_tilly_build_reloc (ccw_ir *ir, ccw_node blk, const char *dest,
                       const char *symbol, int64_t addend)
{
  ccw_node ins = ccw_ir_instr_build (ir, CCW_TILLY_OP_RELOC, CCW_TY_PTR);
  if (ins == 0)
    return 0;
  if (dest != NULL)
    ccw_ir_instr_set_dest (ir, ins, dest);
  ccw_node sym = ccw_ir_operand_func (ir, symbol);
  ccw_node add = ccw_ir_operand_const_int (ir, CCW_TY_I64, addend);
  if (sym == 0 || add == 0)
    return 0;
  ccw_ir_instr_add_operand (ir, ins, sym);
  ccw_ir_instr_add_operand (ir, ins, add);
  if (blk != 0 && ccw_ir_block_append_instr (ir, blk, ins) != CCW_OK)
    return 0;
  return ins;
}

ccw_node
ccw_tilly_build_syscall (ccw_ir *ir, ccw_node blk, const char *dest,
                         ccw_ir_type type, int64_t number,
                         const ccw_node *args, size_t arg_count)
{
  return ccw_ir_build_syscall (ir, blk, dest, type, number, args, arg_count);
}

ccw_node
ccw_tilly_build_io_read (ccw_ir *ir, ccw_node blk, const char *dest,
                         ccw_node fd, ccw_node buffer, ccw_node count)
{
  return ccw_ir_build_io_read (ir, blk, dest, fd, buffer, count);
}

ccw_node
ccw_tilly_build_io_write (ccw_ir *ir, ccw_node blk, const char *dest,
                          ccw_node fd, ccw_node buffer, ccw_node count)
{
  return ccw_ir_build_io_write (ir, blk, dest, fd, buffer, count);
}

ccw_node
ccw_tilly_build_io_close (ccw_ir *ir, ccw_node blk, const char *dest,
                          ccw_node fd)
{
  return ccw_ir_build_io_close (ir, blk, dest, fd);
}

ccw_node
ccw_tilly_build_io_open (ccw_ir *ir, ccw_node blk, const char *dest,
                         ccw_node path, ccw_node flags, ccw_node mode)
{
  return ccw_ir_build_io_open (ir, blk, dest, path, flags, mode);
}

ccw_status
ccw_tilly_set_link_section (ccw_ir *ir, ccw_node fn, const char *section)
{
  if (ccw_ir_module_profile (ir) != CCW_PROFILE_TILLY)
    return CCW_ERR_TYPE;
  return ccw_ir_attr_set (ir, fn, CCW_TILLY_ATTR_LINK_SECTION, section);
}

ccw_status
ccw_tilly_set_layout (ccw_ir *ir, const char *layout)
{
  if (ccw_ir_module_profile (ir) != CCW_PROFILE_TILLY)
    return CCW_ERR_TYPE;
  return ccw_ir_attr_set (ir, 0, CCW_TILLY_ATTR_LAYOUT, layout);
}

const char *
ccw_tilly_reject_reason (const ccw_ir *ir, ccw_node ins)
{
  const char *opcode = ccw_ir_instr_opcode (ir, ins);
  if (ccw_on1x_is_profile_opcode (opcode))
    return "dynamic-execution construct is not permitted in a Tilly module";
  int n = ccw_ir_attr_count (ir, ins);
  for (int i = 0; i < n; i++)
    if (ccw_on1x_is_profile_attr (ccw_ir_attr_key (ir, ins, i)))
      return "dynamic-dispatch metadata is not permitted in a Tilly module";
  return NULL;
}
