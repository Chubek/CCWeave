/* §3 resolve: symbol resolution across objects/archives.
 *
 * Deterministic tie-breaking: link order, then first-occurrence (§7).
 * Archive groups use repeated-scan resolution (ld GROUP semantics);
 * `--as-needed` DSOs are pruned unless they satisfied a reference;
 * script-level assigns overriding input definitions are fatal (lccwld
 * §4.7); PROVIDE defines only when referenced and not otherwise
 * defined. */
#include "ccwld_phases.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define SHN_UNDEF_SYM 0

/* Merge one object's global/weak symbols into the resolved table.
 * Returns 0 on a hard error (duplicate strong definition). */
static int
merge_object (ccwld_state *st, int oi, ccwld_error *e)
{
  ccwld_obj *o = &st->objs[oi];
  for (size_t i = 0; i < o->nsyms; i++)
    {
      ccwld_isym *s = &o->syms[i];
      if (s->binding == 0 || !s->name[0])
        continue; /* locals do not participate */
      ccwld_rsym *r = ccwld_state_rsym (st, s->name);
      if (!r)
        {
          ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
          return 0;
        }
      r->referenced = 1;
      if (s->shndx == SHN_UNDEF_SYM)
        continue; /* reference only */
      if (r->defined)
        {
          const ccwld_obj *prev = &st->objs[r->obj];
          int prev_strong = r->defined && !r->weak;
          int this_weak = (s->binding == 2);
          if (prev_strong && !this_weak)
            {
              ccwld_diag_error (
                  st, CCWLD_EXIT_LINK, NULL, NULL,
                  "duplicate symbol '%s' (defined in '%s' and '%s')",
                  s->name, prev->path, o->path);
              ccwld_error_set (
                  e, CCWLD_EXIT_LINK,
                  "duplicate symbol '%s' (defined in '%s' and '%s')",
                  s->name, prev->path, o->path);
              return 0;
            }
          if (!r->weak && this_weak)
            continue; /* existing strong wins */
          if (r->from_script && !r->provided)
            {
              /* script assign overriding an input definition is fatal
               * (lccwld §4.7); PROVIDE yields to the input definition */
              ccwld_plan *p = st->plan;
              const char *site
                  = (r->script_idx >= 0
                     && (size_t)r->script_idx < p->nsyms)
                        ? (p->syms[r->script_idx].site
                               ? p->syms[r->script_idx].site
                               : "")
                        : "";
              ccwld_error_set (
                  e, CCWLD_EXIT_LINK,
                  "symbol '%s' assigned by the link script (%s) is already "
                  "defined by input object '%s'; use PROVIDE to yield",
                  s->name, site, o->path);
              return 0;
            }
          /* weak replaces weak / replaces nothing stronger */
          if (r->weak && !this_weak)
            {
              /* new strong beats old weak */
            }
          else
            {
              continue; /* first occurrence wins among equals */
            }
        }
      r->defined = 1;
      r->obj = oi;
      r->isym = (int)i;
      r->weak = (s->binding == 2);
      r->from_script = 0;
      r->value_known = 0;
      r->size = s->size;
      free (r->binding);
      free (r->visibility);
      r->binding = strdup (r->weak ? "weak" : "global");
      r->visibility = strdup (s->visibility == 2  ? "hidden"
                              : s->visibility == 1 ? "internal"
                              : s->visibility == 3 ? "protected"
                                                   : "default");
      if (!r->binding || !r->visibility)
        {
          ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
          return 0;
        }
    }
  o->used = 1;
  return 1;
}

/* Extract an archive member that defines a currently-undefined symbol. */
static int
extract_member (ccwld_state *st, int aoi, int midx, int from_group,
                ccwld_error *e)
{
  ccwld_obj *a = &st->objs[aoi];
  ccwld_ar_member *m = &a->members[midx];
  if (m->extracted)
    return 1;
  char pathbuf[512];
  snprintf (pathbuf, sizeof (pathbuf), "%s(%s)", a->path, m->name);
  if (!ccwld_load_elf_mem (st, pathbuf, m->data, m->size, e))
    return 0;
  m->extracted = 1;
  a->used = 1;
  ccwld_obj *loaded = &st->objs[st->nobjs - 1];
  loaded->from_group = from_group;
  return merge_object (st, (int)(st->nobjs - 1), e);
}

/* One pass over an archive (or group of archives): extract any member
 * defining a currently-undefined symbol.  Returns the number of
 * members extracted. */
static int
archive_scan_once (ccwld_state *st, int aoi, int from_group, ccwld_error *e)
{
  int extracted = 0;
  size_t symbol_count = st->objs[aoi].nar_syms;
  for (size_t i = 0; i < symbol_count; i++)
    {
      ccwld_obj *a = &st->objs[aoi];
      ccwld_rsym *r = ccwld_state_rsym (st, a->ar_syms[i]);
      if (!r)
        {
          ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
          return -1;
        }
      if (r->defined || !r->referenced)
        continue;
      int midx = a->ar_sym_member[i];
      if (a->members[midx].extracted)
        continue;
      if (!extract_member (st, aoi, midx, from_group, e))
        return -1;
      extracted++;
    }
  return extracted;
}

/* --- script-level symbols --- */

static int
apply_attr_overrides (ccwld_state *st, ccwld_error *e)
{
  ccwld_plan *p = st->plan;
  for (size_t i = 0; i < p->nattrs; i++)
    {
      const ccwld_attr *a = &p->attrs[i];
      /* aliases are materialized as plan symbols at build time (the
       * plan is sealed here); only visibility/binding apply now */
      if (!a->visibility && !a->binding)
        continue;
      ccwld_rsym *r = ccwld_state_rsym (st, a->name);
      if (!r)
        {
          ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
          return 0;
        }
      if (a->visibility)
        {
          free (r->visibility);
          r->visibility = strdup (a->visibility);
        }
      if (a->binding)
        {
          free (r->binding);
          r->binding = strdup (a->binding);
          r->weak = !strcmp (a->binding, "weak");
        }
    }
  return 1;
}

static int
apply_script_symbols (ccwld_state *st, ccwld_error *e)
{
  ccwld_plan *p = st->plan;
  for (size_t i = 0; i < p->nsyms; i++)
    {
      ccwld_sym *s = &p->syms[i];
      ccwld_rsym *r = ccwld_state_rsym (st, s->name);
      if (!r)
        {
          ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
          return 0;
        }
      if (s->provide)
        {
          /* PROVIDE: define only if referenced and not otherwise
           * defined (ld semantics; lccwld §4.7) */
          if (r->defined || !r->referenced)
            continue;
          r->defined = 1;
          r->from_script = 1;
          r->provided = 1;
          r->script_idx = (int)i;
          r->value_known = 0;
          free (r->binding);
          free (r->visibility);
          r->binding = strdup ("global");
          r->visibility = strdup (s->hidden ? "hidden" : "default");
          if (!r->binding || !r->visibility)
            {
              ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
              return 0;
            }
        }
      else
        {
          if (r->defined && !r->from_script && r->obj >= 0)
            {
              const ccwld_obj *prev = &st->objs[r->obj];
              ccwld_error_set (
                  e, CCWLD_EXIT_LINK,
                  "symbol '%s' assigned by the link script%s%s is already "
                  "defined by input object '%s'; use PROVIDE to yield",
                  s->name, s->site ? " (" : "", s->site ? s->site : "",
                  prev->path);
              return 0;
            }
          r->defined = 1;
          r->from_script = 1;
          r->script_idx = (int)i;
          r->value_known = 0;
          free (r->binding);
          free (r->visibility);
          r->binding = strdup (s->binding ? s->binding : "global");
          r->visibility
              = strdup (s->visibility ? s->visibility : "default");
          if (!r->binding || !r->visibility)
            {
              ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
              return 0;
            }
        }
    }
  return 1;
}

/* --- the resolve phase --- */

int
ccwld_phase_resolve (ccwld_state *st, ccwld_error *e)
{
  ccwld_plan *p = st->plan;

  /* Pass 1: references from all explicitly listed objects (input
   * order), then their definitions.  Archives record references so a
   * later group scan can extract members. */
  for (size_t i = 0; i < st->nobjs; i++)
    {
      ccwld_obj *o = &st->objs[i];
      if (!strcmp (o->kind, "archive"))
        continue;
      if (!strcmp (o->kind, "dso"))
        {
          /* DSO: only its exports matter; it never "merges" */
          for (size_t k = 0; k < o->nsyms; k++)
            {
              ccwld_isym *s = &o->syms[k];
              if (s->binding == 0 || !s->name[0])
                continue;
              ccwld_rsym *r = ccwld_state_rsym (st, s->name);
              if (!r)
                {
                  ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
                  return 0;
                }
              if (!r->defined && s->shndx != 0)
                {
                  r->defined = 1;
                  r->obj = (int)i;
                  r->isym = (int)k;
                  r->weak = (s->binding == 2);
                  r->from_script = 0;
                  r->value_known = 0;
                }
            }
          continue;
        }
      if (!merge_object (st, (int)i, e))
        return 0;
    }

  /* Pass 2: archive groups with repeated-scan resolution.  A group is
   * a maximal run of is_group inputs (ccwld_plan_group); standalone
   * archives get a single scan, group members scan to a fixpoint. */
  for (size_t i = 0; i < st->nobjs; i++)
    {
      ccwld_obj *o = &st->objs[i];
      if (strcmp (o->kind, "archive"))
        continue;
      int from_group = o->from_group;
      if (!from_group)
        {
          for (int round = 0; round < 64; round++)
            {
              int n = archive_scan_once (st, (int)i, 0, e);
              if (n < 0)
                return 0;
              if (n == 0)
                break;
            }
          continue;
        }
      /* group: repeated scan over this archive until no extraction */
      for (int round = 0; round < 64; round++)
        {
          int n = archive_scan_once (st, (int)i, 1, e);
          if (n < 0)
            return 0;
          if (n == 0)
            break;
        }
    }

  /* Pass 3: script-level symbols (assign/provide) and attr overrides. */
  if (!apply_attr_overrides (st, e))
    return 0;
  if (!apply_script_symbols (st, e))
    return 0;

  /* Pass 4: --as-needed DSO pruning — an as-needed DSO that resolved
   * nothing is dropped (its symbols become undefined again only if
   * nothing else defined them; the prune marks it unused). */
  for (size_t i = 0; i < st->nobjs; i++)
    {
      ccwld_obj *o = &st->objs[i];
      if (!o->is_dso || !o->as_needed)
        continue;
      /* did this DSO win any symbol? */
      int won = 0;
      for (size_t k = 0; k < st->nrsyms; k++)
        if (st->rsyms[k].obj == (int)i && st->rsyms[k].defined
            && !st->rsyms[k].from_script)
          {
            won = 1;
            break;
          }
      if (!won)
        {
          o->used = 0;
          for (size_t k = 0; k < st->nrsyms; k++)
            if (st->rsyms[k].obj == (int)i && st->rsyms[k].defined
                && !st->rsyms[k].from_script)
              {
                st->rsyms[k].defined = 0;
                st->rsyms[k].obj = -1;
                st->rsyms[k].isym = -1;
                st->rsyms[k].value_known = 0;
              }
        }
    }

  /* Pass 5: undefined-symbol determination. */
  st->undefined_strong = 0;
  for (size_t i = 0; i < st->nrsyms; i++)
    {
      ccwld_rsym *r = &st->rsyms[i];
      if (!r->referenced || r->defined)
        continue;
      if (r->weak)
        {
          r->value = 0;
          r->value_known = 1; /* weak undefined resolves to 0 */
          continue;
        }
      st->undefined_strong++;
      ccwld_diag_error (st, CCWLD_EXIT_LINK, NULL, NULL,
                        "undefined symbol '%s'", r->name);
    }
  if (st->undefined_strong)
    {
      ccwld_error_set (e, CCWLD_EXIT_LINK, "%d undefined symbol(s)",
                       st->undefined_strong);
      return 0;
    }

  /* Entry symbol must resolve (root for GC, §3). */
  if (p->output.entry && p->output.entry[0])
    {
      ccwld_rsym *r = ccwld_state_rsym (st, p->output.entry);
      if (!r || !r->defined)
        ccwld_diag_warn (st, NULL, NULL, "cannot find entry symbol '%s'",
                         p->output.entry);
    }
  return 1;
}
