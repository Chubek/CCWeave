/* §6 emission: LIEF-driven when built with CCWEAVE_ENABLE_LIEF, with a
 * self-contained ELF64 writer as the always-available path; Binaryen
 * for wasm when built with CCWEAVE_ENABLE_BINARYEN.  The producer note
 * (§8) is attached by every emitter. */
#ifndef CCWLD_EMIT_H
#define CCWLD_EMIT_H

#include "../ccwld.h"
#include "../phases/ccwld_phases.h"

#ifdef __cplusplus
extern "C"
{
#endif

  /* Emit the laid-out link as `path` in the plan's format/kind. */
  int ccwld_emit_object (ccwld_state *st, const char *path, ccwld_error *e);

  /* Producer-note payload (§8): deterministic key=value lines.  The
   * state must have completed layout; returns bytes written. */
  size_t ccwld_emit_note_text (ccwld_state *st, char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif
#endif /* CCWLD_EMIT_H */
