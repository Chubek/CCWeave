/* Shared instruction-building helper for the Kliche stereotypes.
 * Every stereotype emits only core-IR constructs (§6.1). */

#ifndef CCW_KLICHE_COMMON_H
#define CCW_KLICHE_COMMON_H

#include "ccw_kliche.h"

typedef enum {
    CCW_KO_END = 0,
    CCW_KO_REG,
    CCW_KO_FUNC,
    CCW_KO_BLOCK,
    CCW_KO_INT
} ccw_kliche_opnd_kind;

typedef struct {
    ccw_kliche_opnd_kind kind;
    const char          *name;
    int64_t              value;
} ccw_kliche_opnd;

#define CCW_K_REG(s)   ((ccw_kliche_opnd){ CCW_KO_REG,   (s),  0 })
#define CCW_K_FUNC(s)  ((ccw_kliche_opnd){ CCW_KO_FUNC,  (s),  0 })
#define CCW_K_BLOCK(s) ((ccw_kliche_opnd){ CCW_KO_BLOCK, (s),  0 })
#define CCW_K_INT(v)   ((ccw_kliche_opnd){ CCW_KO_INT,   NULL, (v) })
#define CCW_K_END      ((ccw_kliche_opnd){ CCW_KO_END,   NULL, 0 })

ccw_node ccw_kliche_emit(ccw_ir *ir, ccw_node blk, const char *opcode,
                         ccw_ir_type type, const char *dest,
                         const ccw_kliche_opnd *operands);

#endif /* CCW_KLICHE_COMMON_H */
