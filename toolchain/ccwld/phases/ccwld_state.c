/* Shared pipeline state: lifecycle, diagnostics (§9), resolved-symbol
 * interning, relocation stats, mutation records (§5 conflict
 * authority), and input lookup. */
#include "ccwld_phases.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* --- lifecycle --- */

ccwld_state *
ccwld_state_new (ccwld_plan *p)
{
  ccwld_state *st = calloc (1, sizeof (*st));
  if (!st)
    return NULL;
  st->plan = p;
  return st;
}

static void
free_obj (ccwld_obj *o)
{
  free (o->path);
  free (o->kind);
  free (o->format);
  free (o->raw);
  for (size_t i = 0; i < o->nsecs; i++)
    {
      free (o->secs[i].name);
      free (o->secs[i].data);
    }
  free (o->secs);
  for (size_t i = 0; i < o->nsyms; i++)
    free (o->syms[i].name);
  free (o->syms);
  for (size_t i = 0; i < o->nrelocs; i++)
    free (o->relocs[i].sym);
  free (o->relocs);
  /* member data aliases o->raw; only names are owned */
  for (size_t i = 0; i < o->nmembers; i++)
    free (o->members[i].name);
  free (o->members);
  for (size_t i = 0; i < o->nar_syms; i++)
    free (o->ar_syms[i]);
  free (o->ar_syms);
  free (o->ar_sym_member);
}

void
ccwld_state_free (ccwld_state *st)
{
  if (!st)
    return;
  for (size_t i = 0; i < st->nobjs; i++)
    free_obj (&st->objs[i]);
  free (st->objs);
  for (size_t i = 0; i < st->nrsyms; i++)
    {
      free (st->rsyms[i].name);
      free (st->rsyms[i].binding);
      free (st->rsyms[i].visibility);
    }
  free (st->rsyms);
  free (st->stats);
  for (size_t i = 0; i < st->nnotes; i++)
    {
      free (st->notes[i].key);
      free (st->notes[i].value);
    }
  free (st->notes);
  free (st->muts);
  free (st->diags);
  free (st->pending);
  for (size_t i = 0; i < st->nlma; i++)
    free (st->lma_regions[i]);
  free (st->lma_regions);
  free (st->lma_dots);
  free (st->lto_backend_used);
  free (st);
}

/* --- diagnostics: one stream, one shape (§9) --- */

static void
diag_push (ccwld_state *st, int code, int is_error, const char *node,
           const char *site, const char *fmt, va_list ap)
{
  /* errors raised while a hook/plugin callback runs are buffered so the
   * pipeline driver can attribute them to the callback */
  ccwld_diag **arr = st->in_callback ? &st->pending : &st->diags;
  size_t *n = st->in_callback ? &st->npending : &st->ndiags;
  ccwld_diag *d = realloc (*arr, (*n + 1) * sizeof (**arr));
  if (!d)
    return;
  *arr = d;
  ccwld_diag *dg = &d[*n];
  memset (dg, 0, sizeof (*dg));
  dg->code = code;
  dg->is_error = is_error;
  vsnprintf (dg->message, sizeof (dg->message), fmt, ap);
  snprintf (dg->node, sizeof (dg->node), "%s", node ? node : "");
  snprintf (dg->site, sizeof (dg->site), "%s", site ? site : "");
  (*n)++;
}

void
ccwld_diag_error (ccwld_state *st, int code, const char *node,
                  const char *site, const char *fmt, ...)
{
  if (!st)
    return;
  va_list ap;
  va_start (ap, fmt);
  diag_push (st, code, 1, node, site, fmt, ap);
  va_end (ap);
}

void
ccwld_diag_warn (ccwld_state *st, const char *node, const char *site,
                 const char *fmt, ...)
{
  if (!st)
    return;
  va_list ap;
  va_start (ap, fmt);
  diag_push (st, CCWLD_EXIT_OK, 0, node, site, fmt, ap);
  va_end (ap);
}

void
ccwld_diag_flush_pending (ccwld_state *st)
{
  for (size_t i = 0; i < st->npending; i++)
    {
      ccwld_diag *d = realloc (st->diags, (st->ndiags + 1) * sizeof (*d));
      if (!d)
        break;
      st->diags = d;
      st->diags[st->ndiags++] = st->pending[i];
    }
  st->npending = 0;
}

void
ccwld_diag_print (const ccwld_state *st, FILE *out)
{
  if (!st || !out)
    return;
  for (size_t i = 0; i < st->ndiags; i++)
    {
      const ccwld_diag *d = &st->diags[i];
      fprintf (out, "ccwld: %s: %s", d->is_error ? "error" : "warning",
               d->message);
      if (d->node[0])
        fprintf (out, " [plan: %s]", d->node);
      if (d->site[0])
        fprintf (out, " [site: %s]", d->site);
      fprintf (out, "\n");
    }
}

/* --- resolved symbols --- */

ccwld_rsym *
ccwld_state_rsym (ccwld_state *st, const char *name)
{
  for (size_t i = 0; i < st->nrsyms; i++)
    if (!strcmp (st->rsyms[i].name, name))
      return &st->rsyms[i];
  ccwld_rsym *r = realloc (st->rsyms, (st->nrsyms + 1) * sizeof (*r));
  if (!r)
    return NULL;
  st->rsyms = r;
  ccwld_rsym *s = &st->rsyms[st->nrsyms];
  memset (s, 0, sizeof (*s));
  s->name = strdup (name);
  s->obj = -1;
  s->isym = -1;
  s->script_idx = -1;
  s->binding = strdup ("global");
  s->visibility = strdup ("default");
  if (!s->name || !s->binding || !s->visibility)
    return NULL;
  st->nrsyms++;
  return s;
}

ccwld_isec *
ccwld_state_isec (ccwld_state *st, int obj, int shndx)
{
  if (!st || obj < 0 || (size_t)obj >= st->nobjs)
    return NULL;
  ccwld_obj *o = &st->objs[obj];
  /* isym.shndx is 1-based within isecs (0 = undefined) */
  int idx = shndx - 1;
  if (idx < 0 || (size_t)idx >= o->nsecs)
    return NULL;
  return &o->secs[idx];
}

void
ccwld_state_reset_resolution (ccwld_state *st)
{
  for (size_t i = 0; i < st->nrsyms; i++)
    {
      free (st->rsyms[i].name);
      free (st->rsyms[i].binding);
      free (st->rsyms[i].visibility);
    }
  st->nrsyms = 0;
  st->undefined_strong = 0;
}

/* --- relocation stats --- */

void
ccwld_state_record_stat (ccwld_state *st, const char *name)
{
  for (size_t i = 0; i < st->nstats; i++)
    if (!strcmp (st->stats[i].name, name))
      {
        st->stats[i].count++;
        return;
      }
  ccwld_stat *s = realloc (st->stats, (st->nstats + 1) * sizeof (*s));
  if (!s)
    return;
  st->stats = s;
  snprintf (st->stats[st->nstats].name, sizeof (st->stats[0].name), "%s",
            name);
  st->stats[st->nstats].count = 1;
  st->nstats++;
}

/* --- mutation records (conflict authority, §5) --- */

int
ccwld_state_record_mut (ccwld_state *st, const char *key, int src, int phase)
{
  for (size_t i = 0; i < st->nmuts; i++)
    {
      if (st->muts[i].phase == phase && !strcmp (st->muts[i].key, key))
        return st->muts[i].src == src;
    }
  ccwld_mut *m = realloc (st->muts, (st->nmuts + 1) * sizeof (*m));
  if (!m)
    return 0;
  st->muts = m;
  snprintf (st->muts[st->nmuts].key, sizeof (st->muts[0].key), "%s", key);
  st->muts[st->nmuts].src = src;
  st->muts[st->nmuts].phase = phase;
  st->nmuts++;
  return 1;
}

void
ccwld_state_conflict_scan (ccwld_state *st, int phase)
{
  /* Deterministic order (§5): plugins in registration order, then hooks
   * in registration order; a collision is reported, never silent. */
  for (size_t i = 0; i < st->nmuts; i++)
    {
      if (st->muts[i].phase != phase || st->muts[i].src != CCWLD_MUT_PLUGIN)
        continue;
      for (size_t j = 0; j < st->nmuts; j++)
        {
          if (st->muts[j].phase != phase || st->muts[j].src != CCWLD_MUT_HOOK)
            continue;
          if (!strcmp (st->muts[i].key, st->muts[j].key))
            {
              ccwld_diag_warn (st, NULL, NULL,
                               "plugin and hook both mutated '%s' at phase "
                               "%d; plugins ran first (deterministic order)",
                               st->muts[i].key, phase);
              return;
            }
        }
    }
}

/* --- relocation names --- */

const char *
ccwld_reloc_name (int machine, uint32_t type)
{
  switch (machine)
    {
    case 62: /* EM_X86_64 */
      switch (type)
        {
        case 1: return "R_X86_64_64";
        case 2: return "R_X86_64_PC32";
        case 10: return "R_X86_64_32";
        case 11: return "R_X86_64_32S";
        case 13: return "R_X86_64_PC16";
        case 14: return "R_X86_64_PC8";
        case 41: return "R_X86_64_PLT32";
        default: break;
        }
      break;
    case 183: /* EM_AARCH64 */
      switch (type)
        {
        case 257: return "R_AARCH64_ABS64";
        case 275: return "R_AARCH64_ADR_PREL_PG_HI21";
        case 277: return "R_AARCH64_ADD_ABS_LO12_NC";
        case 283: return "R_AARCH64_CALL26";
        default: break;
        }
      break;
    case 243: /* EM_RISCV */
      switch (type)
        {
        case 2: return "R_RISCV_64";
        case 23: return "R_RISCV_PCREL_HI20";
        case 24: return "R_RISCV_PCREL_LO12_I";
        case 25: return "R_RISCV_PCREL_LO12_S";
        default: break;
        }
      break;
    default:
      break;
    }
  static char buf[32];
  snprintf (buf, sizeof (buf), "R_%u", (unsigned)type);
  return buf;
}

/* --- input lookup honoring search paths --- */

static int
file_readable (const char *path)
{
  FILE *f = fopen (path, "rb");
  if (!f)
    return 0;
  fclose (f);
  return 1;
}

char *
ccwld_find_input (ccwld_state *st, const char *path)
{
  if (!path)
    return NULL;
  if (file_readable (path))
    return strdup (path);
  if (!st || !st->plan)
    return NULL;
  for (size_t i = 0; i < st->plan->npaths; i++)
    {
      const char *dir = st->plan->paths[i];
      size_t n = strlen (dir) + strlen (path) + 2;
      char *cand = malloc (n);
      if (!cand)
        return NULL;
      snprintf (cand, n, "%s/%s", dir, path);
      if (file_readable (cand))
        return cand;
      free (cand);
    }
  return NULL;
}
