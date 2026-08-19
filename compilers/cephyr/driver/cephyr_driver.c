/* Cephyr driver — §8.
 *
 * CLI entry point, toolchain discovery, plugin loading, and Sched plan
 * orchestration. Ties together: preprocessor → Swaff C frontend →
 * sema → lowering → Sched plan execution. */

#define _POSIX_C_SOURCE 200809L

#include "cephyr_driver.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../cpp/cephyr_cpp.h"
#include "../include/cephyr-module.h"
#include "../lower/cephyr_lower.h"
#include "../profile/cephyr_profile.h"
#include "../sema/cephyr_ast.h"
#include "../sema/cephyr_sema.h"
#include "../stdlib/cephyr_stdlib.h"
#include "../stdmodule/cephyr_stdmodule.h"

#include "../../../ir/ccw_ir.h"
#include "../../../sched/sched.h"
#include "../../../swaff/ccw_swaff.h"
#include "../../../toolchain/ccwas/ccwas.h"
#include "../../../toolchain/ccwld/ccwld.h"

#include "kstring.h"
#include "kvec.h"

static char *
cephyr_driver_strdup (const char *s)
{
  kstring_t copy = { 0, 0, NULL };
  if (s == NULL || kputs (s, &copy) == EOF)
    return NULL;
  return ks_release (&copy);
}

/* ---------- default options ---------- */

void
cephyr_options_init (cephyr_options *opts, const char *source_path)
{
  memset (opts, 0, sizeof (*opts));
  opts->source_path = source_path;
  opts->opt_level = CEPHYR_O0;
  opts->target_triple = "x86_64-linux-gnu";
  opts->manifest_dir = "manifests";
}

/* ---------- toolchain discovery ---------- */

typedef struct
{
  const char *triple;
  const char *arch;
} cephyr_target_desc;

static const cephyr_target_desc cephyr_targets[]
    = { { "x86_64-linux-gnu", "x86-64" },
        { "x86_64-unknown-linux-gnu", "x86-64" },
        { "x86_64-pc-linux-gnu", "x86-64" },
        { "aarch64-linux-gnu", "aarch64" },
        { "aarch64-unknown-linux-gnu", "aarch64" },
        { "riscv64-linux-gnu", "riscv64" },
        { "riscv64-unknown-linux-gnu", "riscv64" },
        { "wasm32-unknown-unknown", "wasm32" },
        { "wasm32-wasi", "wasm32" },
        { NULL, NULL } };

static char *
cephyr_find_program (const char *name)
{
  const char *path;
  const char *p;
  if (!name || !*name)
    return NULL;
  if (strchr (name, '/'))
    return access (name, X_OK) == 0 ? cephyr_driver_strdup (name) : NULL;
  path = getenv ("PATH");
  if (!path)
    return NULL;
  for (p = path; *p;)
    {
      const char *end = strchr (p, ':');
      size_t n = end ? (size_t)(end - p) : strlen (p);
      char candidate[PATH_MAX];
      if (n + 1 + strlen (name) + 1 <= sizeof (candidate))
        {
          if (n == 0)
            snprintf (candidate, sizeof (candidate), "./%s", name);
          else
            snprintf (candidate, sizeof (candidate), "%.*s/%s", (int)n, p,
                      name);
          if (access (candidate, X_OK) == 0)
            return cephyr_driver_strdup (candidate);
        }
      p = end ? end + 1 : p + strlen (p);
    }
  return NULL;
}

const char *
cephyr_target_arch (const char *target_triple)
{
  if (target_triple == NULL)
    return NULL;
  for (size_t i = 0; cephyr_targets[i].triple != NULL; ++i)
    if (strcmp (target_triple, cephyr_targets[i].triple) == 0)
      return cephyr_targets[i].arch;
  return NULL;
}

void
cephyr_list_target_triples (FILE *out)
{
  if (out == NULL)
    out = stdout;
  for (size_t i = 0; cephyr_targets[i].triple != NULL; ++i)
    fprintf (out, "%s\n", cephyr_targets[i].triple);
}

const char *
cephyr_discover_assembler (const char *target_triple)
{
  const char *environment_assembler = getenv ("CEPHYR_AS");
  (void)target_triple;
  if (environment_assembler != NULL && *environment_assembler != '\0')
    return cephyr_driver_strdup (environment_assembler);
#ifdef CEPHYR_DEFAULT_ASSEMBLER_PATH
  if (access (CEPHYR_DEFAULT_ASSEMBLER_PATH, X_OK) == 0)
    return cephyr_driver_strdup (CEPHYR_DEFAULT_ASSEMBLER_PATH);
#endif
  /* CCWAS is the default; retain system fallbacks for external builds. */
  static const char *candidates[] = { "ccwas",   "x86_64-linux-gnu-ccwas",
                                      "as",      "x86_64-linux-gnu-as",
                                      "llvm-mc", NULL };
  for (int i = 0; candidates[i]; i++)
    {
      char *found = cephyr_find_program (candidates[i]);
      if (found)
        return found;
    }
  return cephyr_driver_strdup ("ccwas");
}

const char *
cephyr_discover_linker (const char *target_triple)
{
  const char *environment_linker = getenv ("CEPHYR_LD");
  if (environment_linker != NULL && *environment_linker != '\0')
    return cephyr_driver_strdup (environment_linker);
  (void)target_triple;
#ifdef CEPHYR_DEFAULT_LINKER_PATH
  if (access (CEPHYR_DEFAULT_LINKER_PATH, X_OK) == 0)
    return cephyr_driver_strdup (CEPHYR_DEFAULT_LINKER_PATH);
#endif
  /* CCWld is the default; retain system fallbacks for external builds. */
  static const char *candidates[] = { "ccwld", "x86_64-linux-gnu-ccwld",
                                      "ld",    "x86_64-linux-gnu-ld",
                                      "lld",   NULL };
  for (int i = 0; candidates[i]; i++)
    {
      char *found = cephyr_find_program (candidates[i]);
      if (found)
        return found;
    }
  return cephyr_driver_strdup ("ccwld"); /* fallback */
}

/* ---------- error strings ---------- */

const char *
cephyr_result_string (cephyr_result r)
{
  switch (r)
    {
    case CEPHYR_SUCCESS:
      return "success";
    case CEPHYR_ERR_PREPROCESSOR:
      return "preprocessor error";
    case CEPHYR_ERR_PARSE:
      return "parse error";
    case CEPHYR_ERR_SEMA:
      return "semantic error";
    case CEPHYR_ERR_LOWER:
      return "lowering error";
    case CEPHYR_ERR_SCHED:
      return "scheduler error";
    case CEPHYR_ERR_ASSEMBLE:
      return "assembler error";
    case CEPHYR_ERR_LINK:
      return "linker error";
    case CEPHYR_ERR_INTERNAL:
      return "internal compiler error";
    default:
      return "unknown error";
    }
}

/* ---------- profile and Sched helpers ---------- */

static int
profile_scalar_safe (const char *value)
{
  if (value == NULL || *value == '\0')
    return 0;
  for (const unsigned char *p = (const unsigned char *)value; *p; ++p)
    if (*p == '"' || *p == '\\' || *p == '\n' || *p == '\r')
      return 0;
  return 1;
}

static char *
profile_path_dirname (const char *path)
{
  char *copy;
  char *slash;
  if (path == NULL)
    return cephyr_driver_strdup (".");
  copy = cephyr_driver_strdup (path);
  if (copy == NULL)
    return NULL;
  slash = strrchr (copy, '/');
  if (slash == NULL)
    {
      strcpy (copy, ".");
    }
  else if (slash == copy)
    {
      copy[1] = '\0';
    }
  else
    {
      *slash = '\0';
    }
  return copy;
}

static char *
profile_resolve_path (const char *profile_path, const char *value)
{
  char *dir;
  kstring_t result = { 0, 0, NULL };
  if (value == NULL)
    return NULL;
  if (value[0] == '/')
    return cephyr_driver_strdup (value);
  dir = profile_path_dirname (profile_path);
  if (dir == NULL)
    return NULL;
  if (ksprintf (&result, "%s/%s", dir, value) < 0)
    {
      free (dir);
      return NULL;
    }
  free (dir);
  return ks_release (&result);
}

static int
profile_opt_level (const char *value, cephyr_opt_level *out)
{
  if (value == NULL || out == NULL)
    return 0;
  if (!strcmp (value, "O0") || !strcmp (value, "0"))
    *out = CEPHYR_O0;
  else if (!strcmp (value, "O1") || !strcmp (value, "1"))
    *out = CEPHYR_O1;
  else if (!strcmp (value, "O2") || !strcmp (value, "2"))
    *out = CEPHYR_O2;
  else
    return 0;
  return 1;
}

static int
profile_has_explicit_plan (const cephyr_profile *profile)
{
  return profile != NULL
         && (profile->kernel_count != 0 || profile->rewrite_count != 0);
}

static int
merge_string_lists (const char *const *first, size_t first_count,
                    const char *const *second, size_t second_count,
                    const char ***out, int *out_count)
{
  size_t total = first_count + second_count;
  kvec_t (const char *) merged = { 0, 0, NULL };
  if (out == NULL || out_count == NULL || total > (size_t)INT_MAX)
    return 0;
  *out = NULL;
  *out_count = 0;
  if (total == 0)
    return 1;
  if (kv_resize (const char *, merged, total) == NULL)
    return 0;
  for (size_t i = 0; i < first_count; ++i)
    merged.a[i] = first[i];
  for (size_t i = 0; i < second_count; ++i)
    merged.a[first_count + i] = second[i];
  *out = merged.a;
  *out_count = (int)total;
  return 1;
}

/* Three-source merge (profile + CLI + stdlib manifest). The stdlib lists
 * always come last: user -I paths win over system includes, and the
 * standard library archives link after user libraries. */
static int
merge_string_lists3 (const char *const *first, size_t first_count,
                     const char *const *second, size_t second_count,
                     const char *const *third, size_t third_count,
                     const char ***out, int *out_count)
{
  const char **head = NULL;
  int head_count = 0;
  int ok;

  ok = merge_string_lists (first, first_count, second, second_count, &head,
                           &head_count);
  if (!ok)
    return 0;
  ok = merge_string_lists ((const char *const *)head, (size_t)head_count,
                           third, third_count, out, out_count);
  free (head);
  return ok;
}

static int
write_explicit_sched_script (const cephyr_profile *profile, char **path_out,
                             char *error_message, size_t error_capacity)
{
  char template_path[] = "/tmp/cephyr-profile-XXXXXX";
  int fd;
  FILE *file;
  unsigned node_number = 0;
  unsigned previous = 0;

  if (profile == NULL || path_out == NULL)
    return 0;
  *path_out = NULL;
  fd = mkstemp (template_path);
  if (fd < 0)
    {
      snprintf (error_message, error_capacity,
                "cannot create temporary Sched script: %s", strerror (errno));
      return 0;
    }
  file = fdopen (fd, "w");
  if (file == NULL)
    {
      close (fd);
      unlink (template_path);
      snprintf (error_message, error_capacity,
                "cannot open temporary Sched script: %s", strerror (errno));
      return 0;
    }
  fputs ("local S = sched.new \"Cephyr-profile\"\n", file);

  for (size_t i = 0; i < profile->kernel_count; ++i)
    {
      const cephyr_profile_kernel *kernel = &profile->kernels[i];
      char variable[32];
      if ((kernel->name == NULL) == (kernel->capability == NULL)
          || !profile_scalar_safe (kernel->name ? kernel->name
                                                : kernel->capability)
          || (kernel->prefer != NULL && !profile_scalar_safe (kernel->prefer)))
        {
          snprintf (
              error_message, error_capacity,
              "each profile kernel needs exactly one safe name or capability");
          fclose (file);
          unlink (template_path);
          return 0;
        }
      snprintf (variable, sizeof (variable), "n%u", node_number++);
      if (kernel->name != NULL)
        fprintf (file, "local %s = S:require { kernel = \"%s\" }\n", variable,
                 kernel->name);
      else if (kernel->prefer != NULL)
        fprintf (
            file,
            "local %s = S:require { capability = \"%s\", prefer = \"%s\" }\n",
            variable, kernel->capability, kernel->prefer);
      else
        fprintf (file, "local %s = S:require { capability = \"%s\" }\n",
                 variable, kernel->capability);
      if (previous != 0)
        fprintf (file, "S:edge(n%u, %s)\n", previous - 1u, variable);
      previous = node_number;
    }

  for (size_t i = 0; i < profile->rewrite_count; ++i)
    {
      char variable[32];
      const char *pattern = profile->rewrites[i];
      if (!profile_scalar_safe (pattern))
        {
          snprintf (error_message, error_capacity,
                    "profile rewrite patterns must be scalar strings");
          fclose (file);
          unlink (template_path);
          return 0;
        }
      snprintf (variable, sizeof (variable), "n%u", node_number++);
      fprintf (file, "local %s = S:rewrite \"%s\"\n", variable, pattern);
      if (previous != 0)
        fprintf (file, "S:edge(n%u, %s)\n", previous - 1u, variable);
      previous = node_number;
    }
  if (node_number == 0)
    {
      snprintf (error_message, error_capacity,
                "explicit profile must mention a kernel or rewrite");
      fclose (file);
      unlink (template_path);
      return 0;
    }
  fputs ("return S:seal()\n", file);
  if (fclose (file) != 0)
    {
      snprintf (error_message, error_capacity,
                "cannot finish temporary Sched script");
      unlink (template_path);
      return 0;
    }
  *path_out = cephyr_driver_strdup (template_path);
  if (*path_out == NULL)
    {
      unlink (template_path);
      snprintf (error_message, error_capacity,
                "out of memory creating Sched script");
      return 0;
    }
  return 1;
}

/* ---------- read source file ---------- */

static char *
read_file (const char *path, size_t *out_len)
{
  FILE *f = fopen (path, "r");
  char buffer[4096];
  kstring_t text = { 0, 0, NULL };
  if (!f)
    return NULL;
  while (!feof (f))
    {
      size_t n = fread (buffer, 1, sizeof (buffer), f);
      if (n != 0 && kputsn (buffer, (int)n, &text) == EOF)
        {
          fclose (f);
          free (text.s);
          return NULL;
        }
      if (ferror (f))
        {
          fclose (f);
          free (text.s);
          return NULL;
        }
    }
  fclose (f);
  if (out_len)
    *out_len = text.l;
  return ks_release (&text);
}

static const char *
source_extension (const char *path)
{
  const char *slash;
  const char *dot;
  if (path == NULL)
    return NULL;
  slash = strrchr (path, '/');
  dot = strrchr (path, '.');
  if (dot == NULL || (slash != NULL && dot < slash) || dot[1] == '\0')
    return "";
  return dot;
}

static int
extension_in_list (const char *extension, const char *list)
{
  const char *p = list;
  if (extension == NULL || list == NULL)
    return 0;
  while (*p != '\0')
    {
      const char *start;
      size_t length;
      while (*p == ',' || *p == ':' || *p == ';' || *p == ' ' || *p == '\t')
        ++p;
      start = p;
      while (*p != '\0' && *p != ',' && *p != ':' && *p != ';' && *p != ' '
             && *p != '\t')
        ++p;
      length = (size_t)(p - start);
      if (length != 0 && strlen (extension) == length
          && strncmp (extension, start, length) == 0)
        return 1;
    }
  return 0;
}

static int
is_assembly_source (const char *path)
{
  const char *extension = source_extension (path);
  const char *extra = getenv ("CEPHYR_AS_EXTENSIONS");
  if (extension == NULL)
    return 0;
  if (!strcmp (extension, ".s") || !strcmp (extension, ".as")
      || !strcmp (extension, ".asm") || !strcmp (extension, ".S"))
    return 1;
  return extension_in_list (extension, extra);
}

static int
is_preprocessed_assembly_source (const char *path)
{
  return source_extension (path) != NULL
         && !strcmp (source_extension (path), ".S");
}

static int
shell_append_quoted (kstring_t *command, const char *value)
{
  const char *p;
  if (value == NULL)
    return 0;
  if (kputc ('\'', command) == EOF)
    return 0;
  for (p = value; *p != '\0'; ++p)
    {
      if (*p == '\'')
        {
          if (kputs ("'\\''", command) == EOF)
            return 0;
        }
      else if (kputc (*p, command) == EOF)
        {
          return 0;
        }
    }
  return kputc ('\'', command) != EOF;
}

static int
run_external_assembler (const cephyr_options *opts, const char *input_path,
                        const char *output_path)
{
  kstring_t command = { 0, 0, NULL };
  int status;
  if (opts == NULL || opts->assembler == NULL || input_path == NULL
      || output_path == NULL)
    return 0;
  if (kputs (opts->assembler, &command) == EOF)
    goto oom;
  for (int i = 0; i < opts->assembler_option_count; ++i)
    {
      if (kputc (' ', &command) == EOF
          || !shell_append_quoted (&command, opts->assembler_options[i]))
        goto oom;
    }
  for (int i = 0; i < opts->assembler_arg_count; ++i)
    {
      if (kputc (' ', &command) == EOF
          || !shell_append_quoted (&command, opts->assembler_args[i]))
        goto oom;
    }
  if (kputs (" -o ", &command) == EOF
      || !shell_append_quoted (&command, output_path)
      || kputc (' ', &command) == EOF
      || !shell_append_quoted (&command, input_path)
      || kputc ('\0', &command) == EOF)
    goto oom;
  status = system (command.s);
  free (command.s);
  return status >= 0 && WIFEXITED (status) && WEXITSTATUS (status) == 0;
oom:
  free (command.s);
  return 0;
}

static cephyr_result
link_object (const cephyr_options *opts, const char *object_path)
{
  const char *inputs[1] = { object_path };
  ccwld_link_options link_options;
  ccwld_error error;
  const char *output_path = opts->output_path;
  const char *kind = opts->shared ? "dso" : opts->pie ? "pie" : "exe";
  memset (&link_options, 0, sizeof (link_options));
  memset (&error, 0, sizeof (error));
  if (output_path == NULL || strcmp (output_path, "-") == 0)
    output_path = "a.out";
  link_options.kind = kind;
  link_options.format = "elf";
  link_options.entry = "_main";
  link_options.search_paths = opts->library_paths;
  link_options.search_path_count = (size_t)opts->library_path_count;
  if (!ccwld_link_files (opts->target_triple, output_path, inputs, 1,
                         &link_options, &error))
    {
      fprintf (stderr, "cephyr: linker error: %s\n",
               error.message[0] ? error.message : "unknown error");
      return CEPHYR_ERR_LINK;
    }
  return CEPHYR_SUCCESS;
}

static cephyr_result
write_stage_text (const char *path, const char *text)
{
  FILE *out;
  if (path == NULL || strcmp (path, "-") == 0)
    return fputs (text ? text : "", stdout) < 0 ? CEPHYR_ERR_INTERNAL
                                                : CEPHYR_SUCCESS;
  out = fopen (path, "w");
  if (out == NULL)
    {
      fprintf (stderr, "cephyr: error: cannot write to '%s'\n", path);
      return CEPHYR_ERR_INTERNAL;
    }
  if (fputs (text ? text : "", out) < 0 || fclose (out) != 0)
    {
      fprintf (stderr, "cephyr: error: cannot write to '%s'\n", path);
      return CEPHYR_ERR_INTERNAL;
    }
  return CEPHYR_SUCCESS;
}

/* Backend emission façade.  Codegen kernels annotate/mutate the canonical IR;
 * this deterministic textual fallback gives the assembler a concrete target
 * program until target-specific instruction printers are available. */
static char *
emit_target_assembly (const ccw_ir *ir, const char *triple)
{
  const char *arch = cephyr_target_arch (triple);
  kstring_t out = { 0, 0, NULL };
  int functions;
  if (ir == NULL || arch == NULL)
    return NULL;
#define APPEND_FMT(...)                                                       \
  do                                                                          \
    {                                                                         \
      if (ksprintf (&out, __VA_ARGS__) < 0)                                   \
        {                                                                     \
          free (out.s);                                                       \
          return NULL;                                                        \
        }                                                                     \
    }                                                                         \
  while (0)
  if (strcmp (arch, "wasm32") == 0)
    {
      APPEND_FMT (";; generated by cephyr for %s\n", triple);
    }
  else
    {
      APPEND_FMT (".text\n");
    }
  functions = ccw_ir_function_count (ir);
  for (int i = 0; i < functions; ++i)
    {
      ccw_node fn = ccw_ir_function_ref (ir, i);
      const char *name = ccw_ir_function_name (ir, fn);
      if (name == NULL || *name == '\0')
        continue;
      if (strcmp (arch, "x86-64") == 0)
        {
          APPEND_FMT (".global %s\n.type %s, @function\n%s:\n"
                      "  xor eax, eax\n  ret\n.size %s, .-%s\n",
                      name, name, name, name, name);
        }
      else if (strcmp (arch, "aarch64") == 0)
        {
          APPEND_FMT (".global %s\n.type %s, @function\n%s:\n"
                      "  mov w0, #0\n  ret\n.size %s, .-%s\n",
                      name, name, name, name, name);
        }
      else if (strcmp (arch, "riscv64") == 0)
        {
          APPEND_FMT (".global %s\n.type %s, @function\n%s:\n"
                      "  li a0, 0\n  ret\n.size %s, .-%s\n",
                      name, name, name, name, name);
        }
      else
        {
          APPEND_FMT (
              ".func %s (result i32)\n  i32.const 0\n  return\n.endfunc\n",
              name);
        }
    }
#undef APPEND_FMT
  return ks_release (&out);
}

static ccw_arch_t
cephyr_ccwas_arch (const char *arch)
{
  if (strcmp (arch, "x86-64") == 0)
    return CCW_ARCH_X86_64;
  if (strcmp (arch, "aarch64") == 0)
    return CCW_ARCH_AARCH64;
  if (strcmp (arch, "riscv64") == 0)
    return CCW_ARCH_RISCV64;
  return CCW_ARCH_WASM32;
}

static cephyr_result
compile_assembly_source (const cephyr_options *opts)
{
  char *source = NULL;
  char *preprocessed = NULL;
  char *error_message = NULL;
  char input_template[] = "/tmp/cephyr-asm-input-XXXXXX";
  char object_template[] = "/tmp/cephyr-asm-object-XXXXXX";
  const char *assembly_text;
  char *input_path = NULL;
  char *object_path = NULL;
  int fd = -1;
  cephyr_result result = CEPHYR_SUCCESS;

  source = read_file (opts->source_path, NULL);
  if (source == NULL)
    {
      fprintf (stderr, "cephyr: error: cannot read assembly file '%s'\n",
               opts->source_path);
      return CEPHYR_ERR_INTERNAL;
    }
  assembly_text = source;

  if (is_preprocessed_assembly_source (opts->source_path))
    {
      const char *cpp_command = opts->cpp_command;
      if (cpp_command == NULL || *cpp_command == '\0')
        cpp_command = "cpp -E -P";
      if (cpp_command != NULL)
        {
          preprocessed = cephyr_cpp_external_with_options (
              opts->source_path, cpp_command, opts->preprocessor_options,
              opts->preprocessor_option_count, opts->preprocessor_args,
              opts->preprocessor_arg_count, &error_message);
          if (preprocessed == NULL)
            {
              fprintf (stderr, "cephyr: preprocessor error: %s\n",
                       error_message ? error_message : "unknown error");
              free (error_message);
              free (source);
              return CEPHYR_ERR_PREPROCESSOR;
            }
        }
      else
        {
          cephyr_cpp_result cpp = cephyr_cpp_preprocess_with_options (
              source, strlen (source), opts->source_path, opts->include_paths,
              opts->include_path_count, opts->defines, opts->define_count,
              opts->preprocessor_options, opts->preprocessor_option_count,
              opts->preprocessor_args, opts->preprocessor_arg_count);
          if (cpp.error_message != NULL)
            {
              fprintf (stderr, "cephyr: preprocessor error: %s\n",
                       cpp.error_message);
              cephyr_cpp_result_free (&cpp);
              free (source);
              return CEPHYR_ERR_PREPROCESSOR;
            }
          preprocessed = cpp.text;
          cpp.text = NULL;
          cephyr_cpp_result_free (&cpp);
        }
      assembly_text = preprocessed;
    }

  if (opts->stop_stage == CEPHYR_STOP_PREPROCESS)
    result = write_stage_text (opts->output_path, assembly_text);
  else if (opts->stop_stage == CEPHYR_STOP_ASSEMBLER_SCRIPT)
    result = write_stage_text (opts->output_path, assembly_text);
  else
    {
      if (opts->stop_stage == CEPHYR_STOP_LINK && opts->output_path != NULL
          && strcmp (opts->output_path, "-") != 0)
        object_path = cephyr_driver_strdup (opts->output_path);
      else
        {
          fd = mkstemp (object_template);
          if (fd >= 0)
            close (fd);
          object_path
              = fd >= 0 ? cephyr_driver_strdup (object_template) : NULL;
        }
      fd = mkstemp (input_template);
      if (fd >= 0)
        close (fd);
      input_path = fd >= 0 ? cephyr_driver_strdup (input_template) : NULL;
      if (object_path == NULL || input_path == NULL
          || write_stage_text (input_path, assembly_text) != CEPHYR_SUCCESS)
        {
          result = CEPHYR_ERR_ASSEMBLE;
        }
      else if (opts->assembler_external)
        {
          if (!run_external_assembler (opts, input_path, object_path))
            {
              fprintf (stderr, "cephyr: assembler error invoking '%s'\n",
                       opts->assembler ? opts->assembler : "(null)");
              result = CEPHYR_ERR_ASSEMBLE;
            }
        }
      else
        {
          ccwas_options aopts
              = { cephyr_ccwas_arch (cephyr_target_arch (opts->target_triple)),
                  "intel",
                  CCW_FMT_ELF,
                  NULL,
                  0,
                  0,
                  0,
                  0 };
          if (!ccwas_assemble_file (input_path, &aopts, object_path,
                                    &error_message))
            {
              fprintf (stderr, "cephyr: assembler error: %s\n",
                       error_message ? error_message : "unknown error");
              ccwas_free_error (error_message);
              result = CEPHYR_ERR_ASSEMBLE;
            }
        }
      if (result == CEPHYR_SUCCESS && opts->stop_stage == CEPHYR_STOP_NONE)
        result = link_object (opts, object_path);
      if (!opts->keep_temp && input_path != NULL)
        unlink (input_path);
      if (!opts->keep_temp
          && (opts->stop_stage != CEPHYR_STOP_LINK
              || opts->output_path == NULL)
          && object_path != NULL)
        unlink (object_path);
    }
  free (input_path);
  free (object_path);
  free (preprocessed);
  free (source);
  return result;
}

/* ---------- Sched plan execution ---------- */

static cephyr_result
run_sched_plan (ccw_ir *ir, const cephyr_options *opts)
{
  /* Determine the Sched script path */
  const char *script_name = NULL;
  const char *manifest_dir
      = opts->manifest_dir ? opts->manifest_dir : "manifests";
  switch (opts->opt_level)
    {
    case CEPHYR_O0:
      script_name = "O0.lua";
      break;
    case CEPHYR_O1:
      script_name = "O1.lua";
      break;
    case CEPHYR_O2:
      script_name = "O2.lua";
      break;
    }

  /* Build the full path to the script */
  char script_path[1024];
  if (opts->sched_script != NULL)
    {
      snprintf (script_path, sizeof (script_path), "%s", opts->sched_script);
    }
  else
    {
      snprintf (script_path, sizeof (script_path), "compilers/cephyr/sched/%s",
                script_name);
    }

  /* Try to find the script */
  FILE *test = fopen (script_path, "r");
  if (!test)
    {
      if (opts->sched_script == NULL)
        {
          snprintf (script_path, sizeof (script_path),
                    "../compilers/cephyr/sched/%s", script_name);
          test = fopen (script_path, "r");
        }
    }
  if (!test)
    {
      fprintf (stderr, "cephyr: error: cannot find Sched script '%s'\n",
               opts->sched_script ? opts->sched_script : script_name);
      return CEPHYR_ERR_SCHED;
    }
  fclose (test);

  /* Load and run the Sched script */
  ccw_sched_error err;
  memset (&err, 0, sizeof (err));
  ccw_plan *plan = NULL;
  int rc = ccw_sched_run_script (script_path, manifest_dir, &plan, &err);
  if (rc == 0)
    {
      fprintf (stderr, "cephyr: sched error: %s\n", err.message);
      return CEPHYR_ERR_SCHED;
    }

  if (plan)
    {
      ccw_oeuph_budget budget = ccw_oeuph_default_budget ();
      if (!ccw_plan_apply_rewrites (plan, ir, manifest_dir, budget,
                                    CCW_COST_PERFORMANCE, NULL, 0, NULL, &err))
        {
          fprintf (stderr, "cephyr: rewrite error: %s\n", err.message);
          ccw_plan_free (plan);
          return CEPHYR_ERR_SCHED;
        }
      /* Write the plan for debugging */
      const char *plan_name = opts->sched_script ? "profile" : script_name;
      char plan_path[1024];
      snprintf (plan_path, sizeof (plan_path),
                "compilers/cephyr/sched/plans/%s.plan", plan_name);
      ccw_plan_write (plan, plan_path, &err);
      ccw_plan_free (plan);
    }

  /* Kernel nodes remain host/executor responsibilities; rewrite nodes are
   * applied above through Oeuph. */
  if (opts->emit_ir)
    {
      char *ir_text = ccw_ir_print (ir);
      if (ir_text)
        {
          printf ("%s\n", ir_text);
          free (ir_text);
        }
    }

  return CEPHYR_SUCCESS;
}

/* ---------- main compilation pipeline ---------- */

static cephyr_result
cephyr_compile_inner (const cephyr_options *opts)
{
  cephyr_result result = CEPHYR_SUCCESS;
  char *source_text = NULL;
  size_t source_len = 0;
  char *preprocessed = NULL;
  char *error_msg = NULL;
  cephyr_cpp_result cpp_res;
  memset (&cpp_res, 0, sizeof (cpp_res));

  if (cephyr_target_arch (opts->target_triple) == NULL)
    {
      fprintf (stderr,
               "cephyr: unsupported target triple '%s' (use --lstriples)\n",
               opts->target_triple ? opts->target_triple : "");
      return CEPHYR_ERR_INTERNAL;
    }

  if (is_assembly_source (opts->source_path))
    return compile_assembly_source (opts);

  /* Step 1: Read source file */
  source_text = read_file (opts->source_path, &source_len);
  if (!source_text)
    {
      fprintf (stderr, "cephyr: error: cannot read source file '%s'\n",
               opts->source_path);
      return CEPHYR_ERR_INTERNAL;
    }

  /* Step 2: Preprocess */
  if (opts->cpp_command)
    {
      /* Use external preprocessor */
      preprocessed = cephyr_cpp_external_with_options (
          opts->source_path, opts->cpp_command, opts->preprocessor_options,
          opts->preprocessor_option_count, opts->preprocessor_args,
          opts->preprocessor_arg_count, &error_msg);
      if (!preprocessed)
        {
          fprintf (stderr, "cephyr: preprocessor error: %s\n",
                   error_msg ? error_msg : "unknown error");
          free (error_msg);
          free (source_text);
          return CEPHYR_ERR_PREPROCESSOR;
        }
      cpp_res.text = preprocessed;
      cpp_res.text_len = strlen (preprocessed);
    }
  else
    {
      /* Use ucpp */
      cpp_res = cephyr_cpp_preprocess_with_options (
          source_text, source_len, opts->source_path, opts->include_paths,
          opts->include_path_count, opts->defines, opts->define_count,
          opts->preprocessor_options, opts->preprocessor_option_count,
          opts->preprocessor_args, opts->preprocessor_arg_count);
      if (cpp_res.error_message)
        {
          fprintf (stderr, "cephyr: preprocessor error: %s\n",
                   cpp_res.error_message);
          cephyr_cpp_result_free (&cpp_res);
          free (source_text);
          return CEPHYR_ERR_PREPROCESSOR;
        }
    }

  if (opts->stop_stage == CEPHYR_STOP_PREPROCESS)
    {
      result = write_stage_text (opts->output_path, cpp_res.text);
      cephyr_cpp_result_free (&cpp_res);
      free (source_text);
      return result;
    }

  /* Step 3: Parse with Swaff C frontend */
  if (!ccw_swaff_available ())
    {
      fprintf (stderr, "cephyr: error: Swaff C frontend not available\n");
      cephyr_cpp_result_free (&cpp_res);
      free (source_text);
      return CEPHYR_ERR_PARSE;
    }

  const ccw_swaff_frontend *fe = ccw_swaff_frontend_c ();
  ccw_swaff_report report;
  memset (&report, 0, sizeof (report));

  ccw_ir *ir = ccw_swaff_lower (
      fe, cpp_res.text, cpp_res.text_len, opts->source_path, CCW_PROFILE_TILLY,
      CCW_SWAFF_RECOVER_ON_ERROR, &report, &error_msg);

  if (!ir)
    {
      fprintf (stderr, "cephyr: parse error: %s\n",
               error_msg ? error_msg : "unknown error");
      fprintf (stderr,
               "  Swaff report: %d errors, %d missing, %d recovered, %d "
               "unsupported\n",
               report.error_nodes, report.missing_nodes,
               report.recovered_subtrees, report.unsupported_nodes);
      free (error_msg);
      cephyr_cpp_result_free (&cpp_res);
      free (source_text);
      return CEPHYR_ERR_PARSE;
    }

  /* Step 4: Semantic analysis
   * In v0.1, the Swaff C adapter already produces Weave IR directly.
   * The sema layer operates on a typed AST; for v0.1 we validate
   * the IR and report diagnostics. The full typed-AST pipeline is
   * staged for v0.2, when the Swaff→IR lowering is completed. */
  char *validate_err = NULL;
  ccw_status validate_rc = ccw_ir_validate (ir, &validate_err);
  if (validate_rc != CCW_OK)
    {
      fprintf (stderr, "cephyr: IR validation error: %s\n",
               validate_err ? validate_err : "unknown");
      free (validate_err);
      ccw_ir_module_destroy (ir);
      cephyr_cpp_result_free (&cpp_res);
      free (source_text);
      return CEPHYR_ERR_SEMA;
    }

  /* Step 5: Run the Sched plan */
  result = run_sched_plan (ir, opts);

  /* Step 6: emit target assembly, then assemble unless -S was requested. */
  if (result == CEPHYR_SUCCESS && !opts->emit_ir)
    {
      char *assembly = emit_target_assembly (ir, opts->target_triple);
      char assembly_template[] = "/tmp/cephyr-asm-XXXXXX";
      char object_template[] = "/tmp/cephyr-obj-XXXXXX";
      char *assembly_path = NULL;
      char *object_path = NULL;
      int fd;
      if (assembly == NULL)
        {
          result = CEPHYR_ERR_INTERNAL;
        }
      else if (opts->stop_stage == CEPHYR_STOP_ASSEMBLER_SCRIPT)
        {
          result = write_stage_text (opts->output_path, assembly);
        }
      else
        {
          fd = mkstemp (assembly_template);
          if (fd >= 0)
            {
              close (fd);
              assembly_path = cephyr_driver_strdup (assembly_template);
            }
          if (opts->output_path != NULL
              && strcmp (opts->output_path, "-") != 0)
            {
              object_path = cephyr_driver_strdup (opts->output_path);
            }
          else
            {
              fd = mkstemp (object_template);
              if (fd >= 0)
                {
                  close (fd);
                  object_path = cephyr_driver_strdup (object_template);
                }
            }
          if (assembly_path == NULL || object_path == NULL
              || write_stage_text (assembly_path, assembly) != CEPHYR_SUCCESS)
            {
              result = CEPHYR_ERR_ASSEMBLE;
            }
          else
            {
              ccwas_options aopts = { cephyr_ccwas_arch (cephyr_target_arch (
                                          opts->target_triple)),
                                      "intel",
                                      CCW_FMT_ELF,
                                      NULL,
                                      0,
                                      0,
                                      0,
                                      0 };
              char *assemble_error = NULL;
              if (!ccwas_assemble_file (assembly_path, &aopts, object_path,
                                        &assemble_error))
                {
                  fprintf (stderr, "cephyr: assembler error: %s\n",
                           assemble_error ? assemble_error : "unknown error");
                  ccwas_free_error (assemble_error);
                  result = CEPHYR_ERR_ASSEMBLE;
                }
              else if (opts->output_path == NULL
                       || strcmp (opts->output_path, "-") == 0)
                {
                  fprintf (stderr, "cephyr: assembled object written to %s\n",
                           object_path);
                }
            }
          if (!opts->keep_temp && assembly_path != NULL)
            unlink (assembly_path);
          if ((opts->output_path == NULL
               || strcmp (opts->output_path, "-") == 0)
              && !opts->keep_temp && object_path != NULL)
            unlink (object_path);
        }
      free (assembly_path);
      free (object_path);
      free (assembly);
    }

  /* Cleanup */
  ccw_ir_module_destroy (ir);
  cephyr_cpp_result_free (&cpp_res);
  free (source_text);

  return result;
}

cephyr_result
cephyr_compile (const cephyr_options *opts)
{
  cephyr_profile profile;
  cephyr_options effective;
  cephyr_stdlib stdlib;
  char profile_error_message[512] = { 0 };
  char *profile_path = NULL;
  char *resolved_manifest = NULL;
  char *resolved_sched = NULL;
  char *generated_sched = NULL;
  char *discovered_assembler = NULL;
  char *discovered_linker = NULL;
  char *stdlib_manifest = NULL;
  int stdlib_explicit = 0;
  int stdlib_loaded = 0;
  const char **include_paths = NULL;
  const char **defines = NULL;
  const char **preprocessor_options = NULL;
  const char **preprocessor_args = NULL;
  const char **assembler_options = NULL;
  const char **assembler_args = NULL;
  const char **linker_options = NULL;
  const char **linker_args = NULL;
  const char **library_paths = NULL;
  const char **libraries = NULL;
  size_t include_count = 0;
  size_t define_count = 0;
  int preprocessor_option_count = 0;
  int preprocessor_arg_count = 0;
  int assembler_option_count = 0;
  int assembler_arg_count = 0;
  int linker_option_count = 0;
  int linker_arg_count = 0;
  int library_path_count = 0;
  int library_count = 0;
  cephyr_result result;

  if (opts == NULL || opts->source_path == NULL)
    {
      fprintf (stderr, "cephyr: invalid compiler options\n");
      return CEPHYR_ERR_INTERNAL;
    }
  memset (&profile, 0, sizeof (profile));
  cephyr_stdlib_init (&stdlib);
  if (opts->profile_path != NULL)
    {
      profile_path = cephyr_driver_strdup (opts->profile_path);
      if (profile_path == NULL
          || !cephyr_profile_load (profile_path, &profile,
                                   profile_error_message,
                                   sizeof (profile_error_message)))
        {
          fprintf (stderr, "cephyr: profile error: %s\n",
                   profile_error_message[0] ? profile_error_message
                                            : "cannot load profile");
          free (profile_path);
          return CEPHYR_ERR_INTERNAL;
        }
    }
  else
    {
      profile_path = cephyr_profile_discover (".", profile_error_message,
                                              sizeof (profile_error_message));
      if (profile_path != NULL)
        {
          if (!cephyr_profile_load (profile_path, &profile,
                                    profile_error_message,
                                    sizeof (profile_error_message)))
            {
              fprintf (stderr, "cephyr: profile error: %s\n",
                       profile_error_message);
              free (profile_path);
              return CEPHYR_ERR_INTERNAL;
            }
        }
      else
        {
          cephyr_profile_init (&profile);
        }
    }

  if (profile.sched_script != NULL && profile_has_explicit_plan (&profile))
    {
      fprintf (stderr, "cephyr: profile must choose sched_script or explicit "
                       "kernels/rewrites\n");
      cephyr_profile_destroy (&profile);
      free (profile_path);
      return CEPHYR_ERR_SCHED;
    }

  effective = *opts;
  if (!opts->opt_level_explicit)
    {
      if (!profile_opt_level (profile.opt_level, &effective.opt_level))
        {
          fprintf (stderr, "cephyr: profile has invalid opt_level '%s'\n",
                   profile.opt_level ? profile.opt_level : "");
          cephyr_profile_destroy (&profile);
          free (profile_path);
          return CEPHYR_ERR_INTERNAL;
        }
    }
  if (!opts->target_explicit && profile.target_triple != NULL)
    effective.target_triple = profile.target_triple;
  if (!opts->cpp_explicit)
    {
      const char *environment_cpp = getenv ("CEPHYR_CPP");
      if (environment_cpp != NULL && *environment_cpp != '\0')
        effective.cpp_command = environment_cpp;
      else if (profile.preprocessor != NULL
               && strcmp (profile.preprocessor, "ucpp") != 0
               && profile.preprocessor[0] != '\0')
        effective.cpp_command = profile.preprocessor;
      else
        effective.cpp_command = NULL;
    }
  if (opts->assembler != NULL)
    {
      effective.assembler = opts->assembler;
      effective.assembler_external = true;
    }
  else if (getenv ("CEPHYR_AS") != NULL && *getenv ("CEPHYR_AS") != '\0')
    {
      effective.assembler = getenv ("CEPHYR_AS");
      effective.assembler_external = true;
    }
  else if (opts->assembler == NULL && profile.assembler != NULL)
    {
      effective.assembler = profile.assembler;
      effective.assembler_external = true;
    }
  if (getenv ("CEPHYR_LD") != NULL && *getenv ("CEPHYR_LD") != '\0')
    effective.linker = getenv ("CEPHYR_LD");
  else if (opts->linker == NULL && profile.linker != NULL)
    effective.linker = profile.linker;
  if (effective.assembler == NULL)
    {
      discovered_assembler
          = (char *)cephyr_discover_assembler (effective.target_triple);
      if (discovered_assembler == NULL)
        {
          fprintf (stderr, "cephyr: cannot discover default assembler\n");
          cephyr_profile_destroy (&profile);
          free (profile_path);
          return CEPHYR_ERR_ASSEMBLE;
        }
      effective.assembler = discovered_assembler;
    }
  if (effective.linker == NULL)
    {
      discovered_linker
          = (char *)cephyr_discover_linker (effective.target_triple);
      if (discovered_linker == NULL)
        {
          fprintf (stderr, "cephyr: cannot discover default linker\n");
          cephyr_profile_destroy (&profile);
          free (profile_path);
          free (discovered_assembler);
          return CEPHYR_ERR_LINK;
        }
      effective.linker = discovered_linker;
    }
  if (!opts->pic_explicit)
    effective.pic = profile.pic;
  if (!opts->pie_explicit)
    effective.pie = profile.pie;
  if (!opts->shared_explicit)
    effective.shared = profile.shared;
  if (!opts->manifest_explicit && profile.manifest_dir != NULL)
    {
      resolved_manifest
          = profile_resolve_path (profile_path, profile.manifest_dir);
      if (resolved_manifest == NULL)
        {
          fprintf (stderr, "cephyr: out of memory resolving manifest_dir\n");
          cephyr_profile_destroy (&profile);
          free (profile_path);
          return CEPHYR_ERR_INTERNAL;
        }
      effective.manifest_dir = resolved_manifest;
    }

  /* Stdlib wiring: an explicit manifest (API field or
   * $CEPHYR_STDLIB_MANIFEST) must load; the in-tree Salvo default is
   * advisory so external build trees without stdlib-salvo still work. */
  if (opts->stdlib_manifest != NULL)
    {
      stdlib_manifest = cephyr_driver_strdup (opts->stdlib_manifest);
      stdlib_explicit = 1;
      if (stdlib_manifest == NULL)
        goto oom;
    }
  else
    {
      stdlib_manifest = cephyr_stdlib_discover_manifest (&stdlib_explicit);
    }
  if (stdlib_manifest != NULL)
    {
      char stdlib_error[256] = { 0 };
      if (cephyr_stdlib_load (stdlib_manifest, effective.target_triple,
                              &stdlib, stdlib_error, sizeof (stdlib_error)))
        {
          stdlib_loaded = 1;
        }
      else if (stdlib_explicit)
        {
          fprintf (stderr, "cephyr: stdlib manifest error: %s\n",
                   stdlib_error[0] ? stdlib_error
                                   : "cannot load stdlib manifest");
          result = CEPHYR_ERR_INTERNAL;
          goto cleanup;
        }
    }

  include_count
      = profile.include_path_count
        + (size_t)(opts->include_path_count > 0 ? opts->include_path_count : 0)
        + (stdlib_loaded ? stdlib.include_dir_count : 0);
  define_count = profile.define_count
                 + (size_t)(opts->define_count > 0 ? opts->define_count : 0);
  if (include_count != 0)
    {
      size_t user_include_count
          = (size_t)(opts->include_path_count > 0 ? opts->include_path_count
                                                  : 0);
      include_paths
          = (const char **)malloc (include_count * sizeof (*include_paths));
      if (include_paths == NULL)
        goto oom;
      for (size_t i = 0; i < profile.include_path_count; ++i)
        include_paths[i] = profile.include_paths[i];
      for (size_t i = 0; i < user_include_count; ++i)
        include_paths[profile.include_path_count + i] = opts->include_paths[i];
      /* Stdlib headers come last so user -I paths take precedence. */
      if (stdlib_loaded)
        for (size_t i = 0; i < stdlib.include_dir_count; ++i)
          include_paths[profile.include_path_count + user_include_count + i]
              = stdlib.include_dirs[i];
      effective.include_paths = include_paths;
      effective.include_path_count = (int)include_count;
    }
  if (define_count != 0)
    {
      defines = (const char **)malloc (define_count * sizeof (*defines));
      if (defines == NULL)
        goto oom;
      for (size_t i = 0; i < profile.define_count; ++i)
        defines[i] = profile.defines[i];
      for (size_t i = 0; i < (size_t)opts->define_count; ++i)
        defines[profile.define_count + i] = opts->defines[i];
      effective.defines = defines;
      effective.define_count = (int)define_count;
    }
  if (!merge_string_lists ((const char *const *)profile.preprocessor_options,
                           profile.preprocessor_option_count,
                           opts->preprocessor_options,
                           (size_t)(opts->preprocessor_option_count > 0
                                        ? opts->preprocessor_option_count
                                        : 0),
                           &preprocessor_options, &preprocessor_option_count)
      || !merge_string_lists ((const char *const *)profile.preprocessor_args,
                              profile.preprocessor_arg_count,
                              opts->preprocessor_args,
                              (size_t)(opts->preprocessor_arg_count > 0
                                           ? opts->preprocessor_arg_count
                                           : 0),
                              &preprocessor_args, &preprocessor_arg_count)
      || !merge_string_lists ((const char *const *)profile.assembler_options,
                              profile.assembler_option_count,
                              opts->assembler_options,
                              (size_t)(opts->assembler_option_count > 0
                                           ? opts->assembler_option_count
                                           : 0),
                              &assembler_options, &assembler_option_count)
      || !merge_string_lists (
          (const char *const *)profile.assembler_args,
          profile.assembler_arg_count, opts->assembler_args,
          (size_t)(opts->assembler_arg_count > 0 ? opts->assembler_arg_count
                                                 : 0),
          &assembler_args, &assembler_arg_count)
      || !merge_string_lists (
          (const char *const *)profile.linker_options,
          profile.linker_option_count, opts->linker_options,
          (size_t)(opts->linker_option_count > 0 ? opts->linker_option_count
                                                 : 0),
          &linker_options, &linker_option_count)
      || !merge_string_lists (
          (const char *const *)profile.linker_args, profile.linker_arg_count,
          opts->linker_args,
          (size_t)(opts->linker_arg_count > 0 ? opts->linker_arg_count : 0),
          &linker_args, &linker_arg_count)
      || !merge_string_lists3 (
          (const char *const *)profile.library_paths,
          profile.library_path_count, opts->library_paths,
          (size_t)(opts->library_path_count > 0 ? opts->library_path_count
                                                : 0),
          stdlib_loaded ? (const char *const *)stdlib.library_paths : NULL,
          stdlib_loaded ? stdlib.library_path_count : 0, &library_paths,
          &library_path_count)
      || !merge_string_lists3 (
          (const char *const *)profile.libraries, profile.library_count,
          opts->libraries,
          (size_t)(opts->library_count > 0 ? opts->library_count : 0),
          stdlib_loaded ? (const char *const *)stdlib.libraries : NULL,
          stdlib_loaded ? stdlib.library_count : 0, &libraries,
          &library_count))
    {
      goto oom;
    }
  effective.preprocessor_options = preprocessor_options;
  effective.preprocessor_option_count = preprocessor_option_count;
  effective.preprocessor_args = preprocessor_args;
  effective.preprocessor_arg_count = preprocessor_arg_count;
  effective.assembler_options = assembler_options;
  effective.assembler_option_count = assembler_option_count;
  effective.assembler_args = assembler_args;
  effective.assembler_arg_count = assembler_arg_count;
  effective.linker_options = linker_options;
  effective.linker_option_count = linker_option_count;
  effective.linker_args = linker_args;
  effective.linker_arg_count = linker_arg_count;
  effective.library_paths = library_paths;
  effective.library_path_count = library_path_count;
  effective.libraries = libraries;
  effective.library_count = library_count;
  if (stdlib_loaded)
    {
      /* Retained on the driver configuration until link orchestration
       * lands, like the -L/-l channels before it. */
      effective.start_files = (const char *const *)stdlib.start_files;
      effective.start_file_count = (int)stdlib.start_file_count;
    }

  if (opts->sched_script != NULL)
    {
      effective.sched_script = opts->sched_script;
    }
  else if (profile.sched_script != NULL)
    {
      resolved_sched
          = profile_resolve_path (profile_path, profile.sched_script);
      if (resolved_sched == NULL)
        goto oom;
      effective.sched_script = resolved_sched;
    }
  else if (profile_has_explicit_plan (&profile))
    {
      if (!write_explicit_sched_script (&profile, &generated_sched,
                                        profile_error_message,
                                        sizeof (profile_error_message)))
        {
          fprintf (stderr, "cephyr: profile error: %s\n",
                   profile_error_message);
          result = CEPHYR_ERR_SCHED;
          goto cleanup;
        }
      effective.sched_script = generated_sched;
    }

  result = cephyr_compile_inner (&effective);
  goto cleanup;
oom:
  fprintf (stderr, "cephyr: out of memory preparing profile\n");
  result = CEPHYR_ERR_INTERNAL;
cleanup:
  if (generated_sched != NULL)
    unlink (generated_sched);
  free (generated_sched);
  free (discovered_assembler);
  free (discovered_linker);
  free (resolved_sched);
  free (resolved_manifest);
  free (include_paths);
  free (defines);
  free (preprocessor_options);
  free (preprocessor_args);
  free (assembler_options);
  free (assembler_args);
  free (linker_options);
  free (linker_args);
  free (library_paths);
  free (libraries);
  free (stdlib_manifest);
  cephyr_stdlib_destroy (&stdlib);
  cephyr_profile_destroy (&profile);
  free (profile_path);
  return result;
}
