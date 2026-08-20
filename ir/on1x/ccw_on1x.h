/* On1x profile (§5.3, dynamic execution): dynamic dispatch sites,
 * inline-cache slots, deoptimization metadata, safepoints. */

#ifndef CCW_ON1X_H
#define CCW_ON1X_H

#include "../ccw_ir.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* On1x-only opcodes. */
#define CCW_ON1X_OP_CALL_DYNAMIC "call.dynamic"
#define CCW_ON1X_OP_SAFEPOINT "safepoint"
#define CCW_ON1X_OP_DEOPT "deopt"

/* On1x-only attribute keys. */
#define CCW_ON1X_ATTR_INLINE_CACHE "dispatch.inline-cache"
#define CCW_ON1X_ATTR_DEOPT_TARGET "deopt.target"

  bool ccw_on1x_is_profile_opcode (const char *opcode);
  bool ccw_on1x_is_profile_attr (const char *key);

  ccw_node ccw_on1x_build_call_dynamic (ccw_ir *ir, ccw_node blk,
                                        const char *dest, ccw_ir_type type,
                                        const char *receiver,
                                        const char *selector, int cache_slots);
  ccw_node ccw_on1x_build_safepoint (ccw_ir *ir, ccw_node blk);
  ccw_node ccw_on1x_build_deopt (ccw_ir *ir, ccw_node blk, const char *target);
  ccw_node ccw_on1x_build_syscall (ccw_ir *ir, ccw_node blk, const char *dest,
                                   ccw_ir_type type, int64_t number,
                                   const ccw_node *args, size_t arg_count);
  ccw_node ccw_on1x_build_io_read (ccw_ir *ir, ccw_node blk, const char *dest,
                                   ccw_node fd, ccw_node buffer,
                                   ccw_node count);
  ccw_node ccw_on1x_build_io_write (ccw_ir *ir, ccw_node blk, const char *dest,
                                    ccw_node fd, ccw_node buffer,
                                    ccw_node count);
  ccw_node ccw_on1x_build_io_close (ccw_ir *ir, ccw_node blk, const char *dest,
                                    ccw_node fd);
  ccw_node ccw_on1x_build_io_open (ccw_ir *ir, ccw_node blk, const char *dest,
                                   ccw_node path, ccw_node flags,
                                   ccw_node mode);

  /* NULL if `ins` is legal in an On1x module, else the reason. */
  const char *ccw_on1x_reject_reason (const ccw_ir *ir, ccw_node ins);

#ifdef __cplusplus
}
#endif
#endif /* CCW_ON1X_H */
