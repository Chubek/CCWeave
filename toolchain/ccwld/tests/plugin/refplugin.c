/* refplugin — the in-tree reference ccwld plugin (CCWLD.md §5, §10).
 *
 * Built as a MODULE against the shipped abi/ccwld-plugin.h and used by
 * tests/plugin/.  It subscribes to all four spec'd phases, exercises
 * the read-only introspection surface at each one, and performs the
 * canonical legal mutations: set_symbol at resolved, keep_section at
 * gc, add_note at emit.  Its options JSON carries the symbols to set
 * and the note to add, e.g.
 *   {"note":{"key":"refplugin","value":"ran"},"set":{"s":16}}
 * Unknown options are ignored (plugins must tolerate forward
 * options).  Everything it does is deterministic. */
#include "../../abi/ccwld-plugin.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
  char note_key[64];
  char note_value[192];
  char set_name[64]; /* symbol to set at the resolved phase */
  uint64_t set_value;
  char keep[64]; /* section glob to keep at the gc phase */
  int ran_phases; /* bitmask of phases observed (self-check) */
} ref_state;

/* --- tiny JSON scalars (no nesting beyond one object per key) --- */

static const char *
json_find (const char *json, const char *key)
{
  if (!json)
    return NULL;
  char pat[80];
  snprintf (pat, sizeof (pat), "\"%s\"", key);
  const char *p = strstr (json, pat);
  return p ? p + strlen (pat) : NULL;
}

static int
json_string (const char *json, const char *key, char *out, size_t outn)
{
  const char *p = json_find (json, key);
  if (!p)
    return 0;
  p = strchr (p, ':');
  if (!p)
    return 0;
  p++;
  while (*p == ' ' || *p == '\t')
    p++;
  if (*p != '"')
    return 0;
  p++;
  size_t n = 0;
  while (*p && *p != '"' && n + 1 < outn)
    {
      if (*p == '\\' && p[1])
        p++;
      out[n++] = *p++;
    }
  out[n] = 0;
  return 1;
}

static int
json_uint (const char *json, const char *key, uint64_t *out)
{
  const char *p = json_find (json, key);
  if (!p)
    return 0;
  p = strchr (p, ':');
  if (!p)
    return 0;
  *out = strtoull (p + 1, NULL, 0);
  return 1;
}

static void
ref_diag (const char *fmt, ...)
{
  va_list ap;
  fputs ("refplugin: ", stderr);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
}

/* --- vtable callbacks --- */

static ref_state S;

static int
ref_init (void *self, const char *options)
{
  (void)self;
  memset (&S, 0, sizeof (S));
  json_string (options, "note_key", S.note_key, sizeof (S.note_key));
  json_string (options, "note_value", S.note_value, sizeof (S.note_value));
  json_string (options, "set_name", S.set_name, sizeof (S.set_name));
  json_uint (options, "set_value", &S.set_value);
  json_string (options, "keep", S.keep, sizeof (S.keep));
  if (!S.note_key[0])
    snprintf (S.note_key, sizeof (S.note_key), "refplugin");
  if (!S.note_value[0])
    snprintf (S.note_value, sizeof (S.note_value), "ran");
  return 0;
}

static int
ref_run (void *self, ccwld_phase phase, ccwld_link *lk)
{
  (void)self;
  ccwld_phase now = ccwld_link_phase (lk);
  if (now != phase)
    {
      ref_diag ("phase handle mismatch (%d != %d)", (int)now, (int)phase);
      return 1;
    }

  /* introspection is legal at every phase (§5) */
  size_t nobjs = ccwld_link_object_count (lk);
  size_t nsyms = ccwld_link_symbol_count (lk);
  size_t nundef = ccwld_link_undefined_count (lk);
  for (size_t i = 0; i < nobjs; i++)
    {
      ccwld_obj_view v;
      if (!ccwld_link_object (lk, i, &v))
        return 1;
    }
  for (size_t i = 0; i < nsyms; i++)
    {
      ccwld_sym_view v;
      if (!ccwld_link_symbol (lk, i, &v))
        return 1;
    }

  switch (phase)
    {
    case CCWLD_PHASE_RESOLVED:
      if (S.set_name[0]
          && !ccwld_link_set_symbol (lk, S.set_name, S.set_value,
                                     CCWLD_MUT_PLUGIN))
        return 1; /* scope violation: CCWld reports the diagnostic */
      break;
    case CCWLD_PHASE_GC:
      if (S.keep[0] && !ccwld_link_keep_section (lk, S.keep, CCWLD_MUT_PLUGIN))
        return 1;
      break;
    case CCWLD_PHASE_LAYOUT:
      /* read-only here by design: sections carry layout results */
      break;
    case CCWLD_PHASE_EMIT:
      if (!ccwld_link_add_note (lk, S.note_key, S.note_value,
                                CCWLD_MUT_PLUGIN))
        return 1;
      break;
    default:
      ref_diag ("scheduled at a non-plugin phase %d", (int)phase);
      return 1;
    }

  S.ran_phases |= (int)CCWLD_PHASE_BIT (phase);
  (void)nundef;
  return 0;
}

static void
ref_fini (void *self)
{
  (void)self;
}

static const ccwld_plugin_vtable *
ccwld_plugin_entry (void)
{
  static const ccwld_plugin_vtable vt = {
    CCWLD_PLUGIN_ABI_VERSION,
    "refplugin",
    CCWLD_PHASE_ALL_SPEC_BITS,
    ref_init,
    ref_run,
    ref_fini,
    NULL,
  };
  return &vt;
}
