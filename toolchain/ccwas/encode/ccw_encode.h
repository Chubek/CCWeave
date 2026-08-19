#ifndef CCWAS_ENCODE_H
#define CCWAS_ENCODE_H
/* §8: per-target instruction encoders */

#include "ccw_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

  /* Encode a single instruction statement into the unit's current section.
   * Returns number of bytes emitted, or 0 on error (error string set). */
  int ccw_encode_insn (ccw_unit_t *u, const ccw_insn_stmt_t *insn,
                       char **error);

  /* Look up canonical register number for a given register name.
   * Returns -1 if not found. */
  int ccw_encode_regno (ccw_arch_t arch, const char *name);

  /* Get the canonical register name for a register number. */
  const char *ccw_encode_regname (ccw_arch_t arch, int regno);

#ifdef __cplusplus
}
#endif

#endif /* CCWAS_ENCODE_H */
