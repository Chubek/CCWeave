#include "repl.h"

#include "kstring.h"
#include "linenoise.h"
#include "sml_parthia.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SML_PARTHIA_REPL_PROMPT "- "
#define SML_PARTHIA_REPL_CONTINUATION_PROMPT "= "

static char *
repl_strdup (const char *text)
{
  size_t length;
  char *copy;
  if (!text)
    return NULL;
  length = strlen (text);
  copy = (char *)malloc (length + 1u);
  if (!copy)
    return NULL;
  memcpy (copy, text, length + 1u);
  return copy;
}

static int
append_line (char **source, size_t *source_len, const char *line)
{
  kstring_t text = { *source_len, *source_len, *source };
  if (*source_len != 0 && kputc ('\n', &text) == EOF)
    return 0;
  if (kputs (line, &text) == EOF)
    return 0;
  *source_len = text.l;
  *source = ks_release (&text);
  return 1;
}

static int
has_non_whitespace (const char *source, size_t length)
{
  size_t i;
  for (i = 0; i < length; ++i)
    if (source[i] != ' ' && source[i] != '\t' && source[i] != '\r'
        && source[i] != '\n')
      return 1;
  return 0;
}

static int
run_phrase (const char *source, size_t length,
            ccw_sml_parthia_runtime *runtime)
{
  ccw_sml_parthia_report report;
  ccw_sml_parthia_program *program;
  char *error = NULL;
  char *history;

  if (!has_non_whitespace (source, length))
    return 1;
  program = ccw_sml_parthia_compile_with_runtime (runtime, source, length,
                                                  &report, &error);
  if (program == NULL)
    {
      fprintf (stderr, "sml-parthia: %s\n", error ? error : "compile failed");
      free (error);
      return 0;
    }

  history = (char *)malloc (length + 1u);
  if (history != NULL)
    {
      memcpy (history, source, length);
      history[length] = '\0';
      linenoiseHistoryAdd (history);
      free (history);
    }
  fputs (ccw_sml_parthia_core_ast (program), stdout);
  fputc ('\n', stdout);
  ccw_sml_parthia_program_destroy (program);
  free (error);
  return 1;
}

static char *
join_path (const char *root, const char *name)
{
  size_t n;
  char *path;
  if (!root || !*root || !name || !*name)
    return NULL;
  n = strlen (root) + strlen (name) + 2;
  path = (char *)malloc (n);
  if (!path)
    return NULL;
  snprintf (path, n, "%s/%s", root, name);
  return path;
}

static char *
resolve_library (const char *name)
{
  const char *search = getenv ("SML_PARTHIA_PATH");
  const char *cursor;
  if (!name)
    return NULL;
  if (strchr (name, '/') != NULL || name[0] == '.')
    {
      return repl_strdup (name);
    }
  if (!search || !*search)
    return NULL;
  cursor = search;
  while (*cursor)
    {
      const char *end = strchr (cursor, ':');
      size_t length = end ? (size_t)(end - cursor) : strlen (cursor);
      char *root = (char *)malloc (length + 1);
      char *candidate;
      char variants[4][1024];
      int variant_count = 1;
      if (!root)
        return NULL;
      memcpy (root, cursor, length);
      root[length] = '\0';
      snprintf (variants[0], sizeof (variants[0]), "%s", name);
      if (!strstr (name, ".so"))
        {
          snprintf (variants[variant_count++], sizeof (variants[0]), "%s.so",
                    name);
          snprintf (variants[variant_count++], sizeof (variants[0]),
                    "lib%s.so", name);
          snprintf (variants[variant_count++], sizeof (variants[0]), "lib%s",
                    name);
        }
      for (int variant = 0; variant < variant_count; variant++)
        {
          candidate = join_path (root, variants[variant]);
          if (candidate)
            {
              FILE *file = fopen (candidate, "rb");
              if (file)
                {
                  fclose (file);
                  free (root);
                  return candidate;
                }
              free (candidate);
            }
        }
      free (root);
      if (!end)
        break;
      cursor = end + 1;
    }
  return NULL;
}

static int
print_module_signatures (const char *session, const char *module)
{
  const char *cursor = session;
  size_t module_len = module ? strlen (module) : 0;
  int found = 0;
  if (!session || !module)
    return 0;
  while (*cursor)
    {
      const char *line_end = strchr (cursor, '\n');
      const char *name = cursor;
      while (*name == ' ' || *name == '\t')
        name++;
      if (strncmp (name, "structure ", 10) == 0
          || strncmp (name, "signature ", 10) == 0)
        {
          name += 10;
        }
      else
        {
          cursor = line_end ? line_end + 1 : cursor + strlen (cursor);
          continue;
        }
      const char *end = name;
      if (strncmp (name, module, module_len) == 0
          && (name[module_len] == ' ' || name[module_len] == ')'
              || name[module_len] == '\0'))
        {
          end = line_end ? line_end : cursor + strlen (cursor);
          fwrite (cursor, 1, (size_t)(end - cursor), stdout);
          fputc ('\n', stdout);
          found = 1;
        }
      cursor = line_end ? line_end + 1 : cursor + strlen (cursor);
    }
  if (!found)
    fprintf (stderr, "sml-parthia: module %s is not open\n", module);
  return found;
}

static int
handle_directive (const char *line, ccw_sml_parthia_runtime *runtime,
                  char **session, size_t *session_len)
{
  char command[32] = { 0 };
  char argument[1024] = { 0 };
  const char *p = line;
  char *path;
  int consumed;
  while (isspace ((unsigned char)*p))
    p++;
  consumed = sscanf (p, "#%31s %1023[^\n]", command, argument);
  if (consumed < 1)
    return 0;
  if (strcmp (command, "help") == 0)
    {
      puts ("#help              list directives");
      puts ("#open MODULE       show a module's structure/signatures");
      puts ("#use \"FILE\"        load and compile SML source");
      puts ("#load LIB          load a native library from SML_PARTHIA_PATH");
      puts ("#quit              leave the REPL");
      return 1;
    }
  if (strcmp (command, "quit") == 0 || strcmp (command, "q") == 0)
    return -1;
  if (strcmp (command, "open") == 0)
    {
      char module[256];
      if (sscanf (argument, "%255s", module) != 1)
        {
          fputs ("sml-parthia: #open expects a module name\n", stderr);
          return 1;
        }
      print_module_signatures (*session ? *session : "", module);
      return 1;
    }
  if (strcmp (command, "load") == 0)
    {
      char name[1024];
      if (sscanf (argument, " \"%1023[^\"]\"", name) != 1
          && sscanf (argument, "%1023s", name) != 1)
        {
          fputs ("sml-parthia: #load expects a library name\n", stderr);
          return 1;
        }
      path = resolve_library (name);
      if (!path)
        {
          fprintf (stderr, "sml-parthia: %s not found in SML_PARTHIA_PATH\n",
                   name);
          return 1;
        }
      if (!ccw_sml_parthia_load_extension (runtime, path))
        fprintf (stderr, "sml-parthia: cannot load %s: %s\n", path,
                 strerror (errno));
      else
        printf ("loaded %s\n", name);
      free (path);
      return 1;
    }
  if (strcmp (command, "use") == 0)
    {
      char name[1024];
      ccw_sml_parthia_program *program;
      ccw_sml_parthia_report report;
      char *error = NULL;
      if (sscanf (argument, " \"%1023[^\"]\"", name) != 1)
        {
          fputs ("sml-parthia: #use expects a quoted source path\n", stderr);
          return 1;
        }
      path = resolve_library (name);
      if (!path)
        path = repl_strdup (name);
      program = ccw_sml_parthia_compile_file (runtime, path, &report, &error);
      if (!program)
        {
          fprintf (stderr, "sml-parthia: %s\n",
                   error ? error : "source load failed");
          free (error);
          free (path);
          return 1;
        }
      if (*session_len == 0)
        {
          free (*session);
          *session = repl_strdup (ccw_sml_parthia_surface_ast (program));
          *session_len = *session ? strlen (*session) : 0;
        }
      fputs (ccw_sml_parthia_core_ast (program), stdout);
      fputc ('\n', stdout);
      ccw_sml_parthia_program_destroy (program);
      free (error);
      free (path);
      return 1;
    }
  fprintf (stderr, "sml-parthia: unknown directive #%s (try #help)\n",
           command);
  return 1;
}

/*
 * Consume every complete phrase in source.  The first two consecutive
 * semicolons are the SML phrase terminator; any suffix remains buffered for
 * the next prompt.
 */
static int
process_phrases (char **source, size_t *source_len,
                 ccw_sml_parthia_runtime *runtime, char **session,
                 size_t *session_len)
{
  int status = 1;
  for (;;)
    {
      char *terminator;
      size_t phrase_length;
      size_t consumed;

      if (*source == NULL || *source_len < 2u)
        break;
      terminator = strstr (*source, ";;");
      if (terminator == NULL)
        break;
      phrase_length = (size_t)(terminator - *source);
      if (!run_phrase (*source, phrase_length, runtime))
        status = 0;
      else
        {
          size_t old_len = *session_len;
          char *grown
              = (char *)realloc (*session, old_len + phrase_length + 2u);
          if (!grown)
            status = 0;
          else
            {
              *session = grown;
              memcpy (*session + old_len, *source, phrase_length);
              (*session)[old_len + phrase_length] = '\n';
              *session_len = old_len + phrase_length + 1u;
              (*session)[*session_len] = '\0';
            }
        }

      consumed = phrase_length + 2u;
      memmove (*source, *source + consumed, *source_len - consumed);
      *source_len -= consumed;
      (*source)[*source_len] = '\0';
    }
  return status;
}

int
sml_parthia_repl (void)
{
  char *source = NULL;
  char *session = NULL;
  size_t source_len = 0;
  size_t session_len = 0;
  int status = 0;
  ccw_sml_parthia_runtime *runtime = ccw_sml_parthia_runtime_new ();

  if (!runtime)
    {
      fputs ("sml-parthia: cannot initialize REPL runtime\n", stderr);
      return 1;
    }

  linenoiseInstallWindowChangeHandler ();
  for (;;)
    {
      const char *prompt = source_len == 0
                               ? SML_PARTHIA_REPL_PROMPT
                               : SML_PARTHIA_REPL_CONTINUATION_PROMPT;
      char *line = linenoise (prompt);

      if (line == NULL)
        {
          if (linenoiseKeyType () == 1)
            {
              free (source);
              fputc ('\n', stdout);
              source = NULL;
              source_len = 0;
              continue;
            }
          if (source_len != 0)
            {
              fputs ("sml-parthia: unexpected end of input; expected ;;\n",
                     stderr);
              status = 1;
            }
          linenoiseHistoryFree ();
          free (session);
          ccw_sml_parthia_runtime_free (runtime);
          return status;
        }

      if (source_len == 0 && line[0] == '#')
        {
          int directive_status
              = handle_directive (line, runtime, &session, &session_len);
          free (line);
          if (directive_status < 0)
            {
              free (source);
              free (session);
              linenoiseHistoryFree ();
              ccw_sml_parthia_runtime_free (runtime);
              return status;
            }
          if (!directive_status)
            status = 1;
          continue;
        }
      if (!append_line (&source, &source_len, line))
        {
          free (line);
          free (source);
          free (session);
          ccw_sml_parthia_runtime_free (runtime);
          linenoiseHistoryFree ();
          fputs ("sml-parthia: out of memory\n", stderr);
          return 1;
        }
      free (line);
      if (!process_phrases (&source, &source_len, runtime, &session,
                            &session_len))
        status = 1;
    }
}
