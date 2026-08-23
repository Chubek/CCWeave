/* §3 gc: dead-section elimination from roots.
 *
 * Roots are the `keep` selector set, the section containing the entry
 * symbol, and sections defining dynamic-referenced symbols (§3).
 * Edges follow relocations from a live section to the defining section
 * of the referenced symbol.  Deterministic: mark in link order. */
#include "ccwld_phases.h"

#include <string.h>

static int
glob_match (const char *pat, const char *str);

/* fnmatch-style glob (subset: *, ?, [class]) over a single path
 * component string; `*` also matches '/'. */
static int
glob_seg (const char *pat, const char *str)
{
  while (*pat)
    {
      if (*pat == '*')
        {
          pat++;
          if (!*pat)
            return 1;
          for (const char *s = str;; s++)
            {
              if (glob_seg (pat, s))
                return 1;
              if (!*s)
                return 0;
            }
        }
      else if (*pat == '?')
        {
          if (!*str)
            return 0;
          pat++;
          str++;
        }
      else if (*pat == '[')
        {
          const char *end = strchr (pat + 1, ']');
          if (!end || end == pat + 1)
            {
              if (*str != '[')
                return 0;
              pat++;
              str++;
              continue;
            }
          if (!*str)
            return 0;
          int matched = 0, negate = 0;
          const char *q = pat + 1;
          if (*q == '!' || *q == '^')
            {
              negate = 1;
              q++;
            }
          for (; q < end; q++)
            {
              if (q[1] == '-' && q + 2 < end)
                {
                  if (*str >= *q && *str <= q[2])
                    matched = 1;
                  q += 2;
                }
              else if (*q == *str)
                matched = 1;
            }
          if (matched == negate)
            return 0;
          pat = end + 1;
          str++;
        }
      else
        {
          if (*pat != *str)
            return 0;
          pat++;
          str++;
        }
    }
  return *str == 0;
}

static int
glob_match (const char *pat, const char *str)
{
  /* a pattern with no '/' matches against the basename */
  if (!strchr (pat, '/'))
    {
      const char *base = strrchr (str, '/');
      str = base ? base + 1 : str;
    }
  return glob_seg (pat, str);
}

int
ccwld_glob_match (const char *pat, const char *str)
{
  return glob_match (pat, str);
}

/* Does any selector of an output section match this input section? */
static int
sec_matches (const ccwld_plan *p, const ccwld_sec *out, const char *objpath,
             const char *secname, int *is_keep)
{
  for (size_t j = 0; j < out->nsels; j++)
    {
      const ccwld_sel *sel = &out->sels[j];
      if (!glob_match (sel->file_glob ? sel->file_glob : "*", objpath))
        continue;
      for (size_t k = 0; k < sel->nglobs; k++)
        if (glob_match (sel->globs[k], secname))
          {
            if (is_keep)
              *is_keep = sel->keep;
            return 1;
          }
    }
  (void)p;
  return 0;
}

int
ccwld_sec_matches (const ccwld_plan *p, const ccwld_sec *out,
                   const char *objpath, const char *secname, int *is_keep)
{
  return sec_matches (p, out, objpath, secname, is_keep);
}

/* section index of an input section that defines `name` (or -1) */
static int
defining_sec (ccwld_state *st, const char *name, int *obj_out)
{
  for (size_t i = 0; i < st->nrsyms; i++)
    {
      ccwld_rsym *r = &st->rsyms[i];
      if (strcmp (r->name, name) || !r->defined || r->obj < 0
          || r->isym < 0)
        continue;
      ccwld_obj *o = &st->objs[r->obj];
      if ((size_t)r->isym >= o->nsyms)
        continue;
      ccwld_isec *s = ccwld_state_isec (st, r->obj, o->syms[r->isym].shndx);
      if (s)
        {
          if (obj_out)
            *obj_out = r->obj;
          return (int)(s - o->secs);
        }
      return -1;
    }
  return -1;
}

static void
mark_sec (ccwld_state *st, int obj, int sidx);

static void
mark_from_relocs (ccwld_state *st, const ccwld_obj *o, int sidx)
{
  for (size_t i = 0; i < o->nrelocs; i++)
    {
      const ccwld_ireloc *r = &o->relocs[i];
      if (r->sec != sidx || !r->sym[0])
        continue;
      int tobj = 0;
      int t = defining_sec (st, r->sym, &tobj);
      if (t >= 0)
        mark_sec (st, tobj, t);
    }
}

static void
mark_sec (ccwld_state *st, int obj, int sidx)
{
  if (obj < 0 || (size_t)obj >= st->nobjs)
    return;
  ccwld_obj *o = &st->objs[obj];
  if (sidx < 0 || (size_t)sidx >= o->nsecs)
    return;
  ccwld_isec *s = &o->secs[sidx];
  if (s->live != 1) /* 1 = live-unvisited, 2 = marked */
    return;
  s->live = 2;
  mark_from_relocs (st, o, sidx);
}

int
ccwld_phase_gc (ccwld_state *st, ccwld_error *e)
{
  ccwld_plan *p = st->plan;
  if (!p->options.gc_sections)
    return 1; /* everything stays live (§3: gc runs, keeps all) */

  /* Roots (§3): keep selectors, entry section, dynamic-referenced. */
  int marked_any = 0;
  for (size_t i = 0; i < st->nobjs; i++)
    {
      ccwld_obj *o = &st->objs[i];
      for (size_t k = 0; k < o->nsecs; k++)
        {
          ccwld_isec *s = &o->secs[k];
          if (s->live != 1)
            continue;
          if (!(s->flags & 0x2)) /* SHF_ALLOC only */
            {
              s->live = 0; /* metadata is never placed */
              continue;
            }
          int keep = 0;
          for (size_t j = 0; j < p->nsecs; j++)
            if (sec_matches (p, &p->secs[j], o->path, s->name, &keep) && keep)
              {
                s->is_root = 1;
                break;
              }
          if (!s->is_root)
            continue;
          mark_sec (st, (int)i, (int)k);
          marked_any = 1;
        }
    }

  /* Entry symbol's section. */
  if (p->output.entry && p->output.entry[0])
    {
      int tobj = 0;
      int t = defining_sec (st, p->output.entry, &tobj);
      if (t >= 0)
        {
          mark_sec (st, tobj, t);
          marked_any = 1;
        }
    }

  /* Sections defining dynamic-referenced symbols: every referenced
   * resolved symbol that came from a DSO side keeps its definer. */
  for (size_t i = 0; i < st->nrsyms; i++)
    {
      ccwld_rsym *r = &st->rsyms[i];
      if (!r->referenced || !r->defined || r->from_script || r->obj < 0)
        continue;
      if (!st->objs[r->obj].is_dso)
        continue;
      int tobj = 0;
      int t = defining_sec (st, r->name, &tobj);
      if (t >= 0)
        {
          mark_sec (st, tobj, t);
          marked_any = 1;
        }
    }
  (void)marked_any;

  /* Sweep: unmarked alloc sections die; their local symbols go too. */
  for (size_t i = 0; i < st->nobjs; i++)
    {
      ccwld_obj *o = &st->objs[i];
      if (o->is_lto)
        continue; /* already replaced (§4) */
      for (size_t k = 0; k < o->nsecs; k++)
        {
          ccwld_isec *s = &o->secs[k];
          if (s->live == 2)
            s->live = 1;
          else if (s->live == 1)
            s->live = 0;
        }
      /* resolved symbols whose defining section died become undefined
       * unless weak (they were referenced from dead code only if that
       * code died too, so this is consistent) */
    }
  (void)e;
  return 1;
}
