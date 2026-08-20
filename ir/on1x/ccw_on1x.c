/* On1x profile constructs and validation (§5.3). */

#include "ccw_on1x.h"
#include "../ccw_ir_internal.h"
#include "../tilly/ccw_tilly.h"

#include <stdio.h>
#include <string.h>

bool
ccw_on1x_is_profile_opcode (const char *opcode)
{
  if (opcode == NULL)
    return false;
  return strcmp (opcode, CCW_ON1X_OP_CALL_DYNAMIC) == 0
         || strcmp (opcode, CCW_ON1X_OP_SAFEPOINT) == 0
         || strcmp (opcode, CCW_ON1X_OP_DEOPT) == 0;
}

bool
ccw_on1x_is_profile_attr (const char *key)
{
  if (key == NULL)
    return false;
  return strcmp (key, CCW_ON1X_ATTR_INLINE_CACHE) == 0
         || strcmp (key, CCW_ON1X_ATTR_DEOPT_TARGET) == 0;
}

ccw_node
ccw_on1x_build_call_dynamic (ccw_ir *ir, ccw_node blk, const char *dest,
                             ccw_ir_type type, const char *receiver,
                             const char *selector, int cache_slots)
{
  ccw_node ins = ccw_ir_instr_build (ir, CCW_ON1X_OP_CALL_DYNAMIC, type);
  if (ins == 0)
    return 0;
  if (dest != NULL)
    ccw_ir_instr_set_dest (ir, ins, dest);
  ccw_node recv = ccw_ir_operand_reg (ir, receiver);
  ccw_node sel = ccw_ir_operand_func (ir, selector);
  if (recv == 0 || sel == 0)
    return 0;
  ccw_ir_instr_add_operand (ir, ins, recv);
  ccw_ir_instr_add_operand (ir, ins, sel);
  char slots[32];
  snprintf (slots, sizeof (slots), "%d", cache_slots);
  ccw_ir_attr_set (ir, ins, CCW_ON1X_ATTR_INLINE_CACHE, slots);
  if (blk != 0 && ccw_ir_block_append_instr (ir, blk, ins) != CCW_OK)
    return 0;
  return ins;
}

ccw_node
ccw_on1x_build_safepoint (ccw_ir *ir, ccw_node blk)
{
  ccw_node ins = ccw_ir_instr_build (ir, CCW_ON1X_OP_SAFEPOINT, CCW_TY_VOID);
  if (ins == 0)
    return 0;
  if (blk != 0 && ccw_ir_block_append_instr (ir, blk, ins) != CCW_OK)
    return 0;
  return ins;
}

ccw_node
ccw_on1x_build_deopt (ccw_ir *ir, ccw_node blk, const char *target)
{
  ccw_node ins = ccw_ir_instr_build (ir, CCW_ON1X_OP_DEOPT, CCW_TY_VOID);
  if (ins == 0)
    return 0;
  ccw_node t = ccw_ir_operand_block (ir, target);
  if (t == 0)
    return 0;
  ccw_ir_instr_add_operand (ir, ins, t);
  ccw_ir_attr_set (ir, ins, CCW_ON1X_ATTR_DEOPT_TARGET, target);
  if (blk != 0 && ccw_ir_block_append_instr (ir, blk, ins) != CCW_OK)
    return 0;
  return ins;
}

ccw_node
ccw_on1x_build_syscall (ccw_ir *ir, ccw_node blk, const char *dest,
                        ccw_ir_type type, int64_t number, const ccw_node *args,
                        size_t arg_count)
{
  return ccw_ir_build_syscall (ir, blk, dest, type, number, args, arg_count);
}

ccw_node
ccw_on1x_build_io_read (ccw_ir *ir, ccw_node blk, const char *dest,
                        ccw_node fd, ccw_node buffer, ccw_node count)
{
  return ccw_ir_build_io_read (ir, blk, dest, fd, buffer, count);
}

ccw_node
ccw_on1x_build_io_write (ccw_ir *ir, ccw_node blk, const char *dest,
                         ccw_node fd, ccw_node buffer, ccw_node count)
{
  return ccw_ir_build_io_write (ir, blk, dest, fd, buffer, count);
}

ccw_node
ccw_on1x_build_io_close (ccw_ir *ir, ccw_node blk, const char *dest,
                         ccw_node fd)
{
  return ccw_ir_build_io_close (ir, blk, dest, fd);
}

ccw_node
ccw_on1x_build_io_open (ccw_ir *ir, ccw_node blk, const char *dest,
                        ccw_node path, ccw_node flags, ccw_node mode)
{
  return ccw_ir_build_io_open (ir, blk, dest, path, flags, mode);
}

const char *
ccw_on1x_reject_reason (const ccw_ir *ir, ccw_node ins)
{
  const char *opcode = ccw_ir_instr_opcode (ir, ins);
  if (ccw_tilly_is_profile_opcode (opcode))
    return "ahead-of-time link construct is not permitted in an On1x module";
  int n = ccw_ir_attr_count (ir, ins);
  for (int i = 0; i < n; i++)
    if (ccw_tilly_is_profile_attr (ccw_ir_attr_key (ir, ins, i)))
      return "link/layout metadata is not permitted in an On1x module";
  return NULL;
}
