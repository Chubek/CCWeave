#ifndef CCWLD_PLUGIN_H
#define CCWLD_PLUGIN_H
#include <stddef.h>
#include <stdint.h>
#define CCWLD_PLUGIN_ABI_VERSION 1u

/* Phase identifiers (§3/§5).
 *
 * The four spec'd plugin/hook phases keep their normative values
 * (RESOLVED=1, GC=2, LAYOUT=3, EMIT=4) so the `phases` bitmask layout
 * (1u << (phase-1)) matches the ABI contract exactly: a vtable asking
 * for RESOLVED|EMIT sets bits 0x1|0x8.  LOAD, LTO, and RELOCATE are
 * pipeline-internal identifiers outside the bitmask space — plugins
 * may not request them, and CCWld rejects vtables that try. */
typedef enum
{
  CCWLD_PHASE_LOAD = 0,    /* internal: never in a phases mask */
  CCWLD_PHASE_RESOLVED = 1,
  CCWLD_PHASE_RESOLVE = 1, /* alias */
  CCWLD_PHASE_GC = 2,
  CCWLD_PHASE_LAYOUT = 3,
  CCWLD_PHASE_EMIT = 4,
  CCWLD_PHASE_LTO = 5,     /* internal: never in a phases mask */
  CCWLD_PHASE_RELOCATE = 6 /* internal: never in a phases mask */
} ccwld_phase;

/* Bit in a vtable `phases` mask for a phase (§5: OR of
 * (1u << (ccwld_phase-1)) it wants).  Only the four spec'd phases. */
#define CCWLD_PHASE_BIT(ph) ((uint32_t)1u << ((uint32_t)(ph) - 1u))
#define CCWLD_PHASE_ALL_SPEC_BITS                                            \
  (CCWLD_PHASE_BIT (CCWLD_PHASE_RESOLVED) | CCWLD_PHASE_BIT (CCWLD_PHASE_GC) \
   | CCWLD_PHASE_BIT (CCWLD_PHASE_LAYOUT)                                    \
   | CCWLD_PHASE_BIT (CCWLD_PHASE_EMIT))

typedef struct ccwld_link ccwld_link;

typedef struct
{
  uint32_t abi_version;
  const char *name;
  uint32_t phases; /* OR of CCWLD_PHASE_BIT(phase) it wants */
  int (*init) (void *, const char *);
  int (*run) (void *, ccwld_phase, ccwld_link *);
  void (*fini) (void *);
  void *self;
} ccwld_plugin_vtable;
typedef const ccwld_plugin_vtable *(*ccwld_plugin_entry_fn) (void);

/* --- introspection handle (§5, lccwld §4.10) --------------------------
 *
 * The ccwld_link handle exposes the same read-mostly views the Lua
 * introspection handle wraps, with C accessors, under identical
 * phase-scoped mutability rules: `resolved`/`layout` may assign symbol
 * values; `gc` may keep/reorder sections; `emit` is read-only except
 * metadata/note insertion.  Mutators take the mutation source so CCWld
 * can detect plugin/hook conflicts (§5) — never call them from outside
 * a phase callback.  All views iterate in deterministic order (link
 * order, then first-occurrence). */

typedef enum
{
  CCWLD_MUT_PLUGIN = 0,
  CCWLD_MUT_HOOK = 1,
} ccwld_mutation_source;

typedef struct
{
  const char *path;   /* input path in link order      */
  const char *kind;   /* "object"|"archive"|"dso"|"lto"*/
  const char *format; /* "elf"|"archive"|...           */
  size_t symbol_count;
  size_t section_count;
} ccwld_obj_view;

typedef struct
{
  const char *name; /* output section name            */
  uint64_t addr;    /* vma (0 before layout)          */
  uint64_t size;    /* size (0 before layout)         */
  uint64_t align;
} ccwld_sec_view;

typedef struct
{
  const char *name;
  uint64_t value;         /* 0 until known                */
  const char *defined_in; /* object path, or ""           */
  const char *binding;    /* "global"|"weak"|"local"      */
  const char *visibility; /* "default"|"hidden"|...       */
  int defined;
} ccwld_sym_view;

ccwld_phase ccwld_link_phase (ccwld_link *);

size_t ccwld_link_object_count (ccwld_link *);
int ccwld_link_object (ccwld_link *, size_t index, ccwld_obj_view *out);

size_t ccwld_link_section_count (ccwld_link *);
int ccwld_link_section (ccwld_link *, size_t index, ccwld_sec_view *out);
size_t ccwld_link_section_member_count (ccwld_link *, size_t index);
const char *ccwld_link_section_member (ccwld_link *, size_t index,
                                       size_t member);

size_t ccwld_link_symbol_count (ccwld_link *);
int ccwld_link_symbol (ccwld_link *, size_t index, ccwld_sym_view *out);

size_t ccwld_link_undefined_count (ccwld_link *);
const char *ccwld_link_undefined (ccwld_link *, size_t index);

size_t ccwld_link_reloc_stat_count (ccwld_link *);
int ccwld_link_reloc_stat (ccwld_link *, size_t index, const char **type,
                           size_t *count);

/* Phase-scoped mutators.  Return 1 on success, 0 on scope violation
 * (the violation itself is reported through CCWld's diagnostic stream
 * by the pipeline driver when the phase callback returns nonzero, or
 * immediately for out-of-scope attempts). */
int ccwld_link_set_symbol (ccwld_link *, const char *name, uint64_t value,
                           ccwld_mutation_source);
int ccwld_link_keep_section (ccwld_link *, const char *section_name,
                             ccwld_mutation_source);
int ccwld_link_move_section (ccwld_link *, size_t from, size_t to,
                             ccwld_mutation_source);
int ccwld_link_add_note (ccwld_link *, const char *key, const char *value,
                         ccwld_mutation_source);
#endif
