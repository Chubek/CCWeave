/* Shared instruction-building helper for the Kliche stereotypes.
 * Every stereotype emits only core-IR constructs (§6.1).
 * Validation: all inputs are checked; null ir/blk return 0 immediately. */

#ifndef CCW_KLICHE_COMMON_H
#define CCW_KLICHE_COMMON_H

#include "ccw_kliche.h"

typedef enum
{
  CCW_KO_END = 0,
  CCW_KO_REG,
  CCW_KO_FUNC,
  CCW_KO_BLOCK,
  CCW_KO_INT,
  CCW_KO_FLOAT,
  CCW_KO_STR
} ccw_kliche_opnd_kind;

typedef struct
{
  ccw_kliche_opnd_kind kind;
  const char *name;
  int64_t value;
  double fvalue;
} ccw_kliche_opnd;

#define CCW_K_REG(s) ((ccw_kliche_opnd){ CCW_KO_REG, (s), 0, 0.0 })
#define CCW_K_FUNC(s) ((ccw_kliche_opnd){ CCW_KO_FUNC, (s), 0, 0.0 })
#define CCW_K_BLOCK(s) ((ccw_kliche_opnd){ CCW_KO_BLOCK, (s), 0, 0.0 })
#define CCW_K_INT(v) ((ccw_kliche_opnd){ CCW_KO_INT, NULL, (v), 0.0 })
#define CCW_K_FLOAT(v) ((ccw_kliche_opnd){ CCW_KO_FLOAT, NULL, 0, (v) })
#define CCW_K_STR(s) ((ccw_kliche_opnd){ CCW_KO_STR, (s), 0, 0.0 })
#define CCW_K_END ((ccw_kliche_opnd){ CCW_KO_END, NULL, 0, 0.0 })

/* Single-instruction emit with full validation (§6.1).
 * Returns 0 on any failure (null ir, null blk, bad operand, etc.). */
ccw_node ccw_kliche_emit (ccw_ir *ir, ccw_node blk, const char *opcode,
                          ccw_ir_type type, const char *dest,
                          const ccw_kliche_opnd *operands);

/* Multi-instruction emit: builds detached instructions from a sequence of
 * triples (opcode, type, dest, operand-count, operands...) and returns the
 * last instruction, or 0 on failure.  The block is not modified; the caller
 * must append instructions to the block.  This is useful for building
 * compound stereotype patterns that lower to several core-IR instructions. */
typedef struct
{
  const char *opcode;
  ccw_ir_type type;
  const char *dest;
  const ccw_kliche_opnd *operands;
} ccw_kliche_emit_spec;

ccw_node ccw_kliche_emit_multi (ccw_ir *ir, ccw_node blk,
                                const ccw_kliche_emit_spec *specs,
                                int count);

/* ---------- helpers for building common patterns ---------- */

/* Build a binary op and return the instruction node.  Convenience over emit. */
ccw_node ccw_kliche_binop (ccw_ir *ir, ccw_node blk, const char *opcode,
                           const char *dest, const char *lhs,
                           const char *rhs, ccw_ir_type type);

/* Build a comparison (eq, ne, lt, le, gt, ge) returning i1. */
ccw_node ccw_kliche_cmp (ccw_ir *ir, ccw_node blk, const char *pred,
                         const char *dest, const char *lhs, const char *rhs,
                         ccw_ir_type type);

/* Build an alloca for a given byte count; returns pointer in dest. */
ccw_node ccw_kliche_alloca (ccw_ir *ir, ccw_node blk, const char *dest,
                            int64_t byte_count);

/* Build a gep (get-element-pointer) for pointer arithmetic.
 * base_reg is a pointer; offset is added. */
ccw_node ccw_kliche_gep (ccw_ir *ir, ccw_node blk, const char *dest,
                         const char *base_reg, int64_t offset);

/* Build a pointer-sized load from src_reg into dest. */
ccw_node ccw_kliche_load (ccw_ir *ir, ccw_node blk, const char *dest,
                          const char *src_reg, ccw_ir_type type);

/* Build a pointer-sized store of value_reg into dst_reg. */
ccw_node ccw_kliche_store (ccw_ir *ir, ccw_node blk, const char *dst_reg,
                           const char *value_reg, ccw_ir_type type);

#endif /* CCW_KLICHE_COMMON_H */
