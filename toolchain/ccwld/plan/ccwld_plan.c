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
  return !f || !strcmp (f, "elf") || !strcmp (f, "pe") || !strcmp (f, "macho");
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
  return p;
}

void
ccwld_plan_free (ccwld_plan *p)
{
  if (!p)
    return;
  free (p->target);
  free (p->serialized);

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
      free (p->secs[i].selector);
      free (p->secs[i].keep);
      ccwld_expr_free (p->secs[i].fill);
    }
  free (p->secs);

  /* symbols */
  for (size_t i = 0; i < p->nsyms; i++)
    {
      free (p->syms[i].name);
      free (p->syms[i].visibility);
      free (p->syms[i].binding);
      ccwld_expr_free (p->syms[i].expr);
    }
  free (p->syms);

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
  free (p->hooks);

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
          e->code = 2;
          snprintf (e->message, sizeof (e->message),
                    "invalid output kind or format");
        }
      return 0;
    }
  if (o->soname && o->format && strcmp (o->format, "elf"))
    {
      if (e)
        {
          e->code = 2;
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
          e->code = 1;
          snprintf (e->message, sizeof (e->message), "invalid input path");
        }
      return 0;
    }
  if (!grow ((void **)&p->inputs, &p->cinputs, p->ninputs + 1,
             sizeof (*p->inputs)))
    {
      if (e)
        {
          e->code = 3;
          snprintf (e->message, sizeof (e->message), "out of memory");
        }
      return 0;
    }
  p->inputs[p->ninputs].path = dupstr (path);
  p->inputs[p->ninputs].as_needed = as_needed;
  p->inputs[p->ninputs].startup = startup;
  p->inputs[p->ninputs].is_group = 0;
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
          e->code = 1;
          snprintf (e->message, sizeof (e->message), "invalid search path");
        }
      return 0;
    }
  if (!grow ((void **)&p->paths, &p->cpaths, p->npaths + 1,
             sizeof (*p->paths)))
    {
      if (e)
        {
          e->code = 3;
          snprintf (e->message, sizeof (e->message), "out of memory");
        }
      return 0;
    }
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
          e->code = 1;
          snprintf (e->message, sizeof (e->message), "invalid memory region");
        }
      return 0;
    }
  if (!grow ((void **)&p->mems, &p->cmems, p->nmems + 1, sizeof (*p->mems)))
    {
      if (e)
        {
          e->code = 3;
          snprintf (e->message, sizeof (e->message), "out of memory");
        }
      return 0;
    }
  p->mems[p->nmems].name = dupstr (name);
  p->mems[p->nmems].attrs = dupstr (attrs ? attrs : "");
  p->mems[p->nmems].origin = origin;
  p->mems[p->nmems].length = length;
  p->nmems++;
  return 1;
}

/* --- sections --- */

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
          e->code = 1;
          snprintf (e->message, sizeof (e->message), "invalid section name");
        }
      return 0;
    }
  if (!grow ((void **)&p->secs, &p->csecs, p->nsecs + 1, sizeof (*p->secs)))
    {
      if (e)
        {
          e->code = 3;
          snprintf (e->message, sizeof (e->message), "out of memory");
        }
      return 0;
    }
  ccwld_sec *s = &p->secs[p->nsecs++];
  memset (s, 0, sizeof (*s));
  s->name = dupstr (name);
  s->region = dupstr (region);
  s->at_region = dupstr (at_region);
  s->selector = dupstr (selector);
  s->keep = dupstr (keep);
  s->align = align ? align : 1;
  s->load = 1;
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
  if (is_sealed (p, e))
    return 0;
  if (!name || !expr)
    {
      if (e)
        {
          e->code = 1;
          snprintf (e->message, sizeof (e->message), "invalid symbol");
        }
      return 0;
    }
  if (!grow ((void **)&p->syms, &p->csyms, p->nsyms + 1, sizeof (*p->syms)))
    {
      if (e)
        {
          e->code = 3;
          snprintf (e->message, sizeof (e->message), "out of memory");
        }
      return 0;
    }
  ccwld_sym *s = &p->syms[p->nsyms++];
  memset (s, 0, sizeof (*s));
  s->name = dupstr (name);
  s->expr = expr;
  s->provide = provide;
  s->hidden = (visibility && !strcmp (visibility, "hidden")) ? 1 : 0;
  s->visibility = dupstr (visibility ? visibility : "default");
  s->binding = dupstr (binding ? binding : "global");
  s->resolved = 0;
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
          e->code = 1;
          snprintf (e->message, sizeof (e->message), "invalid phdr name");
        }
      return 0;
    }
  if (!grow ((void **)&p->phdrs, &p->cphdrs, p->nphdrs + 1,
             sizeof (*p->phdrs)))
    {
      if (e)
        {
          e->code = 3;
          snprintf (e->message, sizeof (e->message), "out of memory");
        }
      return 0;
    }
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
          e->code = 1;
          snprintf (e->message, sizeof (e->message), "invalid version node");
        }
      return 0;
    }
  if (!grow ((void **)&p->vers, &p->cvers, p->nvers + 1, sizeof (*p->vers)))
    {
      if (e)
        {
          e->code = 3;
          snprintf (e->message, sizeof (e->message), "out of memory");
        }
      return 0;
    }
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
          e->code = 1;
          snprintf (e->message, sizeof (e->message), "invalid plugin path");
        }
      return 0;
    }
  if (!grow ((void **)&p->plugins, &p->cplugins, p->nplugins + 1,
             sizeof (*p->plugins)))
    {
      if (e)
        {
          e->code = 3;
          snprintf (e->message, sizeof (e->message), "out of memory");
        }
      return 0;
    }
  p->plugins[p->nplugins].path = dupstr (path);
  p->plugins[p->nplugins].name = NULL;
  p->plugins[p->nplugins].options = dupstr (options);
  p->plugins[p->nplugins].loaded = 0;
  p->nplugins++;
  return 1;
}

/* --- hooks --- */

int
ccwld_plan_hook (ccwld_plan *p, ccwld_phase_id phase, ccwld_hook_fn fn,
                 void *user, ccwld_error *e)
{
  if (is_sealed (p, e))
    return 0;
  if (!fn)
    {
      if (e)
        {
          e->code = 1;
          snprintf (e->message, sizeof (e->message), "invalid hook function");
        }
      return 0;
    }
  if (!grow ((void **)&p->hooks, &p->chooks, p->nhooks + 1,
             sizeof (*p->hooks)))
    {
      if (e)
        {
          e->code = 3;
          snprintf (e->message, sizeof (e->message), "out of memory");
        }
      return 0;
    }
  p->hooks[p->nhooks].phase = phase;
  p->hooks[p->nhooks].fn = fn;
  p->hooks[p->nhooks].user = user;
  p->nhooks++;
  return 1;
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
            _nc *= 2;                                                         \
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

/* --- serialization --- */

int
ccwld_plan_serialize (const ccwld_plan *p, char **out, size_t *out_len,
                      ccwld_error *e)
{
  if (!p || !out)
    {
      if (e)
        {
          e->code = 1;
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

  /* inputs */
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
      APPEND (s, len, cap, ",\"is_group\":%s",
              p->inputs[i].is_group ? "true" : "false");
      APPEND (s, len, cap, "}");
    }
  APPEND (s, len, cap, "]");

  /* memory */
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

  /* sections */
  APPEND (s, len, cap, ",\"sections\":[");
  for (size_t i = 0; i < p->nsecs; i++)
    {
      if (i)
        APPEND (s, len, cap, ",");
      APPEND (s, len, cap, "{\"name\":");
      json_escape (&s, &len, &cap, p->secs[i].name);
      if (p->secs[i].region)
        {
          APPEND (s, len, cap, ",\"region\":");
          json_escape (&s, &len, &cap, p->secs[i].region);
        }
      APPEND (s, len, cap, ",\"align\":%" PRIu64, p->secs[i].align);
      if (p->secs[i].selector)
        {
          APPEND (s, len, cap, ",\"selector\":");
          json_escape (&s, &len, &cap, p->secs[i].selector);
        }
      if (p->secs[i].keep)
        {
          APPEND (s, len, cap, ",\"keep\":");
          json_escape (&s, &len, &cap, p->secs[i].keep);
        }
      if (p->secs[i].at_region)
        {
          APPEND (s, len, cap, ",\"at_region\":");
          json_escape (&s, &len, &cap, p->secs[i].at_region);
        }
      APPEND (s, len, cap, ",\"load\":%s", p->secs[i].load ? "true" : "false");
      APPEND (s, len, cap, "}");
    }
  APPEND (s, len, cap, "]");

  /* symbols */
  APPEND (s, len, cap, ",\"symbols\":[");
  for (size_t i = 0; i < p->nsyms; i++)
    {
      if (i)
        APPEND (s, len, cap, ",");
      APPEND (s, len, cap, "{\"name\":");
      json_escape (&s, &len, &cap, p->syms[i].name);
      APPEND (s, len, cap, ",\"provide\":%s",
              p->syms[i].provide ? "true" : "false");
      APPEND (s, len, cap, ",\"hidden\":%s",
              p->syms[i].hidden ? "true" : "false");
      APPEND (s, len, cap, ",\"visibility\":");
      json_escape (&s, &len, &cap, p->syms[i].visibility);
      APPEND (s, len, cap, ",\"binding\":");
      json_escape (&s, &len, &cap, p->syms[i].binding);
      APPEND (s, len, cap, ",\"expr\":\"");
      ccwld_expr_to_string (p->syms[i].expr, &s, &len, &cap);
      APPEND (s, len, cap, "\"");
      APPEND (s, len, cap, "}");
    }
  APPEND (s, len, cap, "]");

  /* phdrs */
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

  /* version */
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

  /* LTO */
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

  /* plugins */
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

  APPEND (s, len, cap, "}");

  *out = s;
  if (out_len)
    *out_len = len;
  return 1;
}

/* --- seal --- */

int
ccwld_plan_seal (ccwld_plan *p, ccwld_error *e)
{
  if (!p)
    {
      if (e)
        {
          e->code = 1;
          snprintf (e->message, sizeof (e->message), "invalid plan");
        }
      return 0;
    }
  if (p->sealed)
    {
      if (e)
        {
          e->code = 1;
          snprintf (e->message, sizeof (e->message), "plan already sealed");
        }
      return 0;
    }
  if (!p->output.kind)
    {
      if (e)
        {
          e->code = 2;
          snprintf (e->message, sizeof (e->message),
                    "output declaration is required");
        }
      return 0;
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

  /* FNV-1a based 256-bit hash (simplified to 64 hex chars) */
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

/* --- expression adapters (thin wrappers) --- */

ccwld_expr *
ccwld_expr_int (uint64_t value)
{
  return ccwld_expr_int (value);
}

ccwld_expr *
ccwld_expr_symbol (const char *name)
{
  return ccwld_expr_symbol (name);
}

ccwld_expr *
ccwld_expr_dot (void)
{
  return ccwld_expr_dot ();
}

ccwld_expr *
ccwld_expr_binary (char op, ccwld_expr *a, ccwld_expr *b)
{
  return ccwld_expr_binary ((ccwld_op_tag)op, a, b);
}

ccwld_expr *
ccwld_expr_unary (char op, ccwld_expr *a)
{
  return ccwld_expr_unary ((ccwld_op_tag)op, a);
}

ccwld_expr *
ccwld_expr_align_expr (ccwld_expr *a, uint64_t boundary)
{
  return ccwld_expr_align (a, boundary);
}

void
ccwld_expr_free (ccwld_expr *e)
{
  ccwld_expr_free (e);
}

int
ccwld_expr_eval (const ccwld_expr *e, const ccwld_plan *p, uint64_t dot,
                 uint64_t *out, ccwld_error *e_)
{
  char *errmsg = NULL;
  int ok = ccwld_expr_eval (e, p, dot, out, &errmsg);
  if (!ok && e_ && errmsg)
    {
      e_->code = 4;
      snprintf (e_->message, sizeof (e_->message), "%s", errmsg);
      free (errmsg);
    }
  else if (errmsg)
    {
      free (errmsg);
    }
  return ok;
}

void
ccwld_free (void *p)
{
  free (p);
}
