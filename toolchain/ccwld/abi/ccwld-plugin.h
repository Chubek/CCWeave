#ifndef CCWLD_PLUGIN_H
#define CCWLD_PLUGIN_H
#include <stdint.h>
#define CCWLD_PLUGIN_ABI_VERSION 1u

/* Phase identifiers — shared with plan/ccwld_plan.h (§3).
 * Keep in sync with ccwld_phase_id in plan/ccwld_plan.h. */
typedef enum
{
  CCWLD_PHASE_LOAD = 0,
  CCWLD_PHASE_RESOLVE = 1,
  CCWLD_PHASE_LTO = 2,
  CCWLD_PHASE_GC = 3,
  CCWLD_PHASE_LAYOUT = 4,
  CCWLD_PHASE_RELOCATE = 5,
  CCWLD_PHASE_EMIT = 6,
} ccwld_phase;

/* Compatibility alias */
#define CCWLD_PHASE_RESOLVED CCWLD_PHASE_RESOLVE

typedef struct ccwld_link ccwld_link;
typedef struct
{
  uint32_t abi_version;
  const char *name;
  int (*init) (void *, const char *);
  int (*run) (void *, ccwld_phase, ccwld_link *);
  void (*fini) (void *);
  void *self;
} ccwld_plugin_vtable;
typedef const ccwld_plugin_vtable *(*ccwld_plugin_entry_fn) (void);
#endif
