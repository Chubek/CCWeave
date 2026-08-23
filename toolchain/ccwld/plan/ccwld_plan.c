/* §2: link-plan IR implementation — sealed, serializable, canonical.
 *
 * Both frontends lower to this single IR.  Once sealed the plan is
 * read-only except through phase hooks within their mutability scope. */
#include "ccwld_plan.h"
#include "../expr/ccwld_expr.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- helpers --- */

static char *
dupstr (const char *s)
{
  return s ? strdup (s) : NULL;
}

static int
grow (void **p, size_t *cap, size_t n, size_t sz)
{
  if (n <= *cap)
    return 1;
  size_t c = *cap ? *cap * 2 : 8;
  while (c < n)
    c *= 2;
  void *q = realloc (*p, c * sz);
  if (!q)
    return 0;
  *p = q;
  *cap = c;
  return 1;
}

static int
valid_format (const char *f)
{
  return !f || !strcmp (f, "elf") || !strcmp (f, "pe") || !strcmp (f, "macho")
         || !strcmp (f, "wasm");
}

static int
valid_kind (const char *k)
{
  return k
         && (!strcmp (k, "exe") || !strcmp (k, "dso") || !strcmp (k, "reloc")
             || !strcmp (k, "pie"));
}

static int
is_sealed (const ccwld_plan *p, ccwld_error *e)
{
  if (!p || p->sealed)
    {
      if (e)
        {
          e->code = 1;
          snprintf (e->message, sizeof (e->message),
                    "plan is sealed or invalid");
        }
      return 1;
    }
  return 0;
}

static int
oom (ccwld_error *e)
{
  if (e)
    {
      e->code = CCWLD_EXIT_INTERNAL;
      snprintf (e->message, sizeof (e->message), "out of memory");
    }
  return 0;
}

/* --- lifecycle --- */

ccwld_plan *
ccwld_plan_new (const char *target)
{
  ccwld_plan *p = calloc (1, sizeof (*p));
  if (!p)
    return NULL;
  p->target = dupstr (target ? target : "unknown");
  p->sealed = false;
  p->reproducible = true;
  p->options.reproducible = 1;
  p->frontend = dupstr ("api");
  return p;
}

void
ccwld_plan_free (ccwld_plan *p)
{
  if (!p)
    return;
  free (p->target);
  free (p->serialized);
  free (p->frontend);
  free (p->options.cache_dir);
  free (p->options.out_name);

  /* output */
  free ((void *)p->output.kind);
  free ((void *)p->output.format);
  free ((void *)p->output.entry);
  free ((void *)p->output.soname);
  free ((void *)p->output.osabi);

  /* inputs */
  for (size_t i = 0; i < p->ninputs; i++)
    free (p->inputs[i].path);
  free (p->inputs);

  /* paths */
  for (size_t i = 0; i < p->npaths; i++)
    free (p->paths[i]);
  free (p->paths);

  /* memory regions */
  for (size_t i = 0; i < p->nmems; i++)
    {
      free (p->mems[i].name);
      free (p->mems[i].attrs);
    }
  free (p->mems);

  /* sections */
  for (size_t i = 0; i < p->nsecs; i++)
    {
      free (p->secs[i].name);
      free (p->secs[i].region);
      free (p->secs[i].at_region);
      free (p->secs[i].phdr);
      for (size_t j = 0; j < p->secs[i].nsels; j++)
        {
          free (p->secs[i].sels[j].file_glob);
          for (size_t k = 0; k < p->secs[i].sels[j].nglobs; k++)
            free (p->secs[i].sels[j].globs[k]);
          free (p->secs[i].sels[j].globs);
        }
      free (p->secs[i].sels);
      ccwld_expr_free (p->secs[i].vma_expr);
      ccwld_expr_free (p->secs[i].at_expr);
      ccwld_expr_free (p->secs[i].fill);
    }
  free (p->secs);

  /* symbols / dotsteps / attrs */
  for (size_t i = 0; i < p->nsyms; i++)
    {
      free (p->syms[i].name);
      free (p->syms[i].visibility);
      free (p->syms[i].binding);
      free (p->syms[i].site);
      ccwld_expr_free (p->syms[i].expr);
    }
  free (p->syms);
  for (size_t i = 0; i < p->ndotsteps; i++)
    {
      ccwld_expr_free (p->dotsteps[i].expr);
      free (p->dotsteps[i].site);
    }
  free (p->dotsteps);
  for (size_t i = 0; i < p->nattrs; i++)
    {
      free (p->attrs[i].name);
      free (p->attrs[i].visibility);
      free (p->attrs[i].binding);
      free (p->attrs[i].alias);
    }
  free (p->attrs);

  /* phdrs */
  for (size_t i = 0; i < p->nphdrs; i++)
    {
      free (p->phdrs[i].name);
      free (p->phdrs[i].type);
    }
  free (p->phdrs);

  /* version */
  for (size_t i = 0; i < p->nvers; i++)
    {
      free (p->vers[i].symbol);
      free (p->vers[i].version);
    }
  free (p->vers);

  /* env */
  for (size_t i = 0; i < p->nenv; i++)
    {
      free (p->env_keys[i]);
      free (p->env_vals[i]);
    }
  free (p->env_keys);
  free (p->env_vals);

  /* LTO */
  free (p->lto.pipeline);
  free (p->lto.cache_dir);

  /* plugins */
  for (size_t i = 0; i < p->nplugins; i++)
    {
      free (p->plugins[i].path);
      free (p->plugins[i].name);
      free (p->plugins[i].options);
    }
  free (p->plugins);

  /* hooks */
  for (size_t i = 0; i < p->nhooks; i++)
    free (p->hooks[i].site);
  free (p->hooks);

  /* frontend runtime (lccwld's Lua state, closed after the link) */
  if (p->frontend_ctx && p->frontend_ctx_free)
    p->frontend_ctx_free (p->frontend_ctx);
  free (p);
}

/* --- output --- */

int
ccwld_plan_output (ccwld_plan *p, const ccwld_output *o, ccwld_error *e)
{
  if (is_sealed (p, e))
    return 0;
  if (!o || !valid_kind (o->kind) || !valid_format (o->format))
    {
      if (e)
        {
          e->code = CCWLD_EXIT_USAGE;
          snprintf (e->message, sizeof (e->message),
                    "invalid output kind or format");
        }
      return 0;
    }
  if (o->soname && o->format && strcmp (o->format, "elf"))
    {
      if (e)
        {
          e->code = CCWLD_EXIT_USAGE;
          snprintf (e->message, sizeof (e->message),
                    "soname is only valid for ELF");
        }
      return 0;
    }
  free ((void *)p->output.kind);
  free ((void *)p->output.format);
  free ((void *)p->output.entry);
  free ((void *)p->output.soname);
  free ((void *)p->output.osabi);
  p->output.kind = dupstr (o->kind);
  p->output.format = dupstr (o->format ? o->format : "elf");
  p->output.entry = dupstr (o->entry);
  p->output.soname = dupstr (o->soname);
  p->output.osabi = dupstr (o->osabi);
  return 1;
}

/* --- inputs --- */

int
ccwld_plan_input (ccwld_plan *p, const char *path, int as_needed, int startup,
                  ccwld_error *e)
{
  if (is_sealed (p, e))
    return 0;
  if (!path)
    {
      if (e)
        {
          e->code = CCWLD_EXIT_LINK;
          snprintf (e->message, sizeof (e->message), "invalid input path");
        }
      return 0;
    }
  if (!grow ((void **)&p->inputs, &p->cinputs, p->ninputs + 1,
             sizeof (*p->inputs)))
    return oom (e);
  p->inputs[p->ninputs].path = dupstr (path);
  p->inputs[p->ninputs].as_needed = as_needed;
  p->inputs[p->ninputs].startup = startup;
  p->inputs[p->ninputs].is_group = 0;
  p->inputs[p->ninputs].group_start = 0;
  p->ninputs++;
  return 1;
}

int
ccwld_plan_group (ccwld_plan *p, const char **paths, size_t n, ccwld_error *e)
{
  if (is_sealed (p, e))
    return 0;
  for (size_t i = 0; i < n; i++)
    {
      if (!ccwld_plan_input (p, paths[i], 0, 0, e))
        return 0;
      p->inputs[p->ninputs - 1].is_group = 1;
      if (i == 0)
        p->inputs[p->ninputs - 1].group_start = 1;
    }
  return 1;
}

int
ccwld_plan_search_path (ccwld_plan *p, const char *path, ccwld_error *e)
{
  if (is_sealed (p, e))
    return 0;
  if (!path)
    {
      if (e)
        {
          e->code = CCWLD_EXIT_LINK;
          snprintf (e->message, sizeof (e->message), "invalid search path");
        }
      return 0;
    }
  if (!grow ((void **)&p->paths, &p->cpaths, p->npaths + 1,
             sizeof (*p->paths)))
    return oom (e);
  p->paths[p->npaths++] = dupstr (path);
  return 1;
}

/* --- memory --- */

int
ccwld_plan_memory (ccwld_plan *p, const char *name, const char *attrs,
                   uint64_t origin, uint64_t length, ccwld_error *e)
{
  if (is_sealed (p, e))
    return 0;
  if (!name || !length)
    {
      if (e)
        {
          e->code = CCWLD_EXIT_LINK;
          snprintf (e->message, sizeof (e->message), "invalid memory region");
        }
      return 0;
    }
  if (!grow ((void **)&p->mems, &p->cmems, p->nmems + 1, sizeof (*p->mems)))
    return oom (e);
  p->mems[p->nmems].name = dupstr (name);
  p->mems[p->nmems].attrs = dupstr (attrs ? attrs : "");
  p->mems[p->nmems].origin = origin;
  p->mems[p->nmems].length = length;
  p->nmems++;
  return 1;
}

/* --- sections --- */

static int
section_split_selector (ccwld_sec *s, const char *list, int keep)
{
  /* Split a whitespace-separated glob list into one selector with
   * file glob "*" (legacy string API of ccwld_plan_section[_full]). */
  if (!list || !list[0])
    return 1;
  ccwld_sel *sels = realloc (s->sels, (s->nsels + 1) * sizeof (*sels));
  if (!sels)
    return 0;
  s->sels = sels;
  ccwld_sel *sel = &s->sels[s->nsels];
  memset (sel, 0, sizeof (*sel));
  sel->keep = keep;
  sel->file_glob = dupstr ("*");

  const char *p = list;
  while (*p)
    {
      while (*p == ' ' || *p == '\t')
        p++;
      if (!*p)
        break;
      const char *st = p;
      while (*p && *p != ' ' && *p != '\t')
        p++;
      char **g = realloc (sel->globs, (sel->nglobs + 1) * sizeof (*g));
      if (!g)
        return 0;
      sel->globs = g;
      size_t n = (size_t)(p - st);
      char *dup = malloc (n + 1);
      if (!dup)
        return 0;
      memcpy (dup, st, n);
      dup[n] = 0;
      sel->globs[sel->nglobs++] = dup;
    }
  s->nsels++;
  return 1;
}

int
ccwld_plan_section (ccwld_plan *p, const char *name, const char *region,
                    uint64_t align, const char *selector,
                    const char *at_region, ccwld_error *e)
{
  return ccwld_plan_section_full (p, name, region, align, selector, NULL,
                                  at_region, NULL, e);
}

int
ccwld_plan_section_full (ccwld_plan *p, const char *name, const char *region,
                         uint64_t align, const char *selector,
                         const char *keep, const char *at_region,
                         ccwld_expr *fill, ccwld_error *e)
{
  if (is_sealed (p, e))
    return 0;
  if (!name)
    {
      if (e)
        {
          e->code = CCWLD_EXIT_LINK;
          snprintf (e->message, sizeof (e->message), "invalid section name");
        }
      return 0;
    }
  if (!grow ((void **)&p->secs, &p->csecs, p->nsecs + 1, sizeof (*p->secs)))
    return oom (e);
  ccwld_sec *s = &p->secs[p->nsecs++];
  memset (s, 0, sizeof (*s));
  s->name = dupstr (name);
  s->region = dupstr (region);
  s->at_region = dupstr (at_region);
  s->phdr = NULL;
  s->align = align ? align : 1;
  s->subalign = 0;
  s->load = 1;
  s->fill = fill;
  if (!section_split_selector (s, selector, 0)
      || !section_split_selector (s, keep, 1))
    return oom (e);
  return 1;
}

static ccwld_sec *
last_section (ccwld_plan *p, const char *name)
{
  for (size_t i = p->nsecs; i > 0; i--)
    if (p->secs[i - 1].name && strcmp (p->secs[i - 1].name, name) == 0)
      return &p->secs[i - 1];
  return NULL;
}

int
ccwld_plan_selector (ccwld_plan *p, const char *secname,
                     const char *file_glob, char *const *globs, size_t nglobs,
                     int keep, ccwld_error *e)
{
  if (is_sealed (p, e))
    return 0;
  ccwld_sec *s = last_section (p, secname);
  if (!s)
    {
      if (e)
        {
          e->code = CCWLD_EXIT_LINK;
          snprintf (e->message, sizeof (e->message),
                    "selector for unknown output section '%s'", secname);
        }
      return 0;
    }
  ccwld_sel *sels = realloc (s->sels, (s->nsels + 1) * sizeof (*sels));
  if (!sels)
    return oom (e);
  s->sels = sels;
  ccwld_sel *sel = &s->sels[s->nsels];
  memset (sel, 0, sizeof (*sel));
  sel->keep = keep;
  sel->file_glob = dupstr (file_glob ? file_glob : "*");
  if (nglobs)
    {
      sel->globs = calloc (nglobs, sizeof (*sel->globs));
      if (!sel->globs)
        return oom (e);
      for (size_t i = 0; i < nglobs; i++)
        {
          sel->globs[i] = dupstr (globs[i]);
          if (!sel->globs[i])
            return oom (e);
        }
      sel->nglobs = nglobs;
    }
  s->nsels++;
  return 1;
}

int
ccwld_plan_section_set_vma (ccwld_plan *p, const char *secname,
                            ccwld_expr *vma, ccwld_error *e)
{
  if (is_sealed (p, e))
    return 0;
  ccwld_sec *s = last_section (p, secname);
  if (!s)
    {
      if (e)
        {
          e->code = CCWLD_EXIT_LINK;
          snprintf (e->message, sizeof (e->message),
                    "unknown output section '%s'", secname);
        }
      return 0;
    }
  ccwld_expr_free (s->vma_expr);
  s->vma_expr = vma;
  return 1;
}

int
ccwld_plan_section_set_at (ccwld_plan *p, const char *secname,
                           ccwld_expr *at, ccwld_error *e)
{
  if (is_sealed (p, e))
    return 0;
  ccwld_sec *s = last_section (p, secname);
  if (!s)
    {
      if (e)
        {
          e->code = CCWLD_EXIT_LINK;
          snprintf (e->message, sizeof (e->message),
                    "unknown output section '%s'", secname);
        }
      return 0;
    }
  ccwld_expr_free (s->at_expr);
  s->at_expr = at;
  return 1;
}

int
ccwld_plan_section_set_subalign (ccwld_plan *p, const char *secname,
                                 uint64_t subalign, ccwld_error *e)
{
  if (is_sealed (p, e))
    return 0;
  ccwld_sec *s = last_section (p, secname);
  if (!s)
    {
      if (e)
        {
          e->code = CCWLD_EXIT_LINK;
          snprintf (e->message, sizeof (e->message),
                    "unknown output section '%s'", secname);
        }
      return 0;
    }
  s->subalign = subalign;
  return 1;
}

int
ccwld_plan_section_set_phdr (ccwld_plan *p, const char *secname,
                             const char *phdr, ccwld_error *e)
{
  if (is_sealed (p, e))
    return 0;
  ccwld_sec *s = last_section (p, secname);
  if (!s)
    {
      if (e)
        {
          e->code = CCWLD_EXIT_LINK;
          snprintf (e->message, sizeof (e->message),
                    "unknown output section '%s'", secname);
        }
      return 0;
    }
  free (s->phdr);
  s->phdr = dupstr (phdr);
  return 1;
}

int
ccwld_plan_section_set_load (ccwld_plan *p, const char *secname, int load,
                             ccwld_error *e)
{
  if (is_sealed (p, e))
    return 0;
  ccwld_sec *s = last_section (p, secname);
  if (!s)
    {
      if (e)
        {
          e->code = CCWLD_EXIT_LINK;
          snprintf (e->message, sizeof (e->message),
                    "unknown output section '%s'", secname);
        }
      return 0;
    }
  s->load = load;
  return 1;
}

int
ccwld_plan_section_set_region (ccwld_plan *p, const char *secname,
                               const char *region, ccwld_error *e)
{
  if (is_sealed (p, e))
    return 0;
  ccwld_sec *s = last_section (p, secname);
  if (!s)
    {
      if (e)
        {
          e->code = CCWLD_EXIT_LINK;
          snprintf (e->message, sizeof (e->message),
                    "unknown output section '%s'", secname);
        }
      return 0;
    }
  free (s->region);
  s->region = dupstr (region);
  return 1;
}

int
ccwld_plan_section_set_at_region (ccwld_plan *p, const char *secname,
                                  const char *at_region, ccwld_error *e)
{
  if (is_sealed (p, e))
    return 0;
  ccwld_sec *s = last_section (p, secname);
  if (!s)
    {
      if (e)
        {
          e->code = CCWLD_EXIT_LINK;
          snprintf (e->message, sizeof (e->message),
                    "unknown output section '%s'", secname);
        }
      return 0;
    }
  free (s->at_region);
  s->at_region = dupstr (at_region);
  return 1;
}

int
ccwld_plan_section_set_fill (ccwld_plan *p, const char *secname,
                             ccwld_expr *fill, ccwld_error *e)
{
  if (is_sealed (p, e))
    return 0;
  ccwld_sec *s = last_section (p, secname);
  if (!s)
    {
      if (e)
        {
          e->code = CCWLD_EXIT_LINK;
          snprintf (e->message, sizeof (e->message),
                    "unknown output section '%s'", secname);
        }
      return 0;
    }
  ccwld_expr_free (s->fill);
  s->fill = fill;
  return 1;
}

/* --- symbols --- */

int
ccwld_plan_symbol (ccwld_plan *p, const char *name, ccwld_expr *expr,
                   int provide, int hidden, ccwld_error *e)
{
  return ccwld_plan_symbol_full (p, name, expr, provide,
                                 hidden ? "hidden" : "default", "global", e);
}

int
ccwld_plan_symbol_full (ccwld_plan *p, const char *name, ccwld_expr *expr,
                        int provide, const char *visibility,
                        const char *binding, ccwld_error *e)
{
  int hidden = visibility && !strcmp (visibility, "hidden");
  if (!ccwld_plan_symbol_at (p, name, expr, provide, hidden, -1, NULL, e))
    return 0;
  /* symbol_at filled defaults; apply the requested binding/visibility. */
  ccwld_sym *s = &p->syms[p->nsyms - 1];
  free (s->visibility);
  free (s->binding);
  s->visibility = dupstr (visibility ? visibility : "default");
  s->binding = dupstr (binding ? binding : "global");
  if (!s->visibility || !s->binding)
    return oom (e);
  return 1;
}

int
ccwld_plan_symbol_at (ccwld_plan *p, const char *name, ccwld_expr *expr,
                      int provide, int hidden, int sec_idx, const char *site,
                      ccwld_error *e)
{
  if (is_sealed (p, e))
    return 0;
  if (!name || !expr)
    {
      if (e)
        {
          e->code = CCWLD_EXIT_LINK;
          snprintf (e->message, sizeof (e->message), "invalid symbol");
        }
      return 0;
    }
  if (!grow ((void **)&p->syms, &p->csyms, p->nsyms + 1, sizeof (*p->syms)))
    return oom (e);
  ccwld_sym *s = &p->syms[p->nsyms++];
  memset (s, 0, sizeof (*s));
  s->name = dupstr (name);
  s->expr = expr;
  s->provide = provide;
  s->hidden = hidden;
  s->visibility = dupstr (hidden ? "hidden" : "default");
  s->binding = dupstr ("global");
  s->sec_idx = sec_idx;
  s->seq = p->stmt_seq++;
  s->site = dupstr (site);
  s->resolved = 0;
  return 1;
}

int
ccwld_plan_dotstep (ccwld_plan *p, ccwld_expr *expr, int sec_idx,
                    const char *site, ccwld_error *e)
{
  if (is_sealed (p, e))
    return 0;
  if (!expr)
    {
      if (e)
        {
          e->code = CCWLD_EXIT_LINK;
          snprintf (e->message, sizeof (e->message), "invalid dotstep");
        }
      return 0;
    }
  if (!grow ((void **)&p->dotsteps, &p->cdotsteps, p->ndotsteps + 1,
             sizeof (*p->dotsteps)))
    return oom (e);
  ccwld_dotstep *d = &p->dotsteps[p->ndotsteps++];
  memset (d, 0, sizeof (*d));
  d->expr = expr;
  d->sec_idx = sec_idx;
  d->seq = p->stmt_seq++;
  d->site = dupstr (site);
  return 1;
}

int
ccwld_plan_attr (ccwld_plan *p, const char *name, const char *visibility,
                 const char *binding, const char *alias, ccwld_error *e)
{
  if (is_sealed (p, e))
    return 0;
  if (!name)
    {
      if (e)
        {
          e->code = CCWLD_EXIT_LINK;
          snprintf (e->message, sizeof (e->message), "invalid symbol attr");
        }
      return 0;
    }
  if (!grow ((void **)&p->attrs, &p->cattrs, p->nattrs + 1,
             sizeof (*p->attrs)))
    return oom (e);
  ccwld_attr *a = &p->attrs[p->nattrs++];
  memset (a, 0, sizeof (*a));
  a->name = dupstr (name);
  a->visibility = dupstr (visibility);
  a->binding = dupstr (binding);
  a->alias = dupstr (alias);
  return 1;
}

/* --- phdrs --- */

int
ccwld_plan_phdr (ccwld_plan *p, const char *name, const char *type,
                 uint32_t flags, uint64_t align, ccwld_error *e)
{
  if (is_sealed (p, e))
    return 0;
  if (!name)
    {
      if (e)
        {
          e->code = CCWLD_EXIT_LINK;
          snprintf (e->message, sizeof (e->message), "invalid phdr name");
        }
      return 0;
    }
  if (!grow ((void **)&p->phdrs, &p->cphdrs, p->nphdrs + 1,
             sizeof (*p->phdrs)))
    return oom (e);
  ccwld_phdr *ph = &p->phdrs[p->nphdrs++];
  memset (ph, 0, sizeof (*ph));
  ph->name = dupstr (name);
  ph->type = dupstr (type ? type : "LOAD");
  ph->flags = flags;
  ph->align = align ? align : 4096;
  return 1;
}

/* --- version --- */

int
ccwld_plan_version (ccwld_plan *p, const char *symbol, const char *version,
                    int is_default, ccwld_error *e)
{
  if (is_sealed (p, e))
    return 0;
  if (!symbol || !version)
    {
      if (e)
        {
          e->code = CCWLD_EXIT_LINK;
          snprintf (e->message, sizeof (e->message), "invalid version node");
        }
      return 0;
    }
  if (!grow ((void **)&p->vers, &p->cvers, p->nvers + 1, sizeof (*p->vers)))
    return oom (e);
  p->vers[p->nvers].symbol = dupstr (symbol);
  p->vers[p->nvers].version = dupstr (version);
  p->vers[p->nvers].is_default = is_default;
  p->nvers++;
  return 1;
}

/* --- LTO --- */

int
ccwld_plan_lto (ccwld_plan *p, const char *pipeline, unsigned jobs,
                const char *cache_dir, ccwld_error *e)
{
  if (is_sealed (p, e))
    return 0;
  free (p->lto.pipeline);
  free (p->lto.cache_dir);
  p->lto.pipeline = dupstr (pipeline);
  p->lto.jobs = jobs ? jobs : 1;
  p->lto.cache_dir = dupstr (cache_dir);
  p->lto.enabled = 1;
  return 1;
}

/* --- plugins --- */

int
ccwld_plan_plugin (ccwld_plan *p, const char *path, const char *options,
                   ccwld_error *e)
{
  if (is_sealed (p, e))
    return 0;
  if (!path)
    {
      if (e)
        {
          e->code = CCWLD_EXIT_LINK;
          snprintf (e->message, sizeof (e->message), "invalid plugin path");
        }
      return 0;
    }
  if (!grow ((void **)&p->plugins, &p->cplugins, p->nplugins + 1,
             sizeof (*p->plugins)))
    return oom (e);
  p->plugins[p->nplugins].path = dupstr (path);
  p->plugins[p->nplugins].name = NULL;
  p->plugins[p->nplugins].options = dupstr (options);
  p->plugins[p->nplugins].loaded = 0;
  p->nplugins++;
  return 1;
}

/* --- hooks --- */

int
ccwld_plan_hook (ccwld_plan *p, ccwld_phase phase, ccwld_hook_fn fn,
                 void *user, ccwld_error *e)
{
  if (is_sealed (p, e))
    return 0;
  if (!fn)
    {
      if (e)
        {
          e->code = CCWLD_EXIT_LINK;
          snprintf (e->message, sizeof (e->message), "invalid hook function");
        }
      return 0;
    }
  if (!grow ((void **)&p->hooks, &p->chooks, p->nhooks + 1,
             sizeof (*p->hooks)))
    return oom (e);
  p->hooks[p->nhooks].phase = phase;
  p->hooks[p->nhooks].fn = fn;
  p->hooks[p->nhooks].user = user;
  p->hooks[p->nhooks].site = NULL;
  p->hooks[p->nhooks].is_lua = 0;
  p->nhooks++;
  return 1;
}

/* --- env / gensym --- */

int
ccwld_plan_env (ccwld_plan *p, const char *key, const char *val, int defsym,
                ccwld_error *e)
{
  if (is_sealed (p, e))
    return 0;
  if (!key || !val)
    {
      if (e)
        {
          e->code = CCWLD_EXIT_LINK;
          snprintf (e->message, sizeof (e->message), "invalid env binding");
        }
      return 0;
    }
  /* -D keys are unique; a repeat rebinds in place (deterministic). */
  for (size_t i = 0; i < p->nenv; i++)
    if (strcmp (p->env_keys[i], key) == 0)
      {
        free (p->env_vals[i]);
        p->env_vals[i] = dupstr (val);
        return 1;
      }
  if (!grow ((void **)&p->env_keys, &p->cenv, p->nenv + 1, sizeof (char *)))
    return oom (e);
  {
    char **nv = realloc (p->env_vals, p->cenv * sizeof (char *));
    if (!nv)
      return oom (e);
    p->env_vals = nv;
  }
  p->env_keys[p->nenv] = dupstr (key);
  p->env_vals[p->nenv] = dupstr (val);
  p->nenv++;
  return 1;
}

const char *
ccwld_plan_env_get (const ccwld_plan *p, const char *key)
{
  if (!p || !key)
    return NULL;
  for (size_t i = 0; i < p->nenv; i++)
    if (strcmp (p->env_keys[i], key) == 0)
      return p->env_vals[i];
  return NULL;
}

size_t
ccwld_plan_gensym (ccwld_plan *p, const char *prefix, char *buf,
                   size_t buflen)
{
  if (!p || !buf || buflen == 0)
    return 0;
  const char *pre = prefix ? prefix : "g";
  size_t n = (size_t)snprintf (buf, buflen, ".Lccwld_%s_%u", pre,
                               (unsigned)p->gensym + 1);
  if (n >= buflen)
    return 0;
  p->gensym++;
  return n;
}

void
ccwld_plan_set_frontend (ccwld_plan *p, const char *frontend)
{
  if (!p)
    return;
  free (p->frontend);
  p->frontend = dupstr (frontend ? frontend : "api");
}

/* --- serialization helpers --- */

#define APPEND(s, len, cap, ...)                                              \
  do                                                                          \
    {                                                                         \
      char _b[512];                                                           \
      int _z = snprintf (_b, sizeof (_b), __VA_ARGS__);                       \
      if (_z < 0)                                                             \
        break;                                                                \
      if ((len) + (size_t)_z + 1 > (cap))                                     \
        {                                                                     \
          size_t _nc = (cap) ? (cap) * 2 : 4096;                              \
          while (_nc < (len) + (size_t)_z + 1)                                \
            _nc *= 2;                                                          \
          char *_ns = realloc (s, _nc);                                       \
          if (!_ns)                                                           \
            break;                                                            \
          (s) = _ns;                                                          \
          (cap) = _nc;                                                        \
        }                                                                     \
      memcpy ((s) + (len), _b, (size_t)_z);                                   \
      (len) += (size_t)_z;                                                    \
      (s)[len] = 0;                                                           \
    }                                                                         \
  while (0)

static void
json_escape (char **s, size_t *len, size_t *cap, const char *str)
{
  if (!str)
    {
      APPEND (*s, *len, *cap, "null");
      return;
    }
  APPEND (*s, *len, *cap, "\"");
  for (const char *p = str; *p; p++)
    {
      switch (*p)
        {
        case '"':
          APPEND (*s, *len, *cap, "\\\"");
          break;
        case '\\':
          APPEND (*s, *len, *cap, "\\\\");
          break;
        case '\n':
          APPEND (*s, *len, *cap, "\\n");
          break;
        case '\r':
          APPEND (*s, *len, *cap, "\\r");
          break;
        case '\t':
          APPEND (*s, *len, *cap, "\\t");
          break;
        default:
          {
            char c[2] = { *p, 0 };
            APPEND (*s, *len, *cap, "%s", c);
          }
        }
    }
  APPEND (*s, *len, *cap, "\"");
}

/* Append an expression in a JSON string literal position. */
static void
json_expr (char **s, size_t *len, size_t *cap, const ccwld_expr *x)
{
  /* Canonical form is the expr-to-string form; escape happens to be a
   * no-op for the characters it emits, but route through json_escape
   * for safety. */
  char *tmp = NULL;
  size_t tl = 0, tc = 0;
  ccwld_expr_to_string (x, &tmp, &tl, &tc);
  json_escape (s, len, cap, tmp ? tmp : "");
  free (tmp);
}

/* --- serialization (canonical, deterministic; provenance excluded) --- */

int
ccwld_plan_serialize (const ccwld_plan *p, char **out, size_t *out_len,
                      ccwld_error *e)
{
  if (!p || !out)
    {
      if (e)
        {
          e->code = CCWLD_EXIT_LINK;
          snprintf (e->message, sizeof (e->message),
                    "invalid serialization request");
        }
      return 0;
    }

  char *s = NULL;
  size_t len = 0, cap = 0;

  APPEND (s, len, cap, "{");
  /* target */
  APPEND (s, len, cap, "\"target\":");
  json_escape (&s, &len, &cap, p->target);

  /* output */
  APPEND (s, len, cap, ",\"output\":{\"kind\":");
  json_escape (&s, &len, &cap, p->output.kind ? p->output.kind : "");
  APPEND (s, len, cap, ",\"format\":");
  json_escape (&s, &len, &cap, p->output.format ? p->output.format : "");
  APPEND (s, len, cap, ",\"entry\":");
  json_escape (&s, &len, &cap, p->output.entry ? p->output.entry : "");
  if (p->output.soname)
    {
      APPEND (s, len, cap, ",\"soname\":");
      json_escape (&s, &len, &cap, p->output.soname);
    }
  if (p->output.osabi)
    {
      APPEND (s, len, cap, ",\"osabi\":");
      json_escape (&s, &len, &cap, p->output.osabi);
    }
  APPEND (s, len, cap, "}");

  /* inputs (ordered) */
  APPEND (s, len, cap, ",\"inputs\":[");
  for (size_t i = 0; i < p->ninputs; i++)
    {
      if (i)
        APPEND (s, len, cap, ",");
      APPEND (s, len, cap, "{\"path\":");
      json_escape (&s, &len, &cap, p->inputs[i].path);
      APPEND (s, len, cap, ",\"as_needed\":%s",
              p->inputs[i].as_needed ? "true" : "false");
      APPEND (s, len, cap, ",\"startup\":%s",
              p->inputs[i].startup ? "true" : "false");
      if (p->inputs[i].is_group)
        APPEND (s, len, cap, ",\"is_group\":true");
      if (p->inputs[i].group_start)
        APPEND (s, len, cap, ",\"group_start\":true");
      APPEND (s, len, cap, "}");
    }
  APPEND (s, len, cap, "]");

  /* search paths (ordered) */
  APPEND (s, len, cap, ",\"search_paths\":[");
  for (size_t i = 0; i < p->npaths; i++)
    {
      if (i)
        APPEND (s, len, cap, ",");
      json_escape (&s, &len, &cap, p->paths[i]);
    }
  APPEND (s, len, cap, "]");

  /* memory (ordered) */
  APPEND (s, len, cap, ",\"memory\":[");
  for (size_t i = 0; i < p->nmems; i++)
    {
      if (i)
        APPEND (s, len, cap, ",");
      APPEND (s, len, cap, "{\"name\":");
      json_escape (&s, &len, &cap, p->mems[i].name);
      APPEND (s, len, cap, ",\"attrs\":");
      json_escape (&s, &len, &cap, p->mems[i].attrs);
      APPEND (s, len, cap, ",\"origin\":%" PRIu64, p->mems[i].origin);
      APPEND (s, len, cap, ",\"length\":%" PRIu64, p->mems[i].length);
      APPEND (s, len, cap, "}");
    }
  APPEND (s, len, cap, "]");

  /* sections (ordered, with structured input selectors) */
  APPEND (s, len, cap, ",\"sections\":[");
  for (size_t i = 0; i < p->nsecs; i++)
    {
      const ccwld_sec *sec = &p->secs[i];
      if (i)
        APPEND (s, len, cap, ",");
      APPEND (s, len, cap, "{\"name\":");
      json_escape (&s, &len, &cap, sec->name);
      if (sec->region)
        {
          APPEND (s, len, cap, ",\"region\":");
          json_escape (&s, &len, &cap, sec->region);
        }
      if (sec->at_region)
        {
          APPEND (s, len, cap, ",\"at_region\":");
          json_escape (&s, &len, &cap, sec->at_region);
        }
      if (sec->phdr)
        {
          APPEND (s, len, cap, ",\"phdr\":");
          json_escape (&s, &len, &cap, sec->phdr);
        }
      APPEND (s, len, cap, ",\"align\":%" PRIu64, sec->align);
      if (sec->subalign)
        APPEND (s, len, cap, ",\"subalign\":%" PRIu64, sec->subalign);
      if (sec->vma_expr)
        {
          APPEND (s, len, cap, ",\"vma\":\"");
          ccwld_expr_to_string (sec->vma_expr, &s, &len, &cap);
          APPEND (s, len, cap, "\"");
        }
      if (sec->at_expr)
        {
          APPEND (s, len, cap, ",\"at\":\"");
          ccwld_expr_to_string (sec->at_expr, &s, &len, &cap);
          APPEND (s, len, cap, "\"");
        }
      if (sec->fill)
        {
          APPEND (s, len, cap, ",\"fill\":\"");
          ccwld_expr_to_string (sec->fill, &s, &len, &cap);
          APPEND (s, len, cap, "\"");
        }
      APPEND (s, len, cap, ",\"load\":%s", sec->load ? "true" : "false");
      APPEND (s, len, cap, ",\"input\":[");
      for (size_t j = 0; j < sec->nsels; j++)
        {
          const ccwld_sel *sel = &sec->sels[j];
          if (j)
            APPEND (s, len, cap, ",");
          APPEND (s, len, cap, "{\"file\":");
          json_escape (&s, &len, &cap,
                       sel->file_glob ? sel->file_glob : "*");
          APPEND (s, len, cap, ",\"keep\":%s", sel->keep ? "true" : "false");
          APPEND (s, len, cap, ",\"secs\":[");
          for (size_t k = 0; k < sel->nglobs; k++)
            {
              if (k)
                APPEND (s, len, cap, ",");
              json_escape (&s, &len, &cap, sel->globs[k]);
            }
          APPEND (s, len, cap, "]}");
        }
      APPEND (s, len, cap, "]}");
    }
  APPEND (s, len, cap, "]");

  /* symbols (ordered; inline section assignments carry sec_idx) */
  APPEND (s, len, cap, ",\"symbols\":[");
  for (size_t i = 0; i < p->nsyms; i++)
    {
      const ccwld_sym *sy = &p->syms[i];
      if (i)
        APPEND (s, len, cap, ",");
      APPEND (s, len, cap, "{\"name\":");
      json_escape (&s, &len, &cap, sy->name);
      APPEND (s, len, cap, ",\"provide\":%s", sy->provide ? "true" : "false");
      APPEND (s, len, cap, ",\"hidden\":%s", sy->hidden ? "true" : "false");
      APPEND (s, len, cap, ",\"visibility\":");
      json_escape (&s, &len, &cap, sy->visibility);
      APPEND (s, len, cap, ",\"binding\":");
      json_escape (&s, &len, &cap, sy->binding);
      if (sy->sec_idx >= 0)
        APPEND (s, len, cap, ",\"sec\":%d", sy->sec_idx);
      APPEND (s, len, cap, ",\"expr\":");
      json_expr (&s, &len, &cap, sy->expr);
      APPEND (s, len, cap, "}");
    }
  APPEND (s, len, cap, "]");

  /* dot assignments (ordered) */
  APPEND (s, len, cap, ",\"dots\":[");
  for (size_t i = 0; i < p->ndotsteps; i++)
    {
      const ccwld_dotstep *d = &p->dotsteps[i];
      if (i)
        APPEND (s, len, cap, ",");
      APPEND (s, len, cap, "{\"sec\":%d,\"expr\":", d->sec_idx);
      json_expr (&s, &len, &cap, d->expr);
      APPEND (s, len, cap, "}");
    }
  APPEND (s, len, cap, "]");

  /* phdrs (ordered) */
  APPEND (s, len, cap, ",\"phdrs\":[");
  for (size_t i = 0; i < p->nphdrs; i++)
    {
      if (i)
        APPEND (s, len, cap, ",");
      APPEND (s, len, cap, "{\"name\":");
      json_escape (&s, &len, &cap, p->phdrs[i].name);
      APPEND (s, len, cap, ",\"type\":");
      json_escape (&s, &len, &cap, p->phdrs[i].type);
      APPEND (s, len, cap, ",\"flags\":%u", p->phdrs[i].flags);
      APPEND (s, len, cap, ",\"align\":%" PRIu64, p->phdrs[i].align);
      APPEND (s, len, cap, "}");
    }
  APPEND (s, len, cap, "]");

  /* version (ordered) */
  APPEND (s, len, cap, ",\"version\":[");
  for (size_t i = 0; i < p->nvers; i++)
    {
      if (i)
        APPEND (s, len, cap, ",");
      APPEND (s, len, cap, "{\"symbol\":");
      json_escape (&s, &len, &cap, p->vers[i].symbol);
      APPEND (s, len, cap, ",\"version\":");
      json_escape (&s, &len, &cap, p->vers[i].version);
      APPEND (s, len, cap, ",\"is_default\":%s",
              p->vers[i].is_default ? "true" : "false");
      APPEND (s, len, cap, "}");
    }
  APPEND (s, len, cap, "]");

  /* LTO (configuration only, D-0041) */
  if (p->lto.enabled)
    {
      APPEND (s, len, cap, ",\"lto\":{\"pipeline\":");
      json_escape (&s, &len, &cap, p->lto.pipeline ? p->lto.pipeline : "");
      APPEND (s, len, cap, ",\"jobs\":%u", p->lto.jobs);
      if (p->lto.cache_dir)
        {
          APPEND (s, len, cap, ",\"cache_dir\":");
          json_escape (&s, &len, &cap, p->lto.cache_dir);
        }
      APPEND (s, len, cap, "}");
    }

  /* plugins (registration only, D-0042) */
  APPEND (s, len, cap, ",\"plugins\":[");
  for (size_t i = 0; i < p->nplugins; i++)
    {
      if (i)
        APPEND (s, len, cap, ",");
      APPEND (s, len, cap, "{\"path\":");
      json_escape (&s, &len, &cap, p->plugins[i].path);
      if (p->plugins[i].options)
        {
          APPEND (s, len, cap, ",\"options\":");
          json_escape (&s, &len, &cap, p->plugins[i].options);
        }
      APPEND (s, len, cap, "}");
    }
  APPEND (s, len, cap, "]");

  /* visibility/binding overrides and aliases (lccwld §4.7) */
  APPEND (s, len, cap, ",\"attrs\":[");
  for (size_t i = 0; i < p->nattrs; i++)
    {
      if (i)
        APPEND (s, len, cap, ",");
      APPEND (s, len, cap, "{\"name\":");
      json_escape (&s, &len, &cap, p->attrs[i].name);
      if (p->attrs[i].visibility)
        {
          APPEND (s, len, cap, ",\"visibility\":");
          json_escape (&s, &len, &cap, p->attrs[i].visibility);
        }
      if (p->attrs[i].binding)
        {
          APPEND (s, len, cap, ",\"binding\":");
          json_escape (&s, &len, &cap, p->attrs[i].binding);
        }
      if (p->attrs[i].alias)
        {
          APPEND (s, len, cap, ",\"alias\":");
          json_escape (&s, &len, &cap, p->attrs[i].alias);
        }
      APPEND (s, len, cap, "}");
    }
  APPEND (s, len, cap, "]");

  APPEND (s, len, cap, "}");

  if (!s)
    return oom (e);
  *out = s;
  if (out_len)
    *out_len = len;
  return 1;
}

/* --- seal (freezes + validates the plan) --- */

int
ccwld_plan_seal (ccwld_plan *p, ccwld_error *e)
{
  if (!p)
    {
      if (e)
        {
          e->code = CCWLD_EXIT_LINK;
          snprintf (e->message, sizeof (e->message), "invalid plan");
        }
      return 0;
    }
  if (p->sealed)
    {
      if (e)
        {
          e->code = CCWLD_EXIT_LINK;
          snprintf (e->message, sizeof (e->message), "plan already sealed");
        }
      return 0;
    }
  if (!p->output.kind)
    {
      if (e)
        {
          e->code = CCWLD_EXIT_USAGE;
          snprintf (e->message, sizeof (e->message),
                    "output declaration is required");
        }
      return 0;
    }

  /* Format discipline (§6: format-inappropriate plan nodes are rejected
   * before emission; PE has no phdrs, version nodes are ELF/DSO only). */
  const char *fmt = p->output.format ? p->output.format : "elf";
  if (p->nphdrs > 0 && !strcmp (fmt, "pe"))
    {
      if (e)
        {
          e->code = CCWLD_EXIT_USAGE;
          snprintf (e->message, sizeof (e->message),
                    "phdrs are not valid for PE output");
        }
      return 0;
    }
  if (p->nvers > 0 && strcmp (fmt, "elf"))
    {
      if (e)
        {
          e->code = CCWLD_EXIT_USAGE;
          snprintf (e->message, sizeof (e->message),
                    "version nodes are only valid for ELF output");
        }
      return 0;
    }

  /* Region / phdr references must resolve (plan-node diagnostics). */
  for (size_t i = 0; i < p->nsecs; i++)
    {
      const ccwld_sec *s = &p->secs[i];
      if (s->region)
        {
          int found = 0;
          for (size_t j = 0; j < p->nmems; j++)
            if (p->mems[j].name && !strcmp (p->mems[j].name, s->region))
              found = 1;
          if (!found)
            {
              if (e)
                {
                  e->code = CCWLD_EXIT_USAGE;
                  snprintf (e->message, sizeof (e->message),
                            "section '%s' references unknown region '%s'",
                            s->name, s->region);
                }
              return 0;
            }
        }
      if (s->at_region)
        {
          int found = 0;
          for (size_t j = 0; j < p->nmems; j++)
            if (p->mems[j].name && !strcmp (p->mems[j].name, s->at_region))
              found = 1;
          if (!found)
            {
              if (e)
                {
                  e->code = CCWLD_EXIT_USAGE;
                  snprintf (e->message, sizeof (e->message),
                            "section '%s' references unknown at_region '%s'",
                            s->name, s->at_region);
                }
              return 0;
            }
        }
      if (s->phdr)
        {
          int found = 0;
          for (size_t j = 0; j < p->nphdrs; j++)
            if (p->phdrs[j].name && !strcmp (p->phdrs[j].name, s->phdr))
              found = 1;
          if (!found)
            {
              if (e)
                {
                  e->code = CCWLD_EXIT_USAGE;
                  snprintf (e->message, sizeof (e->message),
                            "section '%s' references unknown phdr '%s'",
                            s->name, s->phdr);
                }
              return 0;
            }
        }
    }

  /* Serialize and cache the canonical form */
  char *s = NULL;
  size_t slen = 0;
  if (!ccwld_plan_serialize (p, &s, &slen, e))
    return 0;
  free (p->serialized);
  p->serialized = s;
  p->serialized_len = slen;

  /* Compute the content hash */
  ccwld_plan_hash (p, p->plan_hash);

  p->sealed = true;
  return 1;
}

/* --- hash --- */

int
ccwld_plan_hash (const ccwld_plan *p, char out[65])
{
  if (!p || !out)
    return 0;

  /* Use the serialized form if available, otherwise serialize now */
  const char *s = p->serialized;
  size_t slen = p->serialized_len;
  char *tmp = NULL;
  if (!s)
    {
      if (!ccwld_plan_serialize (p, &tmp, &slen, NULL))
        return 0;
      s = tmp;
    }

  /* FNV-1a based 256-bit hash (four lanes; 64 hex chars) */
  uint64_t h1 = 14695981039346656037ULL;
  uint64_t h2 = 1099511628211ULL;
  uint64_t h3 = 0x9e3779b97f4a7c15ULL;
  uint64_t h4 = 0xc6a4a7935bd1e995ULL;

  for (size_t i = 0; i < slen; i++)
    {
      uint8_t byte = (uint8_t)s[i];
      h1 ^= byte;
      h1 *= 1099511628211ULL;
      h2 = (h2 ^ byte) * 1099511628211ULL;
      h3 = (h3 ^ byte) * 1099511628211ULL;
      h4 = (h4 ^ byte) * 1099511628211ULL;
    }

  snprintf (out, 65, "%016" PRIx64 "%016" PRIx64 "%016" PRIx64 "%016" PRIx64,
            h1, h2, h3, h4);

  if (tmp)
    free (tmp);
  return 1;
}

void
ccwld_free (void *p)
{
  free (p);
}
