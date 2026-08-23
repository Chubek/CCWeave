/* badphases — a plugin requesting a pipeline-internal phase
 * (RELOCATE, value 6) in its `phases` mask.  The spec'd bitmask space
 * covers only RESOLVED|GC|LAYOUT|EMIT (bits 0x1..0x8); CCWld rejects
 * the vtable with exit 3. */
#include "../../abi/ccwld-plugin.h"

#include <stddef.h>

static int
bad_run (void *self, ccwld_phase phase, ccwld_link *lk)
{
  (void)self;
  (void)phase;
  (void)lk;
  return 0;
}

static const ccwld_plugin_vtable *
ccwld_plugin_entry (void)
{
  static const ccwld_plugin_vtable vt = {
    CCWLD_PLUGIN_ABI_VERSION,
    "badphases",
    CCWLD_PHASE_ALL_SPEC_BITS | ((uint32_t)1u << 5u), /* internal bit */
    NULL,
    bad_run,
    NULL,
    NULL,
  };
  return &vt;
}
