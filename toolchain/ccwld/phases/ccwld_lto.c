/* §4 LTO: the native backend behind the ccwld-lto C ABI.
 *
 * CCWld dlopens the backend named by the `pipeline` configuration
 * string, feeds it every `.ccw.lto` IR member via ccwld_lto_add_module,
 * and ingests the native objects the backend yields through the emit
 * callback.  Emitted objects re-enter the pipeline before gc and the
 * resolution is recomputed so the backend saw the full symbol picture
 * (D-0041).  ABI-version mismatch is fatal with exit code 3. */
#include "ccwld_phases.h"
#include "../abi/ccwld-lto.h"

#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

typedef ccwld_lto_ctx *(*lto_begin_fn) (const ccwld_lto_config *);
typedef int (*lto_add_fn) (ccwld_lto_ctx *, const void *, size_t,
                           const char *);
typedef int (*lto_run_fn) (ccwld_lto_ctx *,
                           void (*) (void *, const void *, size_t,
                                     const char *),
                           void *);
typedef void (*lto_end_fn) (ccwld_lto_ctx *);
typedef const char *(*lto_err_fn) (ccwld_lto_ctx *);

typedef struct
{
  ccwld_state *st;
  ccwld_error *e;
  int count;
  const char *backend_path;
} lto_emit_ctx;

static void
lto_emit_cb (void *user, const void *obj, size_t len, const char *name)
{
  lto_emit_ctx *c = (lto_emit_ctx *)user;
  if (!obj || !len)
    return;
  char pathbuf[512];
  snprintf (pathbuf, sizeof (pathbuf), "%s<%s>", c->backend_path,
            name ? name : "lto");
  if (!ccwld_load_elf_mem (c->st, pathbuf, obj, len, c->e))
    return; /* error recorded in c->e */
  c->count++;
}

int
ccwld_phase_lto (ccwld_state *st, ccwld_error *e)
{
  ccwld_plan *p = st->plan;
  if (!p->lto.enabled || !p->lto.pipeline || !p->lto.pipeline[0])
    return 1; /* not configured: pipeline hook point only (§3) */

  /* Determinism gate (§4): under the reproducible flag a backend that
   * may run parallel codegen is pinned to jobs=1, and the note records
   * the pinning. */
  unsigned jobs = p->lto.jobs;
  if (p->reproducible && jobs > 1)
    {
      jobs = 1;
      ccwld_diag_warn (st, NULL, NULL,
                       "LTO backend pinned to jobs=1 for reproducible "
                       "output");
    }

  void *dl = dlopen (p->lto.pipeline, RTLD_NOW | RTLD_LOCAL);
  if (!dl)
    {
      const char *dm = dlerror ();
      ccwld_error_set (e, CCWLD_EXIT_ABI, "cannot load LTO backend '%s': %s",
                       p->lto.pipeline, dm ? dm : "?");
      return 0;
    }
  lto_begin_fn begin = (lto_begin_fn)dlsym (dl, "ccwld_lto_begin");
  lto_add_fn add = (lto_add_fn)dlsym (dl, "ccwld_lto_add_module");
  lto_run_fn run = (lto_run_fn)dlsym (dl, "ccwld_lto_run");
  lto_end_fn end = (lto_end_fn)dlsym (dl, "ccwld_lto_end");
  lto_err_fn lasterr = (lto_err_fn)dlsym (dl, "ccwld_lto_last_error");
  if (!begin || !add || !run || !end)
    {
      ccwld_error_set (e, CCWLD_EXIT_ABI,
                       "LTO backend '%s' is missing ccwld-lto entry points",
                       p->lto.pipeline);
      dlclose (dl);
      return 0;
    }

  ccwld_lto_config cfg;
  memset (&cfg, 0, sizeof (cfg));
  cfg.abi_version = CCWLD_LTO_ABI_VERSION;
  cfg.pipeline = p->lto.pipeline;
  cfg.jobs = jobs;
  cfg.cache_dir = p->lto.cache_dir;

  ccwld_lto_ctx *ctx = begin (&cfg);
  if (!ctx)
    {
      const char *m = lasterr ? lasterr (NULL) : NULL;
      ccwld_error_set (e, CCWLD_EXIT_ABI,
                       "LTO backend '%s' rejected the session (ABI version "
                       "%u): %s",
                       p->lto.pipeline, (unsigned)CCWLD_LTO_ABI_VERSION,
                       m ? m : "unknown error");
      dlclose (dl);
      return 0;
    }

  /* Feed every IR member (objects carrying a .ccw.lto payload). */
  int fed = 0;
  for (size_t i = 0; i < st->nobjs; i++)
    {
      ccwld_obj *o = &st->objs[i];
      if (!o->is_lto)
        continue;
      for (size_t k = 0; k < o->nsecs; k++)
        {
          if (!strcmp (o->secs[k].name, ".ccw.lto") && o->secs[k].data)
            {
              if (add (ctx, o->secs[k].data, o->secs[k].size, o->path) != 0)
                {
                  const char *m = lasterr ? lasterr (ctx) : NULL;
                  ccwld_error_set (e, CCWLD_EXIT_ABI,
                                   "LTO backend failed on module '%s': %s",
                                   o->path, m ? m : "unknown error");
                  end (ctx);
                  dlclose (dl);
                  return 0;
                }
              fed++;
            }
        }
    }
  if (!fed)
    {
      end (ctx);
      dlclose (dl);
      ccwld_diag_warn (st, NULL, NULL,
                       "LTO configured but no .ccw.lto IR members found");
      return 1;
    }

  /* Run and ingest; then re-resolve (§3). */
  lto_emit_ctx ec = { st, e, 0, p->lto.pipeline };
  if (run (ctx, lto_emit_cb, &ec) != 0)
    {
      const char *m = lasterr ? lasterr (ctx) : NULL;
      ccwld_error_set (e, CCWLD_EXIT_ABI, "LTO backend run failed: %s",
                       m ? m : "unknown error");
      end (ctx);
      dlclose (dl);
      return 0;
    }
  end (ctx);
  dlclose (dl);

  /* Record backend identity for the producer note (§8). */
  free (st->lto_backend_used);
  st->lto_backend_used = strdup (p->lto.pipeline);
  st->lto_abi_used = CCWLD_LTO_ABI_VERSION;

  /* LTO source objects are replaced by their native counterparts. */
  for (size_t i = 0; i < st->nobjs; i++)
    {
      ccwld_obj *o = &st->objs[i];
      if (!o->is_lto)
        continue;
      for (size_t k = 0; k < o->nsecs; k++)
        o->secs[k].live = 0;
      for (size_t k = 0; k < st->nrsyms; k++)
        if (st->rsyms[k].obj == (int)i)
          {
            st->rsyms[k].defined = 0;
            st->rsyms[k].obj = -1;
            st->rsyms[k].isym = -1;
            st->rsyms[k].value_known = 0;
          }
    }

  /* Merge the emitted objects and re-run resolution from scratch so the
   * native results participate fully (script symbols included). */
  if (ec.count > 0)
    {
      ccwld_state_reset_resolution (st);
      /* resolve() re-applies its five passes over the whole state */
      extern int ccwld_phase_resolve (ccwld_state *, ccwld_error *);
      if (!ccwld_phase_resolve (st, e))
        return 0;
    }
  return 1;
}
