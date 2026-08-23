/* ccwld.c — the pipeline driver (§3) and convenience API.
 *
 * load ▸ resolve ▸ [LTO] ▸ gc ▸ layout ▸ relocate ▸ emit, with plugins
 * (§5) dispatched before hooks at each spec'd phase, phase-scoped
 * mutability enforced through the ccwld_link accessors, and CCWld as
 * the conflict authority when a plugin and a hook touch the same key.
 * The plan IR lives in plan/; the phase implementations in phases/;
 * emission in emit/; the link cache in cache/. */

#include "ccwld.h"
#include "cache/ccwld_cache.h"
#include "emit/ccwld_emit.h"
#include "phases/ccwld_phases.h"

#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- error helper (declared in ccwld.h) --- */

void
ccwld_error_set (ccwld_error *e, int code, const char *fmt, ...)
{
  if (!e)
    return;
  e->code = code;
  va_list ap;
  va_start (ap, fmt);
  vsnprintf (e->message, sizeof (e->message), fmt, ap);
  va_end (ap);
}

/* --- plugin modules (§5) --- */

typedef struct
{
  void *dl;
  const ccwld_plugin_vtable *vt;
  char *name;
} plugin_mod;

typedef struct
{
  ccwld_plan *p;
  ccwld_state *st;
  ccwld_link link;
  plugin_mod *mods;
  size_t nmods;
} driver_t;

static void
plugins_unload (driver_t *d)
{
  for (size_t i = 0; i < d->nmods; i++)
    {
      if (d->mods[i].vt && d->mods[i].vt->fini)
        d->mods[i].vt->fini (d->mods[i].vt->self);
      if (d->mods[i].dl)
        dlclose (d->mods[i].dl);
      free (d->mods[i].name);
    }
  free (d->mods);
  d->mods = NULL;
  d->nmods = 0;
}

/* Load every registered plugin.  Registration order is dispatch order
 * (§5).  ABI mismatches are fatal with exit 3 — never a silent skip. */
static int
plugins_load (driver_t *d, ccwld_error *e)
{
  ccwld_plan *p = d->p;
  if (p->nplugins == 0)
    return 1;
  d->mods = calloc (p->nplugins, sizeof (*d->mods));
  if (!d->mods)
    {
      ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
      return 0;
    }
  for (size_t i = 0; i < p->nplugins; i++)
    {
      ccwld_plugin *reg = &p->plugins[i];
      void *dl = dlopen (reg->path, RTLD_NOW | RTLD_LOCAL);
      if (!dl)
        {
          ccwld_error_set (e, CCWLD_EXIT_ABI, "cannot load plugin '%s': %s",
                           reg->path, dlerror ());
          goto fail;
        }
      ccwld_plugin_entry_fn entry
          = (ccwld_plugin_entry_fn)dlsym (dl, "ccwld_plugin_entry");
      if (!entry)
        {
          ccwld_error_set (e, CCWLD_EXIT_ABI,
                           "plugin '%s' has no ccwld_plugin_entry", reg->path);
          dlclose (dl);
          goto fail;
        }
      const ccwld_plugin_vtable *vt = entry ();
      if (!vt)
        {
          ccwld_error_set (e, CCWLD_EXIT_ABI,
                           "plugin '%s' returned no vtable", reg->path);
          dlclose (dl);
          goto fail;
        }
      if (vt->abi_version != CCWLD_PLUGIN_ABI_VERSION)
        {
          ccwld_error_set (e, CCWLD_EXIT_ABI,
                           "plugin '%s' ABI version %u, expected %u",
                           reg->path, vt->abi_version,
                           (unsigned)CCWLD_PLUGIN_ABI_VERSION);
          dlclose (dl);
          goto fail;
        }
      if (vt->phases & ~CCWLD_PHASE_ALL_SPEC_BITS)
        {
          ccwld_error_set (e, CCWLD_EXIT_ABI,
                           "plugin '%s' requests phases outside the four "
                           "spec'd plugin phases (mask 0x%x)",
                           reg->path, vt->phases);
          dlclose (dl);
          goto fail;
        }
      if (vt->init && vt->init (vt->self, reg->options) != 0)
        {
          ccwld_error_set (e, CCWLD_EXIT_ABI, "plugin '%s' init failed",
                           vt->name ? vt->name : reg->path);
          dlclose (dl);
          goto fail;
        }
      d->mods[d->nmods].dl = dl;
      d->mods[d->nmods].vt = vt;
      d->mods[d->nmods].name = strdup (vt->name ? vt->name : reg->path);
      d->nmods++;
      reg->loaded = 1;
      reg->name = reg->name ? reg->name : strdup (vt->name ? vt->name
                                                           : reg->path);
    }
  return 1;

fail:
  plugins_unload (d);
  return 0;
}

/* --- phase dispatch: plugins first, then hooks (§5) --- */

static int
dispatch_phase (driver_t *d, ccwld_phase ph, ccwld_error *e)
{
  ccwld_state *st = d->st;

  /* plugins in registration order, only those that asked for `ph` */
  for (size_t i = 0; i < d->nmods; i++)
    {
      const ccwld_plugin_vtable *vt = d->mods[i].vt;
      if (!vt || !vt->run || !(vt->phases & CCWLD_PHASE_BIT (ph)))
        continue;
      st->in_callback++;
      int rc = vt->run (vt->self, ph, &d->link);
      st->in_callback--;
      ccwld_diag_flush_pending (st);
      if (rc != 0)
        {
          ccwld_error_set (e, CCWLD_EXIT_ABI, "plugin '%s' failed at phase %d",
                           d->mods[i].name, (int)ph);
          return 0;
        }
    }

  /* hooks in registration order (lccwld §4.9: depth capped at 8) */
  for (size_t i = 0; i < d->p->nhooks; i++)
    {
      ccwld_hook *h = &d->p->hooks[i];
      if (h->phase != ph || !h->fn)
        continue;
      if (st->hook_depth >= 8)
        {
          ccwld_error_set (e, CCWLD_EXIT_USAGE,
                           "hook recursion exceeds depth 8 at phase %d",
                           (int)ph);
          return 0;
        }
      st->hook_depth++;
      st->in_callback++;
      int rc = h->fn (ph, &d->link, h->user);
      st->in_callback--;
      st->hook_depth--;
      ccwld_diag_flush_pending (st);
      if (rc != 0)
        {
          ccwld_error_set (e, CCWLD_EXIT_LINK, "hook failed at phase %d",
                           (int)ph);
          return 0;
        }
    }

  /* CCWld is the conflict authority: report same-key mutations from
   * both a plugin and a hook at this phase (§5). */
  ccwld_state_conflict_scan (st, (int)ph);
  return 1;
}

/* --- ccwld_link introspection (§5, lccwld §4.10) --- */

static ccwld_state *
link_state (ccwld_link *l)
{
  return l ? (ccwld_state *)l->phase_state : NULL;
}

ccwld_phase
ccwld_link_phase (ccwld_link *l)
{
  return l ? l->phase : CCWLD_PHASE_LOAD;
}

size_t
ccwld_link_object_count (ccwld_link *l)
{
  ccwld_state *st = link_state (l);
  return st ? st->nobjs : 0;
}

int
ccwld_link_object (ccwld_link *l, size_t index, ccwld_obj_view *out)
{
  ccwld_state *st = link_state (l);
  if (!st || !out || index >= st->nobjs)
    return 0;
  ccwld_obj *o = &st->objs[index];
  memset (out, 0, sizeof (*out));
  out->path = o->path;
  out->kind = o->kind;
  out->format = o->format;
  out->symbol_count = o->nsyms;
  out->section_count = o->nsecs;
  return 1;
}

size_t
ccwld_link_section_count (ccwld_link *l)
{
  ccwld_state *st = link_state (l);
  return (st && st->plan) ? st->plan->nsecs : 0;
}

int
ccwld_link_section (ccwld_link *l, size_t index, ccwld_sec_view *out)
{
  ccwld_state *st = link_state (l);
  if (!st || !st->plan || !out || index >= st->plan->nsecs)
    return 0;
  ccwld_sec *s = &st->plan->secs[index];
  memset (out, 0, sizeof (*out));
  out->name = s->name;
  out->addr = s->vma;
  out->size = s->size;
  out->align = s->align;
  return 1;
}

size_t
ccwld_link_section_member_count (ccwld_link *l, size_t index)
{
  ccwld_state *st = link_state (l);
  if (!st || !st->plan || index >= st->plan->nsecs)
    return 0;
  size_t n = 0;
  for (size_t oi = 0; oi < st->nobjs; oi++)
    for (size_t k = 0; k < st->objs[oi].nsecs; k++)
      {
        ccwld_isec *is = &st->objs[oi].secs[k];
        if (is->placed && is->out_sec == (int)index)
          n++;
      }
  return n;
}

const char *
ccwld_link_section_member (ccwld_link *l, size_t index, size_t member)
{
  ccwld_state *st = link_state (l);
  if (!st || !st->plan || index >= st->plan->nsecs)
    return NULL;
  size_t n = 0;
  for (size_t oi = 0; oi < st->nobjs; oi++)
    for (size_t k = 0; k < st->objs[oi].nsecs; k++)
      {
        ccwld_isec *is = &st->objs[oi].secs[k];
        if (!is->placed || is->out_sec != (int)index)
          continue;
        if (n == member)
          return is->name;
        n++;
      }
  return NULL;
}

size_t
ccwld_link_symbol_count (ccwld_link *l)
{
  ccwld_state *st = link_state (l);
  return st ? st->nrsyms : 0;
}

int
ccwld_link_symbol (ccwld_link *l, size_t index, ccwld_sym_view *out)
{
  ccwld_state *st = link_state (l);
  if (!st || !out || index >= st->nrsyms)
    return 0;
  ccwld_rsym *r = &st->rsyms[index];
  memset (out, 0, sizeof (*out));
  out->name = r->name;
  out->value = r->value_known ? r->value : 0;
  out->defined_in = (r->obj >= 0 && (size_t)r->obj < st->nobjs)
                        ? st->objs[r->obj].path
                        : "";
  out->binding = r->binding ? r->binding : (r->weak ? "weak" : "global");
  out->visibility = r->visibility ? r->visibility : "default";
  out->defined = r->defined;
  return 1;
}

size_t
ccwld_link_undefined_count (ccwld_link *l)
{
  ccwld_state *st = link_state (l);
  size_t n = 0;
  if (!st)
    return 0;
  for (size_t i = 0; i < st->nrsyms; i++)
    if (!st->rsyms[i].defined)
      n++;
  return n;
}

const char *
ccwld_link_undefined (ccwld_link *l, size_t index)
{
  ccwld_state *st = link_state (l);
  if (!st)
    return NULL;
  size_t n = 0;
  for (size_t i = 0; i < st->nrsyms; i++)
    {
      if (st->rsyms[i].defined)
        continue;
      if (n == index)
        return st->rsyms[i].name;
      n++;
    }
  return NULL;
}

size_t
ccwld_link_reloc_stat_count (ccwld_link *l)
{
  ccwld_state *st = link_state (l);
  return st ? st->nstats : 0;
}

int
ccwld_link_reloc_stat (ccwld_link *l, size_t index, const char **type,
                       size_t *count)
{
  ccwld_state *st = link_state (l);
  if (!st || index >= st->nstats)
    return 0;
  if (type)
    *type = st->stats[index].name;
  if (count)
    *count = st->stats[index].count;
  return 1;
}

/* --- phase-scoped mutators --- */

static const char *
mut_src_name (ccwld_mutation_source src)
{
  return src == CCWLD_MUT_PLUGIN ? "plugin" : "hook";
}

int
ccwld_link_set_symbol (ccwld_link *l, const char *name, uint64_t value,
                       ccwld_mutation_source src)
{
  ccwld_state *st = link_state (l);
  if (!st || !name)
    return 0;
  if (l->phase != CCWLD_PHASE_RESOLVE && l->phase != CCWLD_PHASE_LAYOUT)
    {
      ccwld_diag_error (st, CCWLD_EXIT_USAGE, name, NULL,
                        "symbol assignment by a %s is only allowed at the "
                        "resolved and layout phases (now: phase %d)",
                        mut_src_name (src), (int)l->phase);
      return 0;
    }
  ccwld_rsym *r = ccwld_state_rsym (st, name);
  if (!r)
    return 0;
  r->value = value;
  r->value_known = 1;
  r->defined = 1;
  {
    char key[160];
    snprintf (key, sizeof (key), "sym:%s", name);
    ccwld_state_record_mut (st, key, (int)src, (int)l->phase);
  }
  return 1;
}

int
ccwld_link_keep_section (ccwld_link *l, const char *section_name,
                         ccwld_mutation_source src)
{
  ccwld_state *st = link_state (l);
  if (!st || !section_name)
    return 0;
  if (l->phase != CCWLD_PHASE_GC)
    {
      ccwld_diag_error (st, CCWLD_EXIT_USAGE, section_name, NULL,
                        "keep_section by a %s is only allowed at the gc "
                        "phase (now: phase %d)",
                        mut_src_name (src), (int)l->phase);
      return 0;
    }
  int marked = 0;
  for (size_t oi = 0; oi < st->nobjs; oi++)
    for (size_t k = 0; k < st->objs[oi].nsecs; k++)
      {
        ccwld_isec *is = &st->objs[oi].secs[k];
        if (is->live != 1)
          continue;
        if (ccwld_glob_match (section_name, is->name))
          {
            is->is_root = 1;
            marked++;
          }
      }
  if (marked == 0)
    return 0; /* matched nothing: caller-visible, not fatal */
  char key[160];
  snprintf (key, sizeof (key), "keep:%s", section_name);
  ccwld_state_record_mut (st, key, (int)src, (int)l->phase);
  return 1;
}

int
ccwld_link_move_section (ccwld_link *l, size_t from, size_t to,
                         ccwld_mutation_source src)
{
  ccwld_state *st = link_state (l);
  if (!st || !st->plan)
    return 0;
  ccwld_plan *p = st->plan;
  if (l->phase != CCWLD_PHASE_GC)
    {
      ccwld_diag_error (st, CCWLD_EXIT_USAGE, NULL, NULL,
                        "move_section by a %s is only allowed at the gc "
                        "phase (now: phase %d)",
                        mut_src_name (src), (int)l->phase);
      return 0;
    }
  if (from >= p->nsecs || to >= p->nsecs || from == to || p->nsecs < 2)
    return 0;
  ccwld_sec moved = p->secs[from];
  if (from < to)
    memmove (&p->secs[from], &p->secs[from + 1],
             (to - from) * sizeof (ccwld_sec));
  else
    memmove (&p->secs[to + 1], &p->secs[to],
             (from - to) * sizeof (ccwld_sec));
  p->secs[to] = moved;
  char key[160];
  snprintf (key, sizeof (key), "move:%zu:%zu", from, to);
  ccwld_state_record_mut (st, key, (int)src, (int)l->phase);
  return 1;
}

int
ccwld_link_add_note (ccwld_link *l, const char *key, const char *value,
                     ccwld_mutation_source src)
{
  ccwld_state *st = link_state (l);
  if (!st || !key || !value)
    return 0;
  if (l->phase != CCWLD_PHASE_EMIT)
    {
      ccwld_diag_error (st, CCWLD_EXIT_USAGE, key, NULL,
                        "note insertion by a %s is only allowed at the "
                        "emit phase (now: phase %d)",
                        mut_src_name (src), (int)l->phase);
      return 0;
    }
  ccwld_note *n = realloc (st->notes, (st->nnotes + 1) * sizeof (*n));
  if (!n)
    return 0;
  st->notes = n;
  st->notes[st->nnotes].key = strdup (key);
  st->notes[st->nnotes].value = strdup (value);
  st->nnotes++;
  char mkey[160];
  snprintf (mkey, sizeof (mkey), "note:%s", key);
  ccwld_state_record_mut (st, mkey, (int)src, (int)l->phase);
  return 1;
}

/* --- driver-level declarations (§2.1, pre-seal) ---
 * Shared by both frontends: applies the command-line face of the link
 * before the script body runs.  `entry` is handled separately after
 * the script (GNU -e overrides ENTRY/OUTPUT). */
int
ccwld_apply_driver_defs (ccwld_plan *p, const ccwld_driver_defs *extra,
                         ccwld_error *e)
{
  if (!extra)
    return 1;
  if (extra->search_paths)
    for (size_t i = 0; extra->search_paths[i]; i++)
      if (!ccwld_plan_search_path (p, extra->search_paths[i], e))
        return 0;
  if (extra->inputs)
    for (size_t i = 0; extra->inputs[i]; i++)
      if (!ccwld_plan_input (p, extra->inputs[i], 0, 0, e))
        return 0;
  if (extra->plugins)
    for (size_t i = 0; extra->plugins[i]; i++)
      if (!ccwld_plan_plugin (p, extra->plugins[i],
                              extra->plugin_opts_json ? extra->plugin_opts_json
                                                      : "{}",
                              e))
        return 0;
  if (extra->lto_pipeline)
    if (!ccwld_plan_lto (p, extra->lto_pipeline, extra->lto_jobs,
                         extra->lto_cache_dir, e))
      return 0;
  return 1;
}

/* -e / --entry: re-issue the output declaration with the driver's
 * entry symbol, preserving whatever the script declared. */
int
ccwld_driver_entry_override (ccwld_plan *p, const char *entry, ccwld_error *e)
{
  if (!p || !entry || !p->output.kind)
    return 1;
  /* plan_output frees the current strings, so hand it private copies */
  ccwld_output o;
  memset (&o, 0, sizeof (o));
  o.kind = strdup (p->output.kind);
  o.format = strdup (p->output.format ? p->output.format : "elf");
  o.entry = strdup (entry);
  o.soname = p->output.soname ? strdup (p->output.soname) : NULL;
  o.osabi = p->output.osabi ? strdup (p->output.osabi) : NULL;
  int ok = ccwld_plan_output (p, &o, e);
  free (o.kind);
  free (o.format);
  free (o.entry);
  free (o.soname);
  free (o.osabi);
  return ok;
}

/* --- default sections (a plan with no SECTIONS still links) --- */

static int
synthesize_defaults (ccwld_plan *p, ccwld_error *e)
{
  static const struct
  {
    const char *name;
    const char *selector;
    uint64_t align;
    int load;
  } defs[] = {
    { ".text", ".text .text.*", 16, 1 },
    { ".rodata", ".rodata .rodata.* .srodata .srodata.*", 8, 1 },
    { ".data", ".data .data.* .sdata .sdata.*", 8, 1 },
    { ".bss", ".bss .bss.* .sbss .sbss.* COMMON", 8, 0 },
  };
  for (size_t i = 0; i < sizeof (defs) / sizeof (defs[0]); i++)
    {
      if (!ccwld_plan_section (p, defs[i].name, NULL, defs[i].align,
                               defs[i].selector, NULL, e))
        return 0;
      if (!defs[i].load
          && !ccwld_plan_section_set_load (p, defs[i].name, 0, e))
        return 0;
    }
  return 1;
}

/* --- link execution (§3) --- */

int
ccwld_link_run (ccwld_plan *p, const char *out, ccwld_error *e)
{
  driver_t d;
  int ok = 0;

  if (!p || !out)
    {
      ccwld_error_set (e, CCWLD_EXIT_USAGE, "invalid link request");
      return 0;
    }

  if (!p->sealed)
    {
      if (p->nsecs == 0 && !synthesize_defaults (p, e))
        return 0;
      if (!ccwld_plan_seal (p, e))
        return 0;
    }

  memset (&d, 0, sizeof (d));
  d.p = p;
  d.st = ccwld_state_new (p);
  if (!d.st)
    {
      ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
      return 0;
    }
  d.link.plan = p;
  d.link.phase_state = d.st;

  if (!plugins_load (&d, e))
    goto done;

  /* cache lookup (§7.3): a hit means the artifact is byte-identical */
  char cache_key[65];
  const char *cache_dir = p->options.no_cache ? NULL : p->options.cache_dir;
  if (cache_dir)
    {
      if (!ccwld_cache_key (p, cache_key))
        {
          ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "cache key failed");
          goto done;
        }
      int hit = ccwld_cache_lookup (cache_dir, cache_key, out, e);
      if (hit < 0)
        goto done;
      if (hit > 0)
        {
          ok = 1;
          goto done;
        }
    }

  /* the pipeline (§3) */
  if (!ccwld_phase_load (d.st, e))
    goto done;
  if (!ccwld_phase_resolve (d.st, e))
    goto done;
  d.link.phase = CCWLD_PHASE_RESOLVE;
  if (!dispatch_phase (&d, CCWLD_PHASE_RESOLVE, e))
    goto done;

  if (!ccwld_phase_lto (d.st, e))
    goto done;

  if (!ccwld_phase_gc (d.st, e))
    goto done;
  d.link.phase = CCWLD_PHASE_GC;
  if (!dispatch_phase (&d, CCWLD_PHASE_GC, e))
    goto done;

  if (!ccwld_phase_layout (d.st, e))
    goto done;
  d.link.phase = CCWLD_PHASE_LAYOUT;
  if (!dispatch_phase (&d, CCWLD_PHASE_LAYOUT, e))
    goto done;

  if (!ccwld_phase_relocate (d.st, e))
    goto done;
  d.link.phase = CCWLD_PHASE_EMIT;
  if (!dispatch_phase (&d, CCWLD_PHASE_EMIT, e))
    goto done;

  if (!ccwld_emit_object (d.st, out, e))
    goto done;

  /* store failures are warnings: the cache is an optimization, the
   * artifact itself is already on disk */
  if (cache_dir && !ccwld_cache_store (cache_dir, cache_key, out, NULL))
    ccwld_diag_warn (d.st, out, NULL, "cache store failed");
  ok = 1;

done:
  if (p->options.print_plan && p->serialized)
    {
      fwrite (p->serialized, 1, p->serialized_len, stdout);
      fputc ('\n', stdout);
    }
  ccwld_diag_print (d.st, stderr);
  plugins_unload (&d);
  ccwld_state_free (d.st);
  return ok;
}

/* --- convenience: link files (the "api" frontend) --- */

int
ccwld_link_files (const char *target, const char *output,
                  const char *const *inputs, size_t input_count,
                  const ccwld_link_options *options, ccwld_error *e)
{
  ccwld_plan *p = ccwld_plan_new (target);
  ccwld_output plan_out;
  int ok = 0;

  if (!p || !output || (!inputs && input_count))
    {
      ccwld_plan_free (p);
      ccwld_error_set (e, CCWLD_EXIT_USAGE, "invalid link request");
      return 0;
    }

  ccwld_plan_set_frontend (p, "api");

  memset (&plan_out, 0, sizeof (plan_out));
  plan_out.kind = strdup (options && options->kind ? options->kind : "exe");
  plan_out.format
      = strdup (options && options->format ? options->format : "elf");
  plan_out.entry = strdup (options ? options->entry : NULL);
  plan_out.soname = strdup (options ? options->soname : NULL);
  plan_out.osabi = strdup (options ? options->osabi : NULL);

  if (!ccwld_plan_output (p, &plan_out, e))
    goto done;

  free (plan_out.kind);
  free (plan_out.format);
  free (plan_out.entry);
  free (plan_out.soname);
  free (plan_out.osabi);

  if (options)
    {
      for (size_t i = 0; i < options->search_path_count; ++i)
        if (!ccwld_plan_search_path (p, options->search_paths[i], e))
          goto done;
    }

  for (size_t i = 0; i < input_count; ++i)
    if (!ccwld_plan_input (p, inputs[i], 0, i == 0, e))
      goto done;

  ok = ccwld_link_run (p, output, e);
done:
  ccwld_plan_free (p);
  return ok;
}
