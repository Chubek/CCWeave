/* ccwld(1) — the command-line face of the linker (§9 exit codes).
 *
 * Lowers the command line into driver-level declarations (inputs,
 * search paths, entry, plugins, LTO config) handed to the frontend of
 * record, then runs the pipeline.  -T selects the frontend by
 * extension: .lua is lccwld (LCCWLD.md), anything else is the mpc
 * ld-script frontend.  With no -T the driver builds the plan directly
 * (the "api" frontend). */
#include "../ccwld.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_STRINGS 4096

typedef struct
{
  char *v[MAX_STRINGS + 1];
  size_t n;
} str_list;

static void
str_list_add (str_list *l, const char *s)
{
  if (l->n >= MAX_STRINGS)
    {
      fprintf (stderr, "ccwld: too many arguments (max %d)\n", MAX_STRINGS);
      return;
    }
  l->v[l->n++] = strdup (s);
}

/* NULL-terminated view of a str_list (backing storage stays alive) */
static const char *const *
str_list_c (str_list *l)
{
  l->v[l->n] = NULL;
  return (const char *const *)l->v;
}

static void
str_list_free (str_list *l)
{
  for (size_t i = 0; i < l->n; i++)
    free (l->v[i]);
  l->n = 0;
}

/* --- JSON building for --plugin-opt (deterministic: command order) --- */

static char *
xrealloc (char *b, size_t *cap, size_t need)
{
  if (need < *cap)
    return b;
  while (*cap <= need)
    *cap *= 2;
  return realloc (b, *cap);
}

static void
json_escape_append (char **buf, size_t *len, size_t *cap, const char *s)
{
  for (; *s; s++)
    {
      *buf = xrealloc (*buf, cap, *len + 8);
      if ((unsigned char)*s < 0x20 || *s == '"' || *s == '\\')
        *len += (size_t)snprintf (*buf + *len, *cap - *len, "\\u%04x",
                                  (unsigned char)*s);
      else
        (*buf)[(*len)++] = *s;
    }
}

static void
jch (char **buf, size_t *len, size_t *cap, char c)
{
  *buf = xrealloc (*buf, cap, *len + 2);
  (*buf)[(*len)++] = c;
}

static char *
build_plugin_opts_json (str_list *opts)
{
  size_t cap = 64, len = 0;
  char *buf = malloc (cap);
  if (!buf)
    return NULL;
  jch (&buf, &len, &cap, '{');
  for (size_t i = 0; i < opts->n; i++)
    {
      if (i)
        jch (&buf, &len, &cap, ',');
      const char *eq = strchr (opts->v[i], '=');
      const char *key = opts->v[i];
      const char *val = eq ? eq + 1 : "true";
      size_t klen = eq ? (size_t)(eq - key) : strlen (key);
      char *k = strndup (key, klen);
      jch (&buf, &len, &cap, '"');
      json_escape_append (&buf, &len, &cap, k ? k : key);
      jch (&buf, &len, &cap, '"');
      jch (&buf, &len, &cap, ':');
      jch (&buf, &len, &cap, '"');
      json_escape_append (&buf, &len, &cap, val);
      jch (&buf, &len, &cap, '"');
      free (k);
    }
  jch (&buf, &len, &cap, '}');
  buf[len] = 0;
  return buf;
}

/* --- -l expansion along the -L paths (lib<name>.so, then .a) --- */

static char *
find_lib (str_list *paths, const char *name)
{
  static const char *suffixes[] = { ".so", ".a" };
  char rel[512];
  for (size_t s = 0; s < sizeof (suffixes) / sizeof (suffixes[0]); s++)
    {
      snprintf (rel, sizeof (rel), "lib%s%s", name, suffixes[s]);
      if (!access (rel, R_OK))
        return strdup (rel);
      for (size_t i = 0; i < paths->n; i++)
        {
          char full[1024];
          snprintf (full, sizeof (full), "%s/%s", paths->v[i], rel);
          if (!access (full, R_OK))
            return strdup (full);
        }
    }
  return NULL;
}

static void
usage (FILE *out)
{
  fputs ("usage: ccwld [options] inputs...\n"
         "  -o FILE              output path (default: OUTPUT() or a.out)\n"
         "  -T FILE              linker script (.lua -> lccwld, else "
         "ld-script)\n"
         "  --target TRIPLE      target triple\n"
         "  -e, --entry SYM      entry symbol (overrides the script's)\n"
         "  -L DIR               library search path (repeatable)\n"
         "  -l NAME              library (libNAME.so or libNAME.a)\n"
         "  -D K=V               define exposed to the script\n"
         "  --defsym N=V         define an absolute symbol\n"
         "  --gc-sections        discard unreferenced sections\n"
         "  --as-needed          link DSOs only when referenced\n"
         "  --plugin PATH        load a ccwld-plugin.h plugin\n"
         "  --plugin-opt K=V     option for driver-loaded plugins\n"
         "  --lto-pipeline PATH  LTO backend (ccwld-lto.h)\n"
         "  --lto-jobs N         LTO parallel jobs (reproducible pins to 1)\n"
         "  --lto-cache-dir DIR  LTO cache directory\n"
         "  --cache-dir DIR      enable the link cache at DIR\n"
         "  --no-cache           disable the link cache\n"
         "  --unsafe-lua         unlock the lccwld sandbox (drops the\n"
         "                       reproducible mark)\n"
         "  --print-plan         print the canonical plan to stdout\n",
         out);
}

int
main (int argc, char **argv)
{
  const char *script = NULL, *target = "unknown", *out_arg = NULL;
  const char *entry = NULL, *cache_dir = NULL, *lto_pipeline = NULL,
             *lto_cache_dir = NULL;
  unsigned lto_jobs = 0;
  int gc_sections = 0, as_needed = 0, print_plan = 0, no_cache = 0,
      unsafe_lua = 0;

  str_list inputs, libs, spaths, defines, defsymbols, plugins, plugin_opts;
  memset (&inputs, 0, sizeof (inputs));
  memset (&libs, 0, sizeof (libs));
  memset (&spaths, 0, sizeof (spaths));
  memset (&defines, 0, sizeof (defines));
  memset (&defsymbols, 0, sizeof (defsymbols));
  memset (&plugins, 0, sizeof (plugins));
  memset (&plugin_opts, 0, sizeof (plugin_opts));

  ccwld_error e;
  memset (&e, 0, sizeof (e));
  ccwld_plan *p = NULL;
  char *plugin_json = NULL;
  int rc = 2;

#define FAIL(cls)                                                             \
  do                                                                          \
    {                                                                         \
      rc = e.code ? e.code : (cls);                                           \
      if (e.message[0])                                                       \
        fprintf (stderr, "ccwld: %s\n", e.message);                           \
      goto done;                                                              \
    }                                                                         \
  while (0)

  for (int i = 1; i < argc; i++)
    {
      const char *a = argv[i];
      if (!strcmp (a, "-o") && i + 1 < argc)
        out_arg = argv[++i];
      else if ((!strcmp (a, "-T") || !strcmp (a, "--script")) && i + 1 < argc)
        script = argv[++i];
      else if (!strcmp (a, "--target") && i + 1 < argc)
        target = argv[++i];
      else if ((!strcmp (a, "-e") || !strcmp (a, "--entry")) && i + 1 < argc)
        entry = argv[++i];
      else if (!strcmp (a, "-L") && i + 1 < argc)
        str_list_add (&spaths, argv[++i]);
      else if (!strncmp (a, "-L", 2) && a[2])
        str_list_add (&spaths, a + 2);
      else if (!strcmp (a, "-l") && i + 1 < argc)
        str_list_add (&libs, argv[++i]);
      else if (!strncmp (a, "-l", 2) && a[2])
        str_list_add (&libs, a + 2);
      else if (!strcmp (a, "-D") && i + 1 < argc)
        str_list_add (&defines, argv[++i]);
      else if (!strncmp (a, "-D", 2) && a[2])
        str_list_add (&defines, a + 2);
      else if (!strcmp (a, "--defsym") && i + 1 < argc)
        str_list_add (&defsymbols, argv[++i]);
      else if (!strcmp (a, "--gc-sections"))
        gc_sections = 1;
      else if (!strcmp (a, "--as-needed"))
        as_needed = 1;
      else if (!strcmp (a, "--plugin") && i + 1 < argc)
        str_list_add (&plugins, argv[++i]);
      else if (!strcmp (a, "--plugin-opt") && i + 1 < argc)
        str_list_add (&plugin_opts, argv[++i]);
      else if (!strcmp (a, "--lto-pipeline") && i + 1 < argc)
        lto_pipeline = argv[++i];
      else if (!strcmp (a, "--lto-jobs") && i + 1 < argc)
        lto_jobs = (unsigned)strtoul (argv[++i], NULL, 10);
      else if (!strcmp (a, "--lto-cache-dir") && i + 1 < argc)
        lto_cache_dir = argv[++i];
      else if (!strcmp (a, "--cache-dir") && i + 1 < argc)
        cache_dir = argv[++i];
      else if (!strcmp (a, "--no-cache"))
        no_cache = 1;
      else if (!strcmp (a, "--unsafe-lua"))
        unsafe_lua = 1;
      else if (!strcmp (a, "--print-plan"))
        print_plan = 1;
      else if (!strcmp (a, "-h") || !strcmp (a, "--help"))
        {
          usage (stdout);
          return 0;
        }
      else if (!strcmp (a, "--version"))
        {
          printf ("ccwld %s\n", CCWLD_VERSION);
          return 0;
        }
      else if (a[0] == '-' && a[1])
        {
          fprintf (stderr, "ccwld: unknown option '%s'\n", a);
          usage (stderr);
          return 2;
        }
      else
        str_list_add (&inputs, a);
    }

  if (!script && inputs.n == 0 && libs.n == 0)
    {
      fprintf (stderr, "ccwld: no inputs and no -T script\n");
      usage (stderr);
      return 2;
    }

  /* -l expansion happens in command-line order against the -L set */
  for (size_t i = 0; i < libs.n; i++)
    {
      char *found = find_lib (&spaths, libs.v[i]);
      if (!found)
        {
          fprintf (stderr, "ccwld: cannot find library '-l%s'\n", libs.v[i]);
          goto done;
        }
      str_list_add (&inputs, found);
      free (found);
    }

  plugin_json = plugins.n ? build_plugin_opts_json (&plugin_opts) : NULL;

  ccwld_driver_defs extra;
  memset (&extra, 0, sizeof (extra));
  extra.inputs = str_list_c (&inputs);
  extra.search_paths = str_list_c (&spaths);
  extra.entry = entry;
  extra.plugins = str_list_c (&plugins);
  extra.plugin_opts_json = plugin_json;
  extra.lto_pipeline = lto_pipeline;
  extra.lto_jobs = lto_jobs;
  extra.lto_cache_dir = lto_cache_dir;

  if (script)
    {
      size_t n = strlen (script);
      if (n > 4 && !strcmp (script + n - 4, ".lua"))
        {
          if (!ccwld_run_lua (script, target, str_list_c (&defines),
                              str_list_c (&defsymbols), unsafe_lua, &extra,
                              &p, &e))
            FAIL (CCWLD_EXIT_USAGE);
        }
      else
        {
          FILE *f = fopen (script, "rb");
          if (!f)
            {
              fprintf (stderr, "ccwld: cannot open script '%s'\n", script);
              goto done;
            }
          fseek (f, 0, SEEK_END);
          long len = ftell (f);
          fseek (f, 0, SEEK_SET);
          if (len < 0)
            {
              fclose (f);
              fprintf (stderr, "ccwld: cannot read script '%s'\n", script);
              goto done;
            }
          char *buf = malloc ((size_t)len + 1);
          if (!buf || fread (buf, 1, (size_t)len, f) != (size_t)len)
            {
              fprintf (stderr, "ccwld: cannot read script '%s'\n", script);
              free (buf);
              fclose (f);
              goto done;
            }
          fclose (f);
          buf[len] = 0;
          int ok = ccwld_run_ldscript (buf, script, target, &extra, &p, &e);
          free (buf);
          if (!ok)
            FAIL (CCWLD_EXIT_USAGE);
        }
    }
  else
    {
      /* no script: the command line is the whole plan ("api" frontend) */
      p = ccwld_plan_new (target);
      if (!p)
        {
          ccwld_error_set (&e, CCWLD_EXIT_INTERNAL, "out of memory");
          FAIL (CCWLD_EXIT_INTERNAL);
        }
      ccwld_plan_set_frontend (p, "api");
      ccwld_output def;
      memset (&def, 0, sizeof (def));
      def.kind = (char *)"exe";
      def.format = (char *)"elf";
      def.entry = (char *)entry;
      if (!ccwld_plan_output (p, &def, &e))
        FAIL (CCWLD_EXIT_USAGE);
      {
        const char *const *ds = str_list_c (&defines);
        for (size_t k = 0; ds[k]; k++)
          {
            const char *eq = strchr (ds[k], '=');
            char key[128];
            if (!eq)
              {
                ccwld_error_set (&e, CCWLD_EXIT_USAGE,
                                 "-D expects key=value (got '%s')", ds[k]);
                FAIL (CCWLD_EXIT_USAGE);
              }
            size_t kl = (size_t)(eq - ds[k]);
            if (kl >= sizeof (key))
              kl = sizeof (key) - 1;
            memcpy (key, ds[k], kl);
            key[kl] = 0;
            if (!ccwld_plan_env (p, key, eq + 1, 0, &e))
              FAIL (CCWLD_EXIT_USAGE);
          }
        const char *const *dv = str_list_c (&defsymbols);
        for (size_t k = 0; dv[k]; k++)
          {
            const char *eq = strchr (dv[k], '=');
            char key[128];
            if (!eq)
              {
                ccwld_error_set (&e, CCWLD_EXIT_USAGE,
                                 "--defsym expects name=value (got '%s')",
                                 dv[k]);
                FAIL (CCWLD_EXIT_USAGE);
              }
            size_t kl = (size_t)(eq - dv[k]);
            if (kl >= sizeof (key))
              kl = sizeof (key) - 1;
            memcpy (key, dv[k], kl);
            key[kl] = 0;
            if (!ccwld_plan_env (p, key, eq + 1, 1, &e))
              FAIL (CCWLD_EXIT_USAGE);
          }
      }
      if (!ccwld_apply_driver_defs (p, &extra, &e))
        FAIL (CCWLD_EXIT_USAGE);
    }

  /* pipeline options (driver-level, not part of the declarative plan) */
  p->options.gc_sections = gc_sections;
  p->options.as_needed_default = as_needed;
  p->options.print_plan = print_plan;
  p->options.no_cache = no_cache;
  if (cache_dir && !no_cache)
    p->options.cache_dir = (char *)cache_dir;

  const char *out = out_arg ? out_arg
                  : (p->options.out_name ? p->options.out_name : "a.out");

  if (!ccwld_link_run (p, out, &e))
    FAIL (CCWLD_EXIT_LINK);
  rc = 0;

done:
  ccwld_plan_free (p);
  free (plugin_json);
  str_list_free (&inputs);
  str_list_free (&libs);
  str_list_free (&spaths);
  str_list_free (&defines);
  str_list_free (&defsymbols);
  str_list_free (&plugins);
  str_list_free (&plugin_opts);
  return rc;
}
