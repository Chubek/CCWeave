/* badabi — a plugin whose vtable carries the wrong ABI version.
 * CCWld must reject it with exit 3 (CCWLD_EXIT_ABI), never skip it. */
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
    CCWLD_PLUGIN_ABI_VERSION + 100u, /* mismatched on purpose */
    "badabi",
    CCWLD_PHASE_BIT (CCWLD_PHASE_EMIT),
    NULL,
    bad_run,
    NULL,
    NULL,
  };
  return &vt;
}
