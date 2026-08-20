/* ccwld.c — link execution and convenience API.
 *
 * The plan IR and expression engine live in plan/ and expr/.
 * This file implements the link execution pipeline and the
 * ccwld_link_files convenience wrapper. */

#include "ccwld.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- helpers --- */

static void
err_set (ccwld_error *e, int c, const char *f, ...)
{
  if (!e)
    return;
  e->code = c;
  va_list ap;
  va_start (ap, f);
  vsnprintf (e->message, sizeof (e->message), f, ap);
  va_end (ap);
}

static char *
dupstr (const char *s)
{
  return s ? strdup (s) : NULL;
}

/* --- phase hook dispatch --- */

static int
phase_dispatch (ccwld_plan *p, ccwld_phase ph, ccwld_error *e)
{
  ccwld_link l = { p, ph, NULL };
  for (size_t i = 0; i < p->nhooks; i++)
    {
      if (p->hooks[i].phase == ph
          && p->hooks[i].fn (ph, &l, p->hooks[i].user) != 0)
        {
          err_set (e, 5, "hook failed at phase %d", (int)ph);
          return 0;
        }
    }
  return 1;
}

/* --- link execution --- */

int
ccwld_link_run (ccwld_plan *p, const char *out, ccwld_error *e)
{
  FILE *f;
  char hash[65];
  char *s = NULL;
  size_t n = 0;

  if (!p || !out)
    {
      err_set (e, 1, "invalid link request");
      return 0;
    }
  if (!p->sealed && !ccwld_plan_seal (p, e))
    return 0;

  /* Run the phase pipeline (§3) */
  if (!phase_dispatch (p, CCWLD_PHASE_LOAD, e))
    return 0;
  if (!phase_dispatch (p, CCWLD_PHASE_RESOLVE, e))
    return 0;
  if (!phase_dispatch (p, CCWLD_PHASE_GC, e))
    return 0;
  if (!phase_dispatch (p, CCWLD_PHASE_LAYOUT, e))
    return 0;
  if (!phase_dispatch (p, CCWLD_PHASE_RELOCATE, e))
    return 0;
  if (!phase_dispatch (p, CCWLD_PHASE_EMIT, e))
    return 0;

  if (!ccwld_plan_hash (p, hash) || !ccwld_plan_serialize (p, &s, &n, e))
    return 0;

  /* Wasm output via Binaryen */
  if (p->output.format && !strcmp (p->output.format, "wasm"))
    {
      free (s);
      return ccwld_emit_binaryen (out, p->output.entry, e);
    }

  /* ELF output via LIEF (if available) */
  if (p->ninputs > 0 && p->inputs[0].path != NULL
      && p->output.format && !strcmp (p->output.format, "elf"))
    {
      int ok = ccwld_emit_lief (p->inputs[0].path, out, p->output.kind,
                                p->output.format, p->output.entry, hash, e);
      free (s);
      return ok;
    }

  /* Fallback: write a text-format object with .note.ccw */
  f = fopen (out, "wb");
  if (!f)
    {
      free (s);
      err_set (e, 6, "cannot write output '%s'", out);
      return 0;
    }
  fprintf (f,
           "CCWLD-OBJECT\nformat=%s\nkind=%s\nplan-hash=%s\nreproducible="
           "%s\n.note.ccw=%s\n",
           p->output.format ? p->output.format : "elf",
           p->output.kind ? p->output.kind : "exe", hash,
           p->reproducible ? "true" : "false", hash);
  fwrite (s, 1, n, f);
  fputc ('\n', f);
  fclose (f);
  free (s);
  return 1;
}

/* --- convenience: link files --- */

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
      err_set (e, 1, "invalid link request");
      return 0;
    }

  memset (&plan_out, 0, sizeof (plan_out));
  plan_out.kind = dupstr (options && options->kind ? options->kind : "exe");
  plan_out.format = dupstr (options && options->format ? options->format : "elf");
  plan_out.entry = dupstr (options ? options->entry : NULL);
  plan_out.soname = dupstr (options ? options->soname : NULL);
  plan_out.osabi = dupstr (options ? options->osabi : NULL);

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

/* --- ld-script frontend stub --- */

int
ccwld_run_ldscript (const char *script, const char *target,
                    ccwld_plan **out, ccwld_error *e)
{
  (void)script;
  (void)target;
  (void)out;
  if (e)
    err_set (e, 4, "ld-script frontend is not yet implemented");
  return 0;
}
