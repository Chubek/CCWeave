/* Tilly profile (§5.3, ahead-of-time): static call and relocation
 * constructs, link-section attributes, whole-module layout directives.
 * Forbids dynamic-dispatch metadata. */

#ifndef CCW_TILLY_H
#define CCW_TILLY_H

#include "../ccw_ir.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Tilly-only opcodes. */
#define CCW_TILLY_OP_CALL_STATIC "call.static"
#define CCW_TILLY_OP_RELOC "reloc"

/* Tilly-only attribute keys. */
#define CCW_TILLY_ATTR_LINK_SECTION "link.section"
#define CCW_TILLY_ATTR_LAYOUT "module.layout"

  bool ccw_tilly_is_profile_opcode (const char *opcode);
  bool ccw_tilly_is_profile_attr (const char *key);

  /* Construction helpers (append to blk). */
  ccw_node ccw_tilly_build_call_static (ccw_ir *ir, ccw_node blk,
                                        const char *dest, ccw_ir_type type,
                                        const char *callee);
  ccw_node ccw_tilly_build_reloc (ccw_ir *ir, ccw_node blk, const char *dest,
                                  const char *symbol, int64_t addend);
  ccw_node ccw_tilly_build_syscall (ccw_ir *ir, ccw_node blk, const char *dest,
                                    ccw_ir_type type, int64_t number,
                                    const ccw_node *args, size_t arg_count);
  ccw_node ccw_tilly_build_io_read (ccw_ir *ir, ccw_node blk, const char *dest,
                                    ccw_node fd, ccw_node buffer,
                                    ccw_node count);
  ccw_node ccw_tilly_build_io_write (ccw_ir *ir, ccw_node blk,
                                     const char *dest, ccw_node fd,
                                     ccw_node buffer, ccw_node count);
  ccw_node ccw_tilly_build_io_close (ccw_ir *ir, ccw_node blk,
                                     const char *dest, ccw_node fd);
  ccw_node ccw_tilly_build_io_open (ccw_ir *ir, ccw_node blk, const char *dest,
                                    ccw_node path, ccw_node flags,
                                    ccw_node mode);
  ccw_status ccw_tilly_set_link_section (ccw_ir *ir, ccw_node fn,
                                         const char *section);
  ccw_status ccw_tilly_set_layout (ccw_ir *ir, const char *layout);

  /* NULL if `ins` is legal in a Tilly module, else the reason. */
  const char *ccw_tilly_reject_reason (const ccw_ir *ir, ccw_node ins);

#ifdef __cplusplus
}
#endif
#endif /* CCW_TILLY_H */
