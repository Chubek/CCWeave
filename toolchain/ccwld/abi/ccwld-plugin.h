#ifndef CCWLD_PLUGIN_H
#define CCWLD_PLUGIN_H
#include <stdint.h>
#define CCWLD_PLUGIN_ABI_VERSION 1u
typedef enum {
  CCWLD_PHASE_RESOLVED=1, CCWLD_PHASE_GC=2,
  CCWLD_PHASE_LAYOUT=3, CCWLD_PHASE_EMIT=4
} ccwld_phase;
typedef struct ccwld_link ccwld_link;
typedef struct {
  uint32_t abi_version;
  const char *name;
  uint32_t phases;
  int (*init)(void *, const char *);
  int (*run)(void *, ccwld_phase, ccwld_link *);
  void (*fini)(void *);
  void *self;
} ccwld_plugin_vtable;
typedef const ccwld_plugin_vtable *(*ccwld_plugin_entry_fn)(void);
#endif
