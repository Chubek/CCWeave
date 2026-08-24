/* §3 layout: section placement and deferred-expression evaluation.
 *
 * Sections are placed into regions in plan order; the location counter
 * advances; deferred expressions (§2.3) evaluate in plan order with a
 * defined progression.  A cyclic dependency is fatal with both the
 * definition site and the layout point reported (§9).  Unplaced input
 * sections are fatal (D-0043). */
#include "ccwld_phases.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static uint64_t
align_up (uint64_t v, uint64_t a)
{
  if (a <= 1)
    return v;
  return (v + a - 1) & ~(a - 1);
}

/* --- resolver installed on the plan for deferred expressions --- */

static int
layout_resolver (const ccwld_plan *p, const char *name, uint64_t *value)
{
  ccwld_state *st = (ccwld_state *)p->state;
  if (!st)
    return 0;
  for (size_t i = 0; i < st->nrsyms; i++)
    {
      const ccwld_rsym *r = &st->rsyms[i];
      if (strcmp (r->name, name))
        continue;
      if (r->defined && r->value_known)
        {
          *value = r->value;
          return 1;
        }
      return 0;
    }
  return 0;
}

int
ccwld_state_sec_laid (const ccwld_plan *p, size_t index)
{
  ccwld_state *st = (ccwld_state *)(p ? p->state : NULL);
  if (!st || !st->sec_laid || index >= st->plan->nsecs)
    return 1; /* no live state: don't second-guess (tests, phase 0) */
  return st->sec_laid[index];
}

/* --- symbol value finalization for one output section --- */

static void
refresh_symbols_for_section (ccwld_state *st, int out_idx)
{
  for (size_t i = 0; i < st->nrsyms; i++)
    {
      ccwld_rsym *r = &st->rsyms[i];
      if (!r->defined || r->from_script || r->obj < 0 || r->isym < 0
          || r->value_known)
        continue;
      ccwld_obj *o = &st->objs[r->obj];
      if ((size_t)r->isym >= o->nsyms)
        continue;
      ccwld_isym *s = &o->syms[r->isym];
      if (s->shndx <= 0)
        {
          /* ABS (shndx == -1): absolute already */
          if (s->shndx < 0)
            {
              r->value = s->value;
              r->value_known = 1;
            }
          continue;
        }
      ccwld_isec *sec = ccwld_state_isec (st, r->obj, s->shndx);
      if (!sec || sec->out_sec != out_idx)
        continue;
      r->value = st->plan->secs[out_idx].vma + sec->out_off + s->value;
      r->value_known = 1;
    }
}

/* --- statement scheduling inside a section context --- */

static int
eval_sym (ccwld_state *st, ccwld_sym *sy, uint64_t dot, ccwld_error *e)
{
  char *emsg = NULL;
  uint64_t v = 0;
  ccwld_rsym *pre = ccwld_state_rsym (st, sy->name);
  /* an unreferenced PROVIDE stays undefined (ld semantics) */
  if (sy->provide && (!pre || !pre->defined))
    return 1;
  if (!ccwld_expr_eval (sy->expr, st->plan, dot, &v, &emsg))
    {
      const char *site = sy->site ? sy->site : "?";
      ccwld_error_set (e, CCWLD_EXIT_LINK,
                       "deferred expression for symbol '%s' failed: %s "
                       "(defined at %s)",
                       sy->name, emsg ? emsg : "?", site);
      free (emsg);
      return 0;
    }
  free (emsg);
  sy->resolved = 1;
  sy->resolved_value = v;
  ccwld_rsym *r = ccwld_state_rsym (st, sy->name);
  if (r)
    {
      r->value = v;
      r->value_known = 1;
      if (!r->defined)
        {
          r->defined = 1;
          r->from_script = 1;
        }
    }
  return 1;
}

static int
run_statements (ccwld_state *st, int sec_idx, uint64_t dot, ccwld_error *e)
{
  /* dotsteps and symbol assignments interleave by statement order */
  ccwld_plan *p = st->plan;
  for (unsigned round = 0; round < p->stmt_seq + 1; round++)
    {
      for (size_t i = 0; i < p->ndotsteps; i++)
        {
          ccwld_dotstep *d = &p->dotsteps[i];
          if (d->sec_idx != sec_idx || d->seq != round)
            continue;
          char *emsg = NULL;
          uint64_t v = 0;
          if (!ccwld_expr_eval (d->expr, p, dot, &v, &emsg))
            {
              ccwld_error_set (e, CCWLD_EXIT_LINK,
                               "location-counter assignment failed: %s "
                               "(defined at %s)",
                               emsg ? emsg : "?", d->site ? d->site : "?");
              free (emsg);
              return 0;
            }
          free (emsg);
          dot = v;
          st->dot = v;
        }
      for (size_t i = 0; i < p->nsyms; i++)
        {
          ccwld_sym *sy = &p->syms[i];
          if (sy->sec_idx != sec_idx || sy->seq != round || sy->resolved)
            continue;
          if (!eval_sym (st, sy, dot, e))
            return 0;
        }
    }
  return 1;
}

/* --- the layout phase --- */

int
ccwld_phase_layout (ccwld_state *st, ccwld_error *e)
{
  ccwld_plan *p = st->plan;

  p->resolve_sym = layout_resolver;
  p->state = st;
  st->sec_laid = calloc (p->nsecs ? p->nsecs : 1, sizeof (int));
  if (!st->sec_laid)
    {
      ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
      return 0;
    }

  int is_reloc = p->output.kind && !strcmp (p->output.kind, "reloc");
  /*
   * Reserve the ELF header and program-header table in the first page.
   * Default executable sections therefore begin at the first byte after
   * those headers, keeping file and virtual addresses page-congruent.
   */
  uint64_t dot = is_reloc ? 0 : 0x400080;
  st->dot = dot;

  for (size_t i = 0; i < p->nsecs; i++)
    {
      ccwld_sec *sec = &p->secs[i];
      const ccwld_mem *region = NULL;
      if (sec->region)
        {
          for (size_t j = 0; j < p->nmems; j++)
            if (p->mems[j].name && !strcmp (p->mems[j].name, sec->region))
              {
                region = &p->mems[j];
                break;
              }
        }

      /* start address */
      if (sec->vma_expr)
        {
          char *emsg = NULL;
          uint64_t v = 0;
          if (!ccwld_expr_eval (sec->vma_expr, p, dot, &v, &emsg))
            {
              ccwld_error_set (e, CCWLD_EXIT_LINK,
                               "address expression for section '%s' failed: "
                               "%s",
                               sec->name, emsg ? emsg : "?");
              free (emsg);
              return 0;
            }
          free (emsg);
          dot = align_up (v, sec->align);
        }
      else if (region)
        {
          if (dot < region->origin)
            dot = region->origin;
          dot = align_up (dot, sec->align);
        }
      else
        dot = align_up (dot, sec->align);

      sec->vma = dot;

      /* place matched input sections in selector order */
      for (size_t j = 0; j < sec->nsels; j++)
        {
          const ccwld_sel *sel = &sec->sels[j];
          for (size_t oi = 0; oi < st->nobjs; oi++)
            {
              ccwld_obj *o = &st->objs[oi];
              if (o->is_lto || (o->is_dso && !o->used))
                continue;
              for (size_t k = 0; k < o->nsecs; k++)
                {
                  ccwld_isec *is = &o->secs[k];
                  if (!is->live || is->placed || !(is->flags & 0x2))
                    continue;
                  if (!ccwld_glob_match (sel->file_glob ? sel->file_glob : "*",
                                         o->path))
                    continue;
                  int matched = 0;
                  for (size_t g = 0; g < sel->nglobs; g++)
                    if (ccwld_glob_match (sel->globs[g], is->name))
                      {
                        matched = 1;
                        break;
                      }
                  if (!matched)
                    continue;
                  uint64_t sub = sec->subalign ? sec->subalign : is->align;
                  uint64_t off = align_up (dot - sec->vma, sub);
                  is->out_sec = (int)i;
                  is->out_off = off;
                  is->placed = 1;
                  dot = sec->vma + off + is->size;
                }
            }
        }

      sec->size = dot - sec->vma;

      /* load address: AT(expr) evaluated against the vma (§2.3);
       * AT>region uses a per-region LMA cursor */
      if (sec->at_expr)
        {
          char *emsg = NULL;
          uint64_t v = 0;
          if (!ccwld_expr_eval (sec->at_expr, p, sec->vma, &v, &emsg))
            {
              ccwld_error_set (e, CCWLD_EXIT_LINK,
                               "load-address expression for section '%s' "
                               "failed: %s",
                               sec->name, emsg ? emsg : "?");
              free (emsg);
              return 0;
            }
          free (emsg);
          sec->lma = v;
        }
      else if (sec->at_region)
        {
          const ccwld_mem *at = NULL;
          for (size_t j = 0; j < p->nmems; j++)
            if (p->mems[j].name && !strcmp (p->mems[j].name, sec->at_region))
              {
                at = &p->mems[j];
                break;
              }
          size_t li = 0;
          int found = 0;
          for (; li < st->nlma; li++)
            if (!strcmp (st->lma_regions[li], sec->at_region))
              {
                found = 1;
                break;
              }
          if (!found)
            {
              char **lr = realloc (st->lma_regions,
                                   (st->nlma + 1) * sizeof (*lr));
              uint64_t *ld = realloc (st->lma_dots,
                                      (st->nlma + 1) * sizeof (*ld));
              if (!lr || !ld)
                {
                  free (lr);
                  free (ld);
                  ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
                  return 0;
                }
              st->lma_regions = lr;
              st->lma_dots = ld;
              st->lma_regions[st->nlma] = strdup (sec->at_region);
              st->lma_dots[st->nlma] = at ? at->origin : 0;
              li = st->nlma++;
            }
          uint64_t lma = align_up (st->lma_dots[li], sec->align);
          sec->lma = lma;
          st->lma_dots[li] = lma + sec->size;
        }
      else
        sec->lma = sec->vma;

      /* region overflow is fatal (never silently truncated) */
      if (region && sec->vma + sec->size > region->origin + region->length)
        {
          ccwld_error_set (e, CCWLD_EXIT_LINK,
                           "section '%s' (0x%" PRIx64 "..0x%" PRIx64
                           ") overflows region '%s' "
                           "(origin 0x%" PRIx64 ", length 0x%" PRIx64 ")",
                           sec->name, sec->vma, sec->vma + sec->size,
                           region->name, region->origin, region->length);
          return 0;
        }

      st->sec_laid[i] = 1;
      st->dot = dot;

      /* input symbols defined in this section become concrete */
      refresh_symbols_for_section (st, (int)i);

      /* section-scoped statements run at end-of-section dot */
      if (!run_statements (st, (int)i, dot, e))
        return 0;
      dot = st->dot;
    }

  /* finalize remaining input symbols (sections without statements) */
  for (size_t i = 0; i < p->nsecs; i++)
    refresh_symbols_for_section (st, (int)i);

  /* top-level statements in plan order */
  if (!run_statements (st, -1, st->dot, e))
    return 0;

  /* PROVIDE symbols that were never referenced stay undefined */
  for (size_t i = 0; i < st->nrsyms; i++)
    {
      ccwld_rsym *r = &st->rsyms[i];
      if (r->defined && !r->value_known && !r->from_script)
        {
          ccwld_error_set (e, CCWLD_EXIT_LINK,
                           "internal: symbol '%s' defined but never valued",
                           r->name);
          return 0;
        }
    }

  /* entry value */
  if (p->output.entry && p->output.entry[0])
    {
      for (size_t i = 0; i < st->nrsyms; i++)
        if (!strcmp (st->rsyms[i].name, p->output.entry)
            && st->rsyms[i].value_known)
          {
            st->entry_value = st->rsyms[i].value;
            st->entry_known = 1;
            break;
          }
    }

  /* D-0043: unplaced alloc sections are fatal, never silently
   * appended (link-stage analogue of ccwas D-0033). */
  for (size_t oi = 0; oi < st->nobjs; oi++)
    {
      ccwld_obj *o = &st->objs[oi];
      if (o->is_lto || (o->is_dso && !o->used))
        continue;
      for (size_t k = 0; k < o->nsecs; k++)
        {
          ccwld_isec *is = &o->secs[k];
          if (!is->live || is->placed || !(is->flags & 0x2))
            continue;
          ccwld_error_set (e, CCWLD_EXIT_LINK,
                           "unplaced section '%s' from '%s' (no matching "
                           "selector; place it explicitly or run with "
                           "--gc-sections)",
                           is->name, o->path);
          return 0;
        }
    }
  return 1;
}
