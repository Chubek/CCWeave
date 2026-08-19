#define _POSIX_C_SOURCE 200809L
#include "ccwas.h"
#include "kstring.h"
#include "lccwas/lccwas.h"
#include "parse/ccw_parse.h"
#include "sema/ccw_symtab.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
fail (char **error, const char *message)
{
  if (error)
    *error = message ? strdup (message) : NULL;
  return 0;
}

static char *
read_all (const char *path, char **error)
{
  FILE *f = fopen (path, "rb");
  kstring_t out = { 0, 0, NULL };
  char buf[4096];
  if (!f)
    {
      char msg[512];
      snprintf (msg, sizeof (msg), "%s: %s", path, strerror (errno));
      fail (error, msg);
      return NULL;
    }
  while (!feof (f))
    {
      size_t n = fread (buf, 1, sizeof (buf), f);
      if (n && kputsn (buf, (int)n, &out) == EOF)
        {
          fclose (f);
          free (out.s);
          fail (error, "out of memory");
          return NULL;
        }
      if (ferror (f))
        {
          char msg[512];
          snprintf (msg, sizeof (msg), "%s: %s", path, strerror (errno));
          fclose (f);
          free (out.s);
          fail (error, msg);
          return NULL;
        }
    }
  fclose (f);
  return ks_release (&out);
}

int
ccwas_assemble (const char *source, const char *filename,
                const ccwas_options *options, const char *output_path,
                char **error)
{
  ccwas_options defaults
      = { CCW_ARCH_X86_64, "intel", CCW_FMT_ELF, NULL, 0, 0, 0, 0 };
  ccwas_options o = options ? *options : defaults;
  ccw_lccwas lua;
  ccw_unit_t unit;
  char *expanded = NULL;
  char *err = NULL;
  int ok = 0;

  if (error)
    *error = NULL;
  if (!source || !output_path)
    return fail (error, "source and output_path are required");
  if (!filename)
    filename = "<memory>";
  if (!o.syntax)
    o.syntax = "intel";
  if (o.arch != CCW_ARCH_X86_64 && strcmp (o.syntax, "intel") != 0)
    return fail (error, "syntax is only configurable for x86-64");

  ccw_lccwas_init (&lua,
                   o.arch == CCW_ARCH_X86_64    ? "x86-64"
                   : o.arch == CCW_ARCH_AARCH64 ? "aarch64"
                   : o.arch == CCW_ARCH_RISCV64 ? "riscv64"
                                                : "wasm32",
                   o.syntax, filename, o.unsafe_lua);
  for (size_t i = 0; i < o.define_count; ++i)
    {
      const char *def = o.defines[i];
      const char *eq = def ? strchr (def, '=') : NULL;
      if (!def)
        continue;
      if (eq)
        {
          char *key = strndup (def, (size_t)(eq - def));
          if (!key || !ccw_lccwas_define (&lua, key, eq + 1))
            {
              free (key);
              ccw_lccwas_destroy (&lua);
              return fail (error, "out of memory");
            }
          free (key);
        }
      else if (!ccw_lccwas_define (&lua, def, "1"))
        {
          ccw_lccwas_destroy (&lua);
          return fail (error, "out of memory");
        }
    }
  if (o.force_template || strstr (source, "<?lua"))
    {
      if (!ccw_lccwas_expand_buffer (&lua, source, filename, &err))
        {
          ccw_lccwas_destroy (&lua);
          if (error)
            *error = err;
          else
            free (err);
          return 0;
        }
      expanded = ccw_lccwas_take_buffer (&lua);
    }
  else
    {
      expanded = strdup (source);
    }
  ccw_lccwas_seal (&lua);
  if (!expanded)
    {
      ccw_lccwas_destroy (&lua);
      return fail (error, "out of memory");
    }

  ccw_unit_init (&unit, o.arch, o.syntax);
  if (!ccw_parse_asm (&unit, expanded, filename, &err))
    {
      if (error)
        *error = err;
      else
        free (err);
      free (expanded);
      ccw_unit_destroy (&unit);
      ccw_lccwas_destroy (&lua);
      return 0;
    }
  if (unit.error_count || (o.werror && unit.warning_count))
    {
      char msg[128];
      snprintf (msg, sizeof (msg), "%d error(s) during assembly",
                unit.error_count ? unit.error_count : unit.warning_count);
      free (expanded);
      ccw_unit_destroy (&unit);
      ccw_lccwas_destroy (&lua);
      return fail (error, msg);
    }
  ok = ccw_obj_write (&unit, output_path, o.format, &err);
  if (!ok)
    {
      if (error)
        *error = err;
      else
        free (err);
    }
  free (expanded);
  ccw_unit_destroy (&unit);
  ccw_lccwas_destroy (&lua);
  return ok;
}

int
ccwas_assemble_file (const char *input_path, const ccwas_options *options,
                     const char *output_path, char **error)
{
  char *source = read_all (input_path, error);
  int ok;
  if (!source)
    return 0;
  ok = ccwas_assemble (source, input_path, options, output_path, error);
  free (source);
  return ok;
}

void
ccwas_free_error (char *error)
{
  free (error);
}
