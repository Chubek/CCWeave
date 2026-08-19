/* Cephyr preprocessor — wraps ucpp (§4).
 *
 * ucpp is a C11 implementation of translation phases 1–6. We use it as
 * a library (compiled with STAND_ALONE not defined) to produce a
 * preprocessed token stream. A line map is built so diagnostics always
 * report original source locations. */

#define _POSIX_C_SOURCE 200809L

#include "cephyr_cpp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kstring.h"
#include "kvec.h"

/* ucpp internal headers. We compile ucpp with STAND_ALONE undefined. */
#include "../../../third_party/ucpp/cpp.h"
#include "../../../third_party/ucpp/mem.h"
#include "../../../third_party/ucpp/tune.h"
#include "../../../third_party/ucpp/ucppi.h"

/* ---------- line-map builder ---------- */

typedef struct
{
  kvec_t (cephyr_line_map_entry) entries;
  kvec_t (char *) filenames;
  int current_line; /* 1-based, in output */
  int source_line;  /* 1-based, in current source file */
  int source_idx;   /* index into filenames[] */
} line_map_builder;

static char *
cephyr_cpp_strdup (const char *s)
{
  if (!s)
    return NULL;
  size_t n = strlen (s) + 1u;
  char *copy = malloc (n);
  if (copy)
    memcpy (copy, s, n);
  return copy;
}

static void
lmb_init (line_map_builder *lmb)
{
  kv_init (lmb->entries);
  kv_init (lmb->filenames);
  lmb->current_line = 1;
  lmb->source_line = 1;
  lmb->source_idx = -1;
}

static void
lmb_add_file (line_map_builder *lmb, const char *filename)
{
  kv_push (char *, lmb->filenames, cephyr_cpp_strdup (filename));
  lmb->source_idx = (int)kv_size (lmb->filenames) - 1;
}

static void
lmb_add_entry (line_map_builder *lmb, int output_line, int source_line,
               int source_idx)
{
  cephyr_line_map_entry entry
      = { .source_file = cephyr_cpp_strdup (
              (source_idx >= 0) ? lmb->filenames.a[source_idx] : "<builtin>"),
          .source_line = source_line,
          .source_column = 1,
          .output_line = output_line };
  kv_push (cephyr_line_map_entry, lmb->entries, entry);
}

static void
lmb_free (line_map_builder *lmb)
{
  for (size_t i = 0; i < kv_size (lmb->filenames); i++)
    free (kv_A (lmb->filenames, i));
  kv_destroy (lmb->filenames);
  kv_destroy (lmb->entries);
}

/* ---------- preprocessor wrapper ---------- */

cephyr_cpp_result
cephyr_cpp_preprocess (const char *source_text, size_t source_len,
                       const char *source_name,
                       const char *const *include_paths,
                       int include_path_count)
{
  return cephyr_cpp_preprocess_with_defines (source_text, source_len,
                                             source_name, include_paths,
                                             include_path_count, NULL, 0);
}

cephyr_cpp_result
cephyr_cpp_preprocess_with_defines (const char *source_text, size_t source_len,
                                    const char *source_name,
                                    const char *const *include_paths,
                                    int include_path_count,
                                    const char *const *defines,
                                    int define_count)
{
  return cephyr_cpp_preprocess_with_options (
      source_text, source_len, source_name, include_paths, include_path_count,
      defines, define_count, NULL, 0, NULL, 0);
}

cephyr_cpp_result
cephyr_cpp_preprocess_with_options (
    const char *source_text, size_t source_len, const char *source_name,
    const char *const *include_paths, int include_path_count,
    const char *const *defines, int define_count, const char *const *options,
    int option_count, const char *const *args, int arg_count)
{
  cephyr_cpp_result result;
  const char **extra_includes = NULL;
  const char **extra_defines = NULL;
  int extra_include_count = 0;
  int extra_define_count = 0;
  int undef_special = 0;
  memset (&result, 0, sizeof (result));

  /* Step 1: initialise ucpp */
  init_cpp ();

  /* Step 2: configure builtin macros */
  no_special_macros = 0;
  emit_defines = emit_assertions = 0;

  /* Step 3: initialise tables (with assertions) */
  init_tables (1);

  /* Step 4: interpret the subset of forwarded options supported by ucpp. */
  for (int pass = 0; pass < 2; ++pass)
    {
      const char *const *items = pass == 0 ? options : args;
      int item_count = pass == 0 ? option_count : arg_count;
      for (int i = 0; i < item_count; ++i)
        {
          const char *item = items[i];
          if (item == NULL)
            {
              result.error_message
                  = cephyr_cpp_strdup ("invalid ucpp preprocessor option");
              goto option_error;
            }
          if (!strncmp (item, "-D", 2) && item[2] != '\0')
            {
              const char **next = (const char **)realloc (
                  extra_defines,
                  (size_t)(extra_define_count + 1) * sizeof (*next));
              if (next == NULL)
                goto option_oom;
              extra_defines = next;
              extra_defines[extra_define_count++] = item + 2;
            }
          else if (!strcmp (item, "-D") && i + 1 < item_count
                   && items[i + 1] != NULL && *items[i + 1] != '\0')
            {
              const char **next = (const char **)realloc (
                  extra_defines,
                  (size_t)(extra_define_count + 1) * sizeof (*next));
              if (next == NULL)
                goto option_oom;
              extra_defines = next;
              extra_defines[extra_define_count++] = items[++i];
            }
          else if (!strncmp (item, "-I", 2) && item[2] != '\0')
            {
              const char **next = (const char **)realloc (
                  extra_includes,
                  (size_t)(extra_include_count + 1) * sizeof (*next));
              if (next == NULL)
                goto option_oom;
              extra_includes = next;
              extra_includes[extra_include_count++] = item + 2;
            }
          else if (!strcmp (item, "-I") && i + 1 < item_count
                   && items[i + 1] != NULL && *items[i + 1] != '\0')
            {
              const char **next = (const char **)realloc (
                  extra_includes,
                  (size_t)(extra_include_count + 1) * sizeof (*next));
              if (next == NULL)
                goto option_oom;
              extra_includes = next;
              extra_includes[extra_include_count++] = items[++i];
            }
          else if (!strcmp (item, "-undef"))
            {
              undef_special = 1;
            }
          else
            {
              result.error_message = cephyr_cpp_strdup (
                  "ucpp supports only -D, -I, and -undef forwarded options");
              goto option_error;
            }
        }
    }

  /* Step 5: set include paths */
  char **ucpp_include_paths = NULL;
  int total_include_count = include_path_count + extra_include_count;
  if (total_include_count > 0)
    {
      ucpp_include_paths = calloc ((size_t)total_include_count + 1u,
                                   sizeof (*ucpp_include_paths));
      if (!ucpp_include_paths)
        {
          goto option_oom;
        }
      for (int i = 0; i < include_path_count; i++)
        ucpp_include_paths[i] = (char *)include_paths[i];
      for (int i = 0; i < extra_include_count; i++)
        ucpp_include_paths[include_path_count + i] = (char *)extra_includes[i];
    }
  init_include_path (ucpp_include_paths);
  free (ucpp_include_paths);
  free (extra_includes);
  extra_includes = NULL;

  /* Step 6: dependencies */
  emit_dependencies = 0;

  /* Step 7: set the source filename */
  set_init_filename ((char *)(source_name ? source_name : "<input>"), 0);

  /* Step 8: initialise lexer */
  struct lexer_state ls;
  init_lexer_state (&ls);
  init_lexer_mode (&ls);
  ls.flags |= HANDLE_ASSERTIONS | HANDLE_PRAGMA | LINE_NUM | KEEP_OUTPUT;
  if (undef_special)
    no_special_macros = 1;

  for (int pass = 0; pass < 2; ++pass)
    {
      const char *const *items = pass == 0 ? defines : extra_defines;
      int item_count = pass == 0 ? define_count : extra_define_count;
      for (int i = 0; i < item_count; ++i)
        {
          if (items == NULL || items[i] == NULL)
            {
              result.error_message
                  = cephyr_cpp_strdup ("invalid preprocessor definition");
              free_lexer_state (&ls);
              free (extra_defines);
              wipeout ();
              return result;
            }
          if (define_macro (&ls, (char *)items[i]) != 0)
            {
              result.error_message
                  = cephyr_cpp_strdup ("invalid preprocessor definition");
              free_lexer_state (&ls);
              free (extra_defines);
              wipeout ();
              return result;
            }
        }
    }
  free (extra_defines);
  extra_defines = NULL;

  /* Step 9: feed source text through a temporary file */
  FILE *tmpf = tmpfile ();
  if (!tmpf)
    {
      result.error_message = cephyr_cpp_strdup (
          "failed to create temporary file for preprocessor");
      return result;
    }
  fwrite (source_text, 1, source_len, tmpf);
  rewind (tmpf);
  ls.input = tmpf;

  /* Step 10: collect output */
  kstring_t output = { 0, 0, NULL };
  line_map_builder lmb;
  lmb_init (&lmb);
  lmb_add_file (&lmb, source_name ? source_name : "<input>");
  enter_file (&ls, ls.flags);

  /* Step 11: tokenize */
  int tok;
  int status;
  int add_newline = 0;

  while ((status = lex (&ls)) < CPPERR_EOF)
    {
      if (status >= CPPERR)
        continue;
      tok = ls.ctok->type;
      if (tok == NEWLINE)
        {
          lmb.current_line++;
          if (add_newline)
            {
              (void)kputc ('\n', &output);
            }
          add_newline = 0;
        }
      else if (tok == CONTEXT)
        {
          /* #line directive or file change. */
          lmb_add_file (&lmb, ls.ctok->name ? ls.ctok->name : "<builtin>");
          lmb.source_line = (int)ls.ctok->line;
          add_newline = 0;
        }
      else if (tok == COMMENT)
        {
          /* Replace comments with a single space */
          if (output.l > 0 && output.s[output.l - 1] != ' '
              && output.s[output.l - 1] != '\n')
            {
              (void)kputc (' ', &output);
            }
        }
      else if (tok == NONE)
        {
          /* Whitespace — collapse to single space */
          if (output.l > 0 && output.s[output.l - 1] != ' '
              && output.s[output.l - 1] != '\n')
            {
              (void)kputc (' ', &output);
            }
        }
      else
        {
          /* Emit the token text */
          const char *token_text = token_name (ls.ctok);
          if (token_text)
            {
              if (output.l == 0 || output.s[output.l - 1] == '\n')
                lmb_add_entry (&lmb, lmb.current_line, (int)ls.ctok->line,
                               lmb.source_idx);
              size_t tlen = strlen (token_text);
              if (output.l > 0 && output.s[output.l - 1] != ' '
                  && output.s[output.l - 1] != '\n'
                  && output.s[output.l - 1] != '('
                  && output.s[output.l - 1] != '['
                  && output.s[output.l - 1] != '{' && *token_text != ')'
                  && *token_text != ']' && *token_text != '}'
                  && *token_text != ',' && *token_text != ';')
                {
                  (void)kputc (' ', &output);
                }
              (void)kputsn (token_text, (int)tlen, &output);
            }
          add_newline = 1;
        }
    }

  /* Add final newline */
  if (output.l > 0 && output.s[output.l - 1] != '\n')
    {
      (void)kputc ('\n', &output);
    }

  /* kstring maintains a trailing NUL; make an empty result NUL-terminated. */
  if (output.s == NULL)
    {
      (void)ks_resize (&output, 1);
      if (output.s)
        output.s[0] = '\0';
    }

  /* Cleanup */
  free_lexer_state (&ls);
  wipeout ();

  /* Build result */
  result.text_len = output.l;
  result.text = ks_release (&output);
  result.line_map = lmb.entries.a;
  result.line_map_count = (int)lmb.entries.n;
  lmb.entries.a = NULL; /* ownership transferred */
  lmb.entries.n = lmb.entries.m = 0;
  lmb_free (&lmb);

  return result;
option_oom:
  result.error_message = cephyr_cpp_strdup ("out of memory");
option_error:
  free (extra_includes);
  free (extra_defines);
  wipeout ();
  return result;
}

void
cephyr_cpp_result_free (cephyr_cpp_result *res)
{
  if (!res)
    return;
  free (res->text);
  for (int i = 0; i < res->line_map_count; i++)
    free ((char *)res->line_map[i].source_file);
  free (res->line_map);
  free (res->error_message);
  memset (res, 0, sizeof (*res));
}

const cephyr_line_map_entry *
cephyr_cpp_lookup_line (const cephyr_cpp_result *res, int output_line)
{
  if (!res || !res->line_map)
    return NULL;
  /* Binary search for the closest entry at or before output_line */
  int lo = 0, hi = res->line_map_count - 1;
  const cephyr_line_map_entry *best = NULL;
  while (lo <= hi)
    {
      int mid = (lo + hi) / 2;
      if (res->line_map[mid].output_line <= output_line)
        {
          best = &res->line_map[mid];
          lo = mid + 1;
        }
      else
        {
          hi = mid - 1;
        }
    }
  return best;
}

static int
append_shell_word (char **command, size_t *length, size_t *capacity,
                   const char *word)
{
  size_t needed = 3u;
  char *next;
  if (word == NULL)
    return 0;
  for (const char *p = word; *p; ++p)
    needed += (*p == '\'') ? 4u : 1u;
  if (*length + needed + 1u > *capacity)
    {
      size_t new_capacity = *capacity ? *capacity : 128u;
      while (*length + needed + 1u > new_capacity)
        new_capacity *= 2u;
      next = (char *)realloc (*command, new_capacity);
      if (next == NULL)
        return 0;
      *command = next;
      *capacity = new_capacity;
    }
  (*command)[(*length)++] = ' ';
  (*command)[(*length)++] = '\'';
  for (const char *p = word; *p; ++p)
    {
      if (*p == '\'')
        {
          memcpy (*command + *length, "'\\''", 4u);
          *length += 4u;
        }
      else
        {
          (*command)[(*length)++] = *p;
        }
    }
  (*command)[(*length)++] = '\'';
  (*command)[*length] = '\0';
  return 1;
}

char *
cephyr_cpp_external_with_options (const char *source_path,
                                  const char *cpp_command,
                                  const char *const *options, int option_count,
                                  const char *const *args, int arg_count,
                                  char **error_message)
{
  size_t command_length;
  size_t command_capacity;
  char *cmd;
  if (!cpp_command)
    {
      if (error_message)
        *error_message
            = cephyr_cpp_strdup ("no external preprocessor command set");
      return NULL;
    }

  command_capacity = strlen (cpp_command) + 128u;
  cmd = (char *)malloc (command_capacity);
  if (cmd == NULL)
    {
      if (error_message)
        *error_message = cephyr_cpp_strdup ("out of memory");
      return NULL;
    }
  command_length = strlen (cpp_command);
  memcpy (cmd, cpp_command, command_length + 1u);
  for (int i = 0; i < option_count; ++i)
    if (!append_shell_word (&cmd, &command_length, &command_capacity,
                            options[i]))
      goto oom;
  for (int i = 0; i < arg_count; ++i)
    if (!append_shell_word (&cmd, &command_length, &command_capacity, args[i]))
      goto oom;
  if (!append_shell_word (&cmd, &command_length, &command_capacity,
                          source_path))
    goto oom;

  FILE *pipe = popen (cmd, "r");
  free (cmd);
  if (!pipe)
    {
      if (error_message)
        {
          char buf[512];
          snprintf (buf, sizeof (buf),
                    "failed to run external preprocessor: %s", cpp_command);
          *error_message = cephyr_cpp_strdup (buf);
        }
      return NULL;
    }

  /* Read all output */
  kstring_t output = { 0, 0, NULL };
  char buf[4096];
  size_t n;
  while ((n = fread (buf, 1, sizeof (buf), pipe)) > 0)
    {
      if (kputsn (buf, (int)n, &output) == EOF)
        {
          free (output.s);
          (void)pclose (pipe);
          if (error_message)
            *error_message = cephyr_cpp_strdup ("out of memory");
          return NULL;
        }
    }

  int status = pclose (pipe);
  if (status != 0)
    {
      free (output.s);
      if (error_message)
        {
          char buf[512];
          snprintf (buf, sizeof (buf),
                    "external preprocessor exited with status %d", status);
          *error_message = cephyr_cpp_strdup (buf);
        }
      return NULL;
    }

  return ks_release (&output);
oom:
  free (cmd);
  if (error_message)
    *error_message = cephyr_cpp_strdup ("out of memory");
  return NULL;
}

char *
cephyr_cpp_external (const char *source_path, const char *cpp_command,
                     char **error_message)
{
  return cephyr_cpp_external_with_options (source_path, cpp_command, NULL, 0,
                                           NULL, 0, error_message);
}
