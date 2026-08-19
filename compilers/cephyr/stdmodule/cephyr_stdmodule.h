/* Cephyr standard module bundle — §1.5, §8.5.
 *
 * Provides GNU C-style __attribute__ handlers, basic LTO fragment,
 * and other standard extensions. All operations are implemented via
 * Kernels; this module only contributes Sched fragments and attribute
 * hook registrations. */

#ifndef CEPHYR_STDMODULE_H
#define CEPHYR_STDMODULE_H

#include "../include/cephyr-module.h"

#ifdef __cplusplus
extern "C"
{
#endif

  /* The standard module bundle is a singleton. Returns the same pointer
   * on every call; the module is initialized on first use. */
  const cephyr_module *cephyr_stdmodule_bundle (void);

  /* Individual modules that make up the bundle (for testing). */
  const cephyr_module *cephyr_stdmodule_gnu_attributes (void);
  const cephyr_module *cephyr_stdmodule_lto (void);

#ifdef __cplusplus
}
#endif
#endif /* CEPHYR_STDMODULE_H */
