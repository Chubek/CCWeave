/* Parthia's deterministic module boundary.
 *
 * This deliberately consumes only Swaff's language-neutral surface AST.
 * Tree-sitter types and grammar details do not cross into the elaborator.
 * The current core representation is a compact, typed-fact carrier: module
 * signatures are erased after their constraints are recorded, and functor
 * applications receive names derived from their lexical application path.
 */

#include "sml_parthia.h"
#include "ccw_dynalo_bridge.h"
#include "dyncall.h"
#include "kbarena.h"
#include "kstring.h"
#include "parthia_rt.h"
#include "ccw_sched.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ccw_sml_parthia_program
{
  char *surface_ast;
  char *core_ast;
  ccw_sml_parthia_report report;
};

static char *
dup_text (const char *text)
{
  size_t length;
  char *copy;
  if (text == NULL)
    return NULL;
  length = strlen (text);
  copy = (char *)malloc (length + 1u);
  if (copy == NULL)
    return NULL;
  memcpy (copy, text, length + 1u);
  return copy;
}

static void
set_error (char **error_message, const char *message)
{
  if (error_message != NULL)
    *error_message = dup_text (message);
}

static char *
read_text_file (const char *path, size_t *length)
{
  FILE *file;
  kstring_t text = { 0, 0, NULL };
  char buffer[4096];
  size_t got;
  if (!path || !length)
    return NULL;
  file = fopen (path, "rb");
  if (!file)
    return NULL;
  while ((got = fread (buffer, 1, sizeof (buffer), file)) != 0)
    if (kputsn (buffer, (int)got, &text) == EOF)
      {
        fclose (file);
        free (text.s);
        return NULL;
      }
  fclose (file);
  *length = text.l;
  return ks_release (&text);
}

static int
directive_path (const char *line, const char *keyword, char *path,
                size_t path_size)
{
  const char *p = strstr (line, keyword);
  const char *q;
  size_t n;
  if (!p)
    return 0;
  p += strlen (keyword);
  while (*p == ' ' || *p == '\t')
    p++;
  if (*p != '"')
    return 0;
  q = strchr (++p, '"');
  if (!q || q == p)
    return 0;
  n = (size_t)(q - p);
  if (n + 1 > path_size)
    return 0;
  memcpy (path, p, n);
  path[n] = '\0';
  return 1;
}

static int
path_exists (const char *path)
{
  FILE *probe = fopen (path, "rb");
  if (probe != NULL)
    {
      fclose (probe);
      return 1;
    }
  return 0;
}

/* Probe `leaf` (and its enclosing search directory, when any) for an
 * existing file.  Returns a malloc'd candidate the caller frees. */
static char *
probe_at (const char *base, size_t base_len, const char *leaf, size_t leaf_len)
{
  char *candidate;
  char *cursor;
  if (base_len + leaf_len + 2u < base_len)
    return NULL;
  candidate = (char *)malloc (base_len + leaf_len + 2u);
  if (candidate == NULL)
    return NULL;
  cursor = candidate;
  if (base_len != 0)
    {
      memcpy (cursor, base, base_len);
      cursor += base_len;
      *cursor++ = '/';
    }
  memcpy (cursor, leaf, leaf_len);
  cursor[leaf_len] = '\0';
  if (path_exists (candidate))
    return candidate;
  free (candidate);
  return NULL;
}

char *
ccw_sml_parthia_resolve_path (const char *name)
{
  const char *search;
  const char *cursor;
  const char *leaves[4];
  char with_so[1100], lib_with_so[1100], lib_only[1100];
  int leaf_count = 1;
  int leaf;
  if (name == NULL || name[0] == '\0')
    return NULL;
  if (strchr (name, '/') != NULL || name[0] == '.')
    return dup_text (name);
  leaves[0] = name;
  if (strstr (name, ".so") == NULL)
    {
      snprintf (with_so, sizeof (with_so), "%s.so", name);
      snprintf (lib_with_so, sizeof (lib_with_so), "lib%s.so", name);
      snprintf (lib_only, sizeof (lib_only), "lib%s", name);
      leaves[1] = with_so;
      leaves[2] = lib_with_so;
      leaves[3] = lib_only;
      leaf_count = 4;
    }
  for (leaf = 0; leaf < leaf_count; ++leaf)
    {
      char *found = probe_at ("", 0u, leaves[leaf], strlen (leaves[leaf]));
      if (found != NULL)
        return found;
    }
  search = getenv ("SML_PARTHIA_PATH");
  if (search == NULL)
    return NULL;
  cursor = search;
  while (*cursor != '\0')
    {
      const char *end = strchr (cursor, ',');
      size_t dir_len = end ? (size_t)(end - cursor) : strlen (cursor);
      if (dir_len != 0)
        for (leaf = 0; leaf < leaf_count; ++leaf)
          {
            char *found = probe_at (cursor, dir_len, leaves[leaf],
                                    strlen (leaves[leaf]));
            if (found != NULL)
              return found;
          }
      if (end == NULL)
        break;
      cursor = end + 1;
    }
  return NULL;
}

static char *
expand_directives (ccw_sml_parthia_runtime *runtime, const char *source,
                   size_t source_len, char **error_message)
{
  kstring_t out = { 0, 0, NULL };
  size_t offset = 0;
  if (!source)
    return NULL;
  while (offset < source_len)
    {
      const char *line_end
          = memchr (source + offset, '\n', source_len - offset);
      size_t line_len = line_end ? (size_t)(line_end - (source + offset))
                                 : source_len - offset;
      char *line = (char *)malloc (line_len + 1);
      char path[1024];
      int is_use = 0, is_load = 0;
      if (!line)
        {
          free (out.s);
          set_error (error_message, "sml/parthia: out of memory");
          return NULL;
        }
      memcpy (line, source + offset, line_len);
      line[line_len] = '\0';
      is_load = directive_path (line, "#load", path, sizeof (path))
                || directive_path (line, "load", path, sizeof (path))
                || directive_path (line, "CM.make", path, sizeof (path));
      is_use = !is_load && directive_path (line, "use", path, sizeof (path));
      if (is_use)
        {
          char *resolved = ccw_sml_parthia_resolve_path (path);
          size_t included_len = 0;
          char *included = resolved != NULL
                               ? read_text_file (resolved, &included_len)
                               : NULL;
          if (included == NULL
              || kputsn (included, (int)included_len, &out) == EOF)
            {
              char message[1200];
              if (resolved == NULL)
                snprintf (message, sizeof (message),
                          "sml/parthia: cannot load SML source %s: "
                          "not found in SML_PARTHIA_PATH",
                          path);
              else
                snprintf (message, sizeof (message),
                          "sml/parthia: cannot read SML source %s: %s",
                          resolved, strerror (errno));
              free (included);
              free (resolved);
              free (line);
              free (out.s);
              set_error (error_message, message);
              return NULL;
            }
          free (included);
          free (resolved);
        }
      else if (is_load && runtime)
        {
          char *resolved = ccw_sml_parthia_resolve_path (path);
          if (resolved != NULL
              && !ccw_sml_parthia_load_extension (runtime, resolved))
            {
              char message[1200];
              snprintf (message, sizeof (message),
                        "sml/parthia: native library load failed: %s",
                        resolved);
              free (resolved);
              free (line);
              free (out.s);
              set_error (error_message, message);
              return NULL;
            }
          if (resolved == NULL)
            {
              char message[1200];
              snprintf (message, sizeof (message),
                        "sml/parthia: native library %s: "
                        "not found in SML_PARTHIA_PATH",
                        path);
              free (line);
              free (out.s);
              set_error (error_message, message);
              return NULL;
            }
          free (resolved);
        }
      else if (!is_load && kputsn (line, (int)line_len, &out) == EOF)
        {
          free (line);
          free (out.s);
          set_error (error_message, "sml/parthia: out of memory");
          return NULL;
        }
      free (line);
      if (line_end && kputc ('\n', &out) == EOF)
        {
          free (out.s);
          return NULL;
        }
      offset += line_len + (line_end ? 1u : 0u);
    }
  return ks_release (&out);
}

int
ccw_sml_parthia_load_plan (const char *level, const char *manifest_dir,
                           const char *sched_dir, ccw_plan **out,
                           char **error_message)
{
  const char *chosen = level != NULL ? level : "O2";
  const char *root
      = sched_dir != NULL ? sched_dir : "interpreters/sml-parthia/sched";
  char path[1024];
  ccw_sched_error error = { 0 };

  if (out != NULL)
    *out = NULL;
  if (error_message != NULL)
    *error_message = NULL;
  if (strcmp (chosen, "O0") != 0 && strcmp (chosen, "O1") != 0
      && strcmp (chosen, "O2") != 0)
    {
      set_error (error_message,
                 "sml/parthia: scheduler level must be O0, O1, or O2");
      return 0;
    }
  if (out == NULL)
    {
      set_error (error_message, "sml/parthia: plan output is required");
      return 0;
    }
  snprintf (path, sizeof (path), "%s/%s.lua", root, chosen);
  if (!ccw_sched_run_script (path, manifest_dir ? manifest_dir : "manifests",
                             out, &error))
    {
      set_error (error_message, error.message);
      return 0;
    }
  return 1;
}

static bool
append (kstring_t *out, const char *text)
{
  return text != NULL && kputs (text, out) != EOF;
}

static int
count_tag (const char *ast, const char *tag)
{
  int count = 0;
  size_t tag_length;
  const char *cursor;
  if (ast == NULL || tag == NULL)
    return 0;
  tag_length = strlen (tag);
  cursor = ast;
  while ((cursor = strstr (cursor, tag)) != NULL)
    {
      bool boundary_before
          = cursor == ast || cursor[-1] == '(' || cursor[-1] == ' ';
      bool boundary_after = cursor[tag_length] == '\0'
                            || cursor[tag_length] == ')'
                            || cursor[tag_length] == ' ';
      if (boundary_before && boundary_after)
        count++;
      cursor += tag_length;
    }
  return count;
}

static int
compare_names (const void *left, const void *right)
{
  const char *const *a = (const char *const *)left;
  const char *const *b = (const char *const *)right;
  return strcmp (*a, *b);
}

static char **
collect_functor_paths (const char *surface, int *count)
{
  const char *cursor = surface;
  char **names = NULL;
  int used = 0;
  while (cursor != NULL && (cursor = strstr (cursor, "(fctapp ")) != NULL)
    {
      const char *start = cursor + strlen ("(fctapp ");
      const char *end = start;
      char *name;
      while (*end != '\0' && *end != ' ' && *end != ')' && *end != '(')
        end++;
      if (end == start)
        {
          cursor = end;
          continue;
        }
      name = (char *)malloc ((size_t)(end - start) + 1u);
      if (name == NULL)
        break;
      memcpy (name, start, (size_t)(end - start));
      name[end - start] = '\0';
      {
        char **grown
            = (char **)realloc (names, (size_t)(used + 1) * sizeof (*names));
        if (grown == NULL)
          {
            free (name);
            break;
          }
        names = grown;
      }
      names[used++] = name;
      cursor = end;
    }
  qsort (names, (size_t)used, sizeof (*names), compare_names);
  *count = used;
  return names;
}

static bool
emit_core_facts (const char *surface, const ccw_sml_parse_report *parse,
                 kstring_t *out, ccw_sml_parthia_report *report)
{
  char line[160];
  int path_count = 0;
  char **paths = collect_functor_paths (surface, &path_count);
#define CORE_FAIL()                                                           \
  do                                                                          \
    {                                                                         \
      for (int core_i = 0; core_i < path_count; core_i++)                     \
        free (paths[core_i]);                                                 \
      free (paths);                                                           \
      return false;                                                           \
    }                                                                         \
  while (0)
  if (!append (out, "(core-ml (modules"))
    CORE_FAIL ();
  if (parse->structure_count > 0)
    {
      snprintf (line, sizeof (line), " (structures %d)",
                parse->structure_count);
      if (!append (out, line))
        CORE_FAIL ();
    }
  if (parse->signature_count > 0)
    {
      snprintf (line, sizeof (line), " (signatures %d)",
                parse->signature_count);
      if (!append (out, line))
        CORE_FAIL ();
    }
  if (parse->sharing_count > 0)
    {
      snprintf (line, sizeof (line), " (sharing-constraints %d)",
                parse->sharing_count);
      if (!append (out, line))
        CORE_FAIL ();
    }
  if (parse->wheretype_count > 0)
    {
      snprintf (line, sizeof (line), " (where-type-constraints %d)",
                parse->wheretype_count);
      if (!append (out, line))
        CORE_FAIL ();
    }
  if (!append (out, ") (functors"))
    CORE_FAIL ();
  for (int i = 0; i < path_count; i++)
    {
      /* The path is sorted before emission.  Duplicate applications receive
       * a stable suffix only to keep their serialized names distinct. */
      int duplicate = 0;
      for (int j = 0; j < i; j++)
        if (strcmp (paths[j], paths[i]) == 0)
          duplicate++;
      if (duplicate == 0)
        snprintf (line, sizeof (line), " (instance fct.%s)", paths[i]);
      else
        snprintf (line, sizeof (line), " (instance fct.%s.%d)", paths[i],
                  duplicate);
      if (!append (out, line))
        CORE_FAIL ();
    }
  if (!append (out, ") (typed-facts"))
    CORE_FAIL ();
  if (count_tag (surface, "(infix") > 0
      && !append (out, " (fixity-resolved true)"))
    CORE_FAIL ();
  if (!append (out, ")))"))
    CORE_FAIL ();

  report->module_facts = parse->structure_count + parse->signature_count
                         + parse->sharing_count + parse->wheretype_count;
  report->functor_instances = path_count;
  report->value_facts = count_tag (surface, "(val");
  report->type_facts = count_tag (surface, "(type");
  for (int i = 0; i < path_count; i++)
    free (paths[i]);
  free (paths);
#undef CORE_FAIL
  return true;
}

static ccw_sml_parthia_program *
compile_expanded (const char *source, size_t source_len,
                  ccw_sml_parthia_report *report, char **error_message)
{
  ccw_sml_parthia_program *program;
  ccw_sml_parse_report parse;
  char *surface;
  char *parse_error = NULL;
  kstring_t core = { 0, 0, NULL };

  if (error_message != NULL)
    *error_message = NULL;
  if (report != NULL)
    memset (report, 0, sizeof (*report));
  surface = ccw_swaff_parse_sml (source, source_len, &parse, &parse_error);
  if (surface == NULL)
    {
      if (report != NULL)
        report->parse = parse;
      if (error_message != NULL)
        *error_message = parse_error;
      else
        free (parse_error);
      return NULL;
    }
  free (parse_error);

  program = (ccw_sml_parthia_program *)calloc (1, sizeof (*program));
  if (program == NULL)
    {
      free (surface);
      set_error (error_message, "sml/parthia: out of memory");
      return NULL;
    }
  program->surface_ast = surface;
  program->report.parse = parse;
  if (!emit_core_facts (surface, &parse, &core, &program->report))
    {
      ccw_sml_parthia_program_destroy (program);
      free (core.s);
      set_error (error_message, "sml/parthia: out of memory");
      return NULL;
    }
  program->core_ast = ks_release (&core);
  if (program->core_ast == NULL)
    {
      ccw_sml_parthia_program_destroy (program);
      set_error (error_message, "sml/parthia: out of memory");
      return NULL;
    }
  if (report != NULL)
    *report = program->report;
  return program;
}

ccw_sml_parthia_program *
ccw_sml_parthia_compile_with_runtime (ccw_sml_parthia_runtime *runtime,
                                      const char *source, size_t source_len,
                                      ccw_sml_parthia_report *report,
                                      char **error_message)
{
  char *expanded;
  size_t expanded_len;
  if (error_message)
    *error_message = NULL;
  expanded = expand_directives (runtime, source, source_len, error_message);
  if (!expanded)
    return NULL;
  expanded_len = strlen (expanded);
  {
    ccw_sml_parthia_program *program
        = compile_expanded (expanded, expanded_len, report, error_message);
    free (expanded);
    return program;
  }
}

ccw_sml_parthia_program *
ccw_sml_parthia_compile (const char *source, size_t source_len,
                         ccw_sml_parthia_report *report, char **error_message)
{
  return ccw_sml_parthia_compile_with_runtime (NULL, source, source_len,
                                               report, error_message);
}

ccw_sml_parthia_program *
ccw_sml_parthia_compile_file (ccw_sml_parthia_runtime *runtime,
                              const char *path, ccw_sml_parthia_report *report,
                              char **error_message)
{
  size_t length = 0;
  char *source = read_text_file (path, &length);
  ccw_sml_parthia_program *program;
  if (!source)
    {
      char message[256];
      snprintf (message, sizeof (message), "sml/parthia: cannot read %s: %s",
                path ? path : "(null)", strerror (errno));
      set_error (error_message, message);
      return NULL;
    }
  program = ccw_sml_parthia_compile_with_runtime (runtime, source, length,
                                                  report, error_message);
  free (source);
  return program;
}

void
ccw_sml_parthia_program_destroy (ccw_sml_parthia_program *program)
{
  if (program == NULL)
    return;
  free (program->surface_ast);
  free (program->core_ast);
  free (program);
}

const char *
ccw_sml_parthia_surface_ast (const ccw_sml_parthia_program *program)
{
  return program == NULL ? NULL : program->surface_ast;
}

const char *
ccw_sml_parthia_core_ast (const ccw_sml_parthia_program *program)
{
  return program == NULL ? NULL : program->core_ast;
}

static int
execute_surface (ccw_sml_parthia_runtime *runtime, const char *surface,
                 char **result, char **error_message)
{
  pa_cache *cache;
  pa_program *compiled = NULL;
  char *compile_error = NULL;
  char *shown;

  if (result != NULL)
    *result = NULL;
  if (error_message != NULL)
    *error_message = NULL;
  if (runtime == NULL || surface == NULL)
    {
      set_error (error_message,
                 "sml/parthia: runtime and source are required");
      return 0;
    }

  for (cache = runtime->cache; cache != NULL; cache = cache->next)
    if (strcmp (cache->key, surface) == 0)
      {
        runtime->jit_hits++;
        compiled = cache->prog;
        break;
      }
  if (compiled == NULL)
    {
      compiled = pa_compile_surface (runtime, surface, &compile_error);
      if (compiled == NULL)
        {
          set_error (error_message, compile_error != NULL
                                        ? compile_error
                                        : "sml/parthia: lowering failed");
          return 0;
        }
      cache = (pa_cache *)pa_alloc (runtime, sizeof (*cache));
      if (cache == NULL)
        {
          set_error (error_message, "sml/parthia: out of memory");
          return 0;
        }
      cache->key = pa_strdup (runtime, surface);
      cache->prog = compiled;
      cache->next = runtime->cache;
      runtime->cache = cache;
      runtime->jit_phrases++;
    }
  if (!pa_eval_program (runtime, compiled, error_message))
    return 0;
  if (result == NULL)
    return 1;
  shown = pa_show (runtime, pa_lookup (runtime, runtime->global, "it"));
  if (shown == NULL)
    {
      set_error (error_message, "sml/parthia: cannot format result");
      return 0;
    }
  *result = dup_text (shown);
  if (*result == NULL)
    {
      set_error (error_message, "sml/parthia: out of memory");
      return 0;
    }
  return 1;
}

int
ccw_sml_parthia_run (ccw_sml_parthia_runtime *runtime, const char *source,
                     size_t source_len, char **result, char **error_message)
{
  ccw_sml_parthia_runtime *owned = runtime;
  ccw_sml_parthia_program *program;
  int ok;
  if (owned == NULL)
    owned = ccw_sml_parthia_runtime_new ();
  if (owned == NULL)
    {
      set_error (error_message, "sml/parthia: cannot create runtime");
      return 0;
    }
  program = ccw_sml_parthia_compile_with_runtime (owned, source, source_len,
                                                  NULL, error_message);
  if (program == NULL)
    {
      if (runtime == NULL)
        ccw_sml_parthia_runtime_free (owned);
      return 0;
    }
  ok = execute_surface (owned, program->surface_ast, result, error_message);
  ccw_sml_parthia_program_destroy (program);
  if (runtime == NULL)
    ccw_sml_parthia_runtime_free (owned);
  return ok;
}

int
ccw_sml_parthia_eval (ccw_sml_parthia_runtime *runtime, const char *phrase,
                      size_t phrase_len, char **result, char **error_message)
{
  if (runtime == NULL)
    {
      set_error (error_message, "sml/parthia: JIT evaluation needs a runtime");
      return 0;
    }
  {
    ccw_sml_parthia_program *program = ccw_sml_parthia_compile_with_runtime (
        runtime, phrase, phrase_len, NULL, error_message);
    int ok;
    if (program == NULL)
      return 0;
    ok = execute_surface (runtime, program->surface_ast, result,
                          error_message);
    ccw_sml_parthia_program_destroy (program);
    return ok;
  }
}

int
ccw_sml_parthia_structure_count (const ccw_sml_parthia_program *program)
{
  return program == NULL ? 0 : program->report.parse.structure_count;
}

int
ccw_sml_parthia_signature_count (const ccw_sml_parthia_program *program)
{
  return program == NULL ? 0 : program->report.parse.signature_count;
}

int
ccw_sml_parthia_functor_count (const ccw_sml_parthia_program *program)
{
  return program == NULL ? 0 : program->report.parse.functor_count;
}

int
ccw_sml_parthia_sharing_count (const ccw_sml_parthia_program *program)
{
  return program == NULL ? 0 : program->report.parse.sharing_count;
}

int
ccw_sml_parthia_wheretype_count (const ccw_sml_parthia_program *program)
{
  return program == NULL ? 0 : program->report.parse.wheretype_count;
}

ccw_sml_parthia_runtime *
ccw_sml_parthia_runtime_new (void)
{
  ccw_sml_parthia_runtime *runtime
      = (ccw_sml_parthia_runtime *)calloc (1, sizeof (*runtime));
  if (runtime == NULL)
    return NULL;
  runtime->arena = kba_init (64u * 1024u);
  if (runtime->arena == NULL)
    {
      free (runtime);
      return NULL;
    }
  runtime->global = pa_env_new (runtime, NULL);
  if (runtime->global == NULL)
    {
      kba_destroy (runtime->arena);
      free (runtime);
      return NULL;
    }
  pa_basis_install (runtime);
  pa_kio_install (runtime);
  return runtime;
}
void
ccw_sml_parthia_runtime_free (ccw_sml_parthia_runtime *runtime)
{
  sml_ext_entry *e, *n;
  if (!runtime)
    return;
  for (e = runtime->extensions; e; e = n)
    {
      n = e->next;
      free (e->name);
      ccw_dynalo_close (e->handle);
      free (e);
    }
  kba_destroy (runtime->arena);
  free (runtime);
}
int
ccw_sml_parthia_register_extension (ccw_sml_parthia_runtime *runtime,
                                    const ccw_sml_extension *extension)
{
  sml_ext_entry *entry;
  if (!runtime || !extension || !extension->name || !*extension->name
      || !extension->invoke)
    return 0;
  entry = (sml_ext_entry *)calloc (1, sizeof (*entry));
  if (!entry)
    return 0;
  entry->name = dup_text (extension->name);
  if (!entry->name)
    {
      free (entry);
      return 0;
    }
  entry->invoke = extension->invoke;
  entry->userdata = extension->userdata;
  entry->next = runtime->extensions;
  runtime->extensions = entry;
  return 1;
}
int
ccw_sml_parthia_call_native (ccw_sml_parthia_runtime *runtime,
                             const char *name, const ccw_sml_value *args,
                             size_t nargs, ccw_sml_value *results,
                             size_t nresults)
{
  sml_ext_entry *e;
  if (!runtime || !name || (nargs && !args) || (nresults && !results))
    return 0;
  for (e = runtime->extensions; e; e = e->next)
    if (strcmp (e->name, name) == 0)
      return e->invoke (args, nargs, results, nresults, e->userdata) == 0;
  return 0;
}
int
ccw_sml_parthia_load_extension (ccw_sml_parthia_runtime *runtime,
                                const char *path)
{
  void *handle;
  const ccw_sml_extension *(*init_fn) (void);
  const ccw_sml_extension *extension;
  if (!runtime || !path)
    return 0;
  handle = ccw_dynalo_open (path, NULL);
  if (!handle)
    return 0;
  *(void **)(&init_fn)
      = ccw_dynalo_symbol (handle, "ccw_sml_parthia_extension_init", NULL);
  extension = init_fn ? init_fn () : NULL;
  if (!extension || !ccw_sml_parthia_register_extension (runtime, extension))
    {
      ccw_dynalo_close (handle);
      return 0;
    }
  runtime->extensions->handle = handle;
  return 1;
}

ccw_sml_ffi_library
ccw_sml_parthia_ffi_open (const char *path)
{
  return ccw_dynalo_open (path, NULL);
}
void *
ccw_sml_parthia_ffi_symbol (ccw_sml_ffi_library library, const char *name)
{
  return ccw_dynalo_symbol (library, name, NULL);
}
void
ccw_sml_parthia_ffi_close (ccw_sml_ffi_library library)
{
  ccw_dynalo_close (library);
}
int
ccw_sml_parthia_ffi_call_i64 (void *symbol, const long long *args,
                              size_t nargs, long long *result)
{
  size_t i;
  DCint error;
  DCCallVM *vm;
  if (!symbol || !result || nargs > 8 || (nargs && !args))
    return 0;
  vm = dcNewCallVM (4096);
  if (!vm)
    return 0;
  dcMode (vm, DC_CALL_C_DEFAULT);
  for (i = 0; i < nargs; ++i)
    dcArgLongLong (vm, (DClonglong)args[i]);
  *result = (long long)dcCallLongLong (vm, (DCpointer)symbol);
  error = dcGetError (vm);
  dcFree (vm);
  if (error != DC_ERROR_NONE)
    return 0;
  return 1;
}
