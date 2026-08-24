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

#include "../include/cephyr-module.h"
#include "../lower/cephyr_lower.h"
#include "../profile/cephyr_profile.h"
#include "../sema/cephyr_ast.h"
#include "../sema/cephyr_sema.h"
#include "../stdlib/cephyr_stdlib.h"
#include "../stdmodule/cephyr_stdmodule.h"

#include "../../../ir/ccw_ir.h"
#include "../../../glue/ccw_host_accessors.h"
#include "../../../oeuph/ccw_sema.h"
#include "../../../sched/ccw_sched.h"
#include "../../../sched/ccw_rewrite_scheme.h"
#include "../../../swaff/ccw_swaff.h"
#include "../../../third_party/ucpp/cpp.h"
#include "../../../third_party/ucpp/tune.h"
#include "../../../third_party/ucpp/ucppi.h"
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

typedef struct
{
  char *name;
  int offset;
} cephyr_stack_slot;

typedef struct
{
  cephyr_stack_slot *slots;
  size_t count;
  size_t cap;
} cephyr_stack_frame;

static void
cephyr_stack_frame_init (cephyr_stack_frame *frame)
{
  memset (frame, 0, sizeof (*frame));
}

static void
cephyr_stack_frame_destroy (cephyr_stack_frame *frame)
{
  if (frame == NULL)
    return;
  for (size_t i = 0; i < frame->count; i++)
    free (frame->slots[i].name);
  free (frame->slots);
  memset (frame, 0, sizeof (*frame));
}

static int
cephyr_is_x86_reg_name (const char *name)
{
  static const char *const regs[] = {
    "rax",  "rbx",  "rcx",  "rdx",  "rsi",  "rdi",  "rbp",  "rsp",
    "eax",  "ebx",  "ecx",  "edx",  "esi",  "edi",  "ebp",  "esp",
    "ax",   "bx",   "cx",   "dx",   "si",   "di",   "bp",   "sp",
    "al",   "bl",   "cl",   "dl",   "ah",   "bh",   "ch",   "dh",
    "r8",   "r9",   "r10",  "r11",  "r12",  "r13",  "r14",  "r15",
    "r8d",  "r9d",  "r10d", "r11d", "r12d", "r13d", "r14d", "r15d",
    "r8w",  "r9w",  "r10w", "r11w", "r12w", "r13w", "r14w", "r15w",
    "r8b",  "r9b",  "r10b", "r11b", "r12b", "r13b", "r14b", "r15b",
    NULL
  };
  if (name == NULL)
    return 0;
  for (size_t i = 0; regs[i] != NULL; i++)
    if (strcmp (name, regs[i]) == 0)
      return 1;
  return 0;
}

static int
cephyr_stack_frame_find (const cephyr_stack_frame *frame, const char *name)
{
  if (frame == NULL || name == NULL)
    return -1;
  for (size_t i = 0; i < frame->count; i++)
    if (strcmp (frame->slots[i].name, name) == 0)
      return (int)i;
  return -1;
}

static int
cephyr_stack_frame_add (cephyr_stack_frame *frame, const char *name)
{
  cephyr_stack_slot *slots;
  if (frame == NULL || name == NULL || *name == '\0'
      || cephyr_is_x86_reg_name (name))
    return -1;
  if (cephyr_stack_frame_find (frame, name) >= 0)
    return 0;
  if (frame->count == frame->cap)
    {
      size_t ncap = frame->cap ? frame->cap * 2 : 32;
      slots = realloc (frame->slots, ncap * sizeof (*slots));
      if (slots == NULL)
        return -1;
      frame->slots = slots;
      frame->cap = ncap;
    }
  frame->slots[frame->count].name = cephyr_driver_strdup (name);
  if (frame->slots[frame->count].name == NULL)
    return -1;
  frame->slots[frame->count].offset = (int)((frame->count + 1) * 8);
  frame->count++;
  return 0;
}

static void
cephyr_stack_frame_collect_operand (cephyr_stack_frame *frame, const char *name)
{
  (void)cephyr_stack_frame_add (frame, name);
}

static void
cephyr_stack_frame_collect_instruction (cephyr_stack_frame *frame,
                                       const ccw_ir *ir, ccw_node ins)
{
  const char *opcode = ccw_ir_instr_opcode (ir, ins);
  const char *dest = ccw_ir_instr_dest (ir, ins);
  int nops = ccw_ir_instr_operand_count (ir, ins);
  if (opcode == NULL)
    return;
  cephyr_stack_frame_collect_operand (frame, dest);
  for (int oi = 0; oi < nops; oi++)
    {
      ccw_node op = ccw_ir_instr_operand (ir, ins, oi);
      if (ccw_ir_operand_is_const (ir, op))
        continue;
      const char *name = ccw_ir_operand_name (ir, op);
      if (name == NULL)
        continue;
      if (strcmp (opcode, "call") == 0 && oi == 0)
        continue;
      if (strcmp (opcode, "branch") == 0 && oi == 0)
        continue;
      if (strcmp (opcode, "branch.if") == 0 && oi > 0)
        continue;
      if (strcmp (opcode, "br.cond") == 0 && oi > 0)
        continue;
      if (strcmp (opcode, "phi") == 0 && (oi % 2) == 1)
        continue;
      cephyr_stack_frame_collect_operand (frame, name);
    }
}

static char *
cephyr_stack_ref (const cephyr_stack_frame *frame, const char *name)
{
  kstring_t out = { 0, 0, NULL };
  int idx;
  if (name == NULL)
    return NULL;
  if (cephyr_is_x86_reg_name (name))
    return cephyr_driver_strdup (name);
  idx = cephyr_stack_frame_find (frame, name);
  if (idx < 0)
    return cephyr_driver_strdup (name);
  if (ksprintf (&out, "[rbp - %d]", frame->slots[idx].offset) < 0)
    {
      free (out.s);
      return NULL;
    }
  return ks_release (&out);
}

static char *read_file (const char *path, size_t *out_len);

/* Check if an env var is set to a truthy value (1, true, yes,
 * case-insensitive). */
static int
env_is_truthy (const char *name)
{
  const char *val = getenv (name);
  if (val == NULL)
    return 0;
  if (strcmp (val, "1") == 0 || strcmp (val, "true") == 0
      || strcmp (val, "TRUE") == 0 || strcmp (val, "True") == 0
      || strcmp (val, "yes") == 0 || strcmp (val, "YES") == 0
      || strcmp (val, "Yes") == 0 || strcmp (val, "on") == 0
      || strcmp (val, "ON") == 0 || strcmp (val, "On") == 0)
    return 1;
  return 0;
}

/* ---------- preprocessor helpers ---------- */

typedef struct
{
  char *text;
  size_t text_len;
  char *error_message;
} cephyr_cpp_result;

static void
cephyr_cpp_result_free (cephyr_cpp_result *res)
{
  if (res == NULL)
    return;
  free (res->text);
  free (res->error_message);
  memset (res, 0, sizeof (*res));
}

/* Run an external preprocessor command on a source file. Returns the
 * preprocessed output as a heap-allocated string. */
static char *
cpp_run_external (const char *source_path, const char *cpp_command,
                  const char *const *options, int option_count,
                  const char *const *args, int arg_count, char **error_message)
{
  kstring_t cmd = { 0, 0, NULL };
  kstring_t output = { 0, 0, NULL };
  FILE *pipe;
  char buf[4096];
  size_t n;
  int status;

  if (cpp_command == NULL || *cpp_command == '\0')
    {
      if (error_message)
        *error_message = cephyr_driver_strdup ("no preprocessor command set");
      return NULL;
    }
  if (kputs (cpp_command, &cmd) == EOF)
    goto oom;
  for (int i = 0; i < option_count; ++i)
    {
      if (kputc (' ', &cmd) == EOF)
        goto oom;
      if (kputs (options[i], &cmd) == EOF)
        goto oom;
    }
  for (int i = 0; i < arg_count; ++i)
    {
      if (kputc (' ', &cmd) == EOF)
        goto oom;
      if (kputs (args[i], &cmd) == EOF)
        goto oom;
    }
  if (kputc (' ', &cmd) == EOF)
    goto oom;
  if (kputs (source_path, &cmd) == EOF)
    goto oom;

  pipe = popen (cmd.s, "r");
  free (cmd.s);
  if (pipe == NULL)
    {
      if (error_message)
        {
          char ebuf[512];
          snprintf (ebuf, sizeof (ebuf),
                    "failed to run external preprocessor: %s", cpp_command);
          *error_message = cephyr_driver_strdup (ebuf);
        }
      return NULL;
    }
  while ((n = fread (buf, 1, sizeof (buf), pipe)) > 0)
    {
      if (kputsn (buf, (int)n, &output) == EOF)
        {
          free (output.s);
          pclose (pipe);
          if (error_message)
            *error_message = cephyr_driver_strdup ("out of memory");
          return NULL;
        }
    }
  status = pclose (pipe);
  if (status != 0)
    {
      free (output.s);
      if (error_message)
        {
          char ebuf[512];
          snprintf (ebuf, sizeof (ebuf),
                    "external preprocessor exited with status %d", status);
          *error_message = cephyr_driver_strdup (ebuf);
        }
      return NULL;
    }
  return ks_release (&output);
oom:
  free (cmd.s);
  if (error_message)
    *error_message = cephyr_driver_strdup ("out of memory");
  return NULL;
}

/* Run ucpp on source text. Returns a cephyr_cpp_result. */
static cephyr_cpp_result
cpp_run_ucpp (const char *source_text, size_t source_len,
              const char *source_name, const char *const *include_paths,
              int include_path_count, const char *const *defines,
              int define_count, const char *const *options, int option_count,
              const char *const *args, int arg_count)
{
  cephyr_cpp_result result;
  FILE *tmpf;
  char *out_buf = NULL;
  size_t out_len = 0;
  FILE *saved_emit_output;

  memset (&result, 0, sizeof (result));

  (void)include_path_count;
  (void)options;
  (void)option_count;
  (void)args;
  (void)arg_count;
  init_cpp ();
  no_special_macros = 0;
  c99_compliant = 1;
  c99_hosted = 1;
  emit_defines = emit_assertions = 0;
  init_tables (1);

  init_include_path ((char **)include_paths);
  set_init_filename ((char *)(source_name ? source_name : "<input>"), 0);

  struct lexer_state ls;
  init_lexer_state (&ls);
  /* Use CPP flags, not LEXER flags — we want preprocessor output, not tokens */
  ls.flags = DEFAULT_CPP_FLAGS;
  ls.flags |= HANDLE_ASSERTIONS | HANDLE_PRAGMA | LINE_NUM | KEEP_OUTPUT;

  /* Predefined macros for Cephyr (LP64, x86-64, Linux) */
  static const char *builtin_macros[] = { "__GNUC__=4",
                                          "__GNUC_MINOR__=2",
                                          "__GNUC_PATCHLEVEL__=1",
                                          "__LP64__",
                                          "_LP64",
                                          "__x86_64__",
                                          "__x86_64",
                                          "__amd64__",
                                          "__amd64",
                                          "__linux__",
                                          "__linux",
                                          "__unix__",
                                          "__unix",
                                          "__SIZEOF_POINTER__=8",
                                          "__SIZEOF_LONG__=8",
                                          "__SIZEOF_INT__=4",
                                          "__SIZEOF_SHORT__=2",
                                          NULL };
  for (int i = 0; builtin_macros[i] != NULL; i++)
    define_macro (&ls, (char *)builtin_macros[i]);

  /* User defines */
  for (int i = 0; i < define_count; ++i)
    {
      if (defines[i] != NULL)
        define_macro (&ls, (char *)defines[i]);
    }

  /* Redirect output to memory buffer */
  FILE *mem_output = open_memstream (&out_buf, &out_len);
  ls.output = mem_output;
  if (mem_output == NULL)
    {
      result.error_message = cephyr_driver_strdup (
          "failed to create memory output for preprocessor");
      free_lexer_state (&ls);
      wipeout ();
      return result;
    }
  /*
   * ucpp's text emitter uses the process-global `emit_output` stream rather
   * than lexer_state.output (see third_party/ucpp/cpp.c).  Keep both streams
   * in sync so the in-process preprocessor actually returns its output.
   */
  saved_emit_output = emit_output;
  emit_output = mem_output;

  /* Feed source and run */
  tmpf = tmpfile ();
  if (tmpf == NULL)
    {
      result.error_message = cephyr_driver_strdup (
          "failed to create temp file for preprocessor input");
      fclose (mem_output);
      free_lexer_state (&ls);
      wipeout ();
      return result;
    }
  fwrite (source_text, 1, source_len, tmpf);
  rewind (tmpf);
  ls.input = tmpf;
  enter_file (&ls, ls.flags);

  /* cpp() must be called in a loop; each call processes one token/directive
   * and returns.  CPPERR_EOF signals end-of-file. */
  {
    int r;
    while ((r = cpp (&ls)) < CPPERR_EOF)
      (void)r;
  }
  /*
   * The standalone ucpp driver calls check_cpp_errors() after the final
   * cpp() call; that step flushes the buffered text output.  The embedded
   * path must do the same before reading the memory stream.
   */
  check_cpp_errors (&ls);
  fclose (mem_output);
  emit_output = saved_emit_output;
  free_lexer_state (&ls);
  wipeout ();

  if (out_buf == NULL)
    {
      out_buf = cephyr_driver_strdup ("");
      out_len = 0;
    }
  result.text = out_buf;
  result.text_len = out_len;
  return result;
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
  const char **inputs = NULL;
  size_t input_count = 0;
  size_t input_capacity = 0;
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
  link_options.entry = "_start";
  link_options.search_paths = opts->library_paths;
  link_options.search_path_count = (size_t)opts->library_path_count;
  /*
   * Cephyr's generated assembly already contains the freestanding _start
   * shim.  Do not add a CRT start object a second time; retain the standard
   * libraries themselves so external calls (printf, malloc, ...) resolve.
   */
  input_capacity = (size_t)opts->library_count + 1;
  inputs = calloc (input_capacity ? input_capacity : 1, sizeof (*inputs));
  if (inputs == NULL)
    return CEPHYR_ERR_INTERNAL;
  inputs[input_count++] = object_path;
  for (int i = 0; i < opts->library_count; i++)
    {
      const char *name = opts->libraries[i];
      if (name == NULL)
        continue;
      char candidate[1024];
      const char *suffixes[] = { ".a", ".so" };
      int found = 0;
      for (size_t s = 0; s < sizeof (suffixes) / sizeof (suffixes[0])
                       && !found;
           s++)
        {
          for (int j = 0; j < opts->library_path_count && !found; j++)
            {
              snprintf (candidate, sizeof (candidate), "%s/lib%s%s",
                        opts->library_paths[j], name, suffixes[s]);
              if (access (candidate, R_OK) == 0)
                {
                  inputs[input_count++] = cephyr_driver_strdup (candidate);
                  found = 1;
                }
            }
        }
      if (!found)
        {
          fprintf (stderr, "cephyr: linker: cannot find library '%s'\n", name);
          for (size_t j = 1; j < input_count;
               j++)
            free ((void *)inputs[j]);
          free (inputs);
          return CEPHYR_ERR_LINK;
        }
    }
  int linked = ccwld_link_files (opts->target_triple, output_path, inputs,
                                 input_count, &link_options, &error);
  for (size_t i = 1; i < input_count; i++)
    free ((void *)inputs[i]);
  free (inputs);
  if (!linked)
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

static const char *
cephyr_canonical_opcode (const char *opcode)
{
  static const struct
  {
    const char *selected;
    const char *canonical;
  } map[] = {
    { "x86-64.mov", "imov" },       { "x86-64.add", "iadd" },
    { "x86-64.sub", "isub" },       { "x86-64.imul", "imul" },
    { "x86-64.idiv", "idiv" },      { "x86-64.idiv-rem", "irem" },
    { "x86-64.and", "iand" },       { "x86-64.or", "ior" },
    { "x86-64.xor", "ixor" },       { "x86-64.shl", "shl" },
    { "x86-64.shr", "lshr" },       { "x86-64.sar", "ashr" },
    { "x86-64.neg", "ineg" },       { "x86-64.not", "inot" },
    { "x86-64.cmp.eq", "icmp.eq" }, { "x86-64.cmp.ne", "icmp.ne" },
    { "x86-64.cmp.lt", "icmp.lt" }, { "x86-64.cmp.le", "icmp.le" },
    { "x86-64.cmp.gt", "icmp.gt" }, { "x86-64.cmp.ge", "icmp.ge" },
    { "x86-64.ret", "ret" },        { "x86-64.br", "br" },
    { "x86-64.call", "call" },      { "x86-64.load", "load" },
    { "x86-64.store", "store" },    { "x86-64.jmp", "jmp" },
  };
  if (opcode == NULL)
    return NULL;
  for (size_t i = 0; i < sizeof (map) / sizeof (map[0]); i++)
    if (strcmp (opcode, map[i].selected) == 0)
      return map[i].canonical;
  return opcode;
}

/* Backend emission façade.  Codegen kernels annotate/mutate the canonical IR;
 * this deterministic textual fallback gives the assembler a concrete target
 * program until target-specific instruction printers are available. */
__attribute__ ((unused)) static char *
emit_target_assembly (const ccw_ir *ir, const char *triple)
{
  const char *arch = cephyr_target_arch (triple);
  kstring_t out = { 0, 0, NULL };
  int functions;
  int i, j, k;
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

  /* Emit x86-64 assembly from the Weave IR.
   * Walk functions, blocks, and instructions, mapping Weave IR opcodes
   * to x86-64 instructions. */
  if (strcmp (arch, "wasm32") == 0)
    {
      APPEND_FMT (";; generated by cephyr for %s\n", triple);
      functions = ccw_ir_function_count (ir);
      for (i = 0; i < functions; ++i)
        {
          ccw_node fn = ccw_ir_function_ref (ir, i);
          const char *name = ccw_ir_function_name (ir, fn);
          if (name == NULL || *name == '\0')
            continue;
          APPEND_FMT (".func %s (result i32)\n  i32.const 0\n  return\n.endfunc\n",
                      name);
        }
    }
  else if (strcmp (arch, "x86-64") == 0)
    {
      APPEND_FMT (".text\n");
      /* A linked Cephyr program is a freestanding ELF image.  `main` is
       * a normal C function and cannot be used as the process entry point:
       * returning from it would jump to an uninitialised return address.
       * Provide the Linux x86-64 process shim which invokes main and exits
       * through the kernel ABI. */
      APPEND_FMT (".global _start\n_start:\n"
                  "  xor ebp, ebp\n"
                  "  and rsp, -16\n"
                  "  call main\n"
                  "  mov edi, eax\n"
                  "  mov eax, 60\n"
                  "  syscall\n");
      functions = ccw_ir_function_count (ir);
      for (i = 0; i < functions; ++i)
        {
          ccw_node fn = ccw_ir_function_ref (ir, i);
          const char *name = ccw_ir_function_name (ir, fn);
          if (name == NULL || *name == '\0')
            continue;
          APPEND_FMT (".global %s\n%s:\n", name, name);

          cephyr_stack_frame frame;
          cephyr_stack_frame_init (&frame);
          int nblocks = ccw_ir_function_block_count (ir, fn);
          for (j = 0; j < nblocks; ++j)
            {
              ccw_node blk = ccw_ir_function_block_ref (ir, fn, j);
              int ninstrs = ccw_ir_block_instr_count (ir, blk);
              for (k = 0; k < ninstrs; ++k)
                cephyr_stack_frame_collect_instruction (
                    &frame, ir, ccw_ir_block_instr_ref (ir, blk, k));
            }
          int frame_size = (int)((frame.count * 8 + 15) & ~15);
          if (frame_size > 0)
            APPEND_FMT ("  push rbp\n  mov rbp, rsp\n  sub rsp, %d\n",
                        frame_size);
          else
            APPEND_FMT ("  push rbp\n  mov rbp, rsp\n");

          for (j = 0; j < nblocks; ++j)
            {
              ccw_node blk = ccw_ir_function_block_ref (ir, fn, j);
              const char *blk_name = ccw_ir_block_name (ir, blk);
              if (blk_name && *blk_name)
                APPEND_FMT (".L%s:\n", blk_name);

              int ninstrs = ccw_ir_block_instr_count (ir, blk);
              for (k = 0; k < ninstrs; ++k)
                {
                  ccw_node ins = ccw_ir_block_instr_ref (ir, blk, k);
                  const char *opcode
                      = cephyr_canonical_opcode (ccw_ir_instr_opcode (ir, ins));
                  const char *dest = ccw_ir_instr_dest (ir, ins);
                  int nops = ccw_ir_instr_operand_count (ir, ins);
                  char *dest_ref = cephyr_stack_ref (&frame, dest);

                  if (opcode == NULL)
                    continue;

                  /* Collect operand names */
                  const char *ops[16];
                  int64_t op_vals[16];
                  int op_is_const[16];
                  for (int oi = 0; oi < nops && oi < 16; ++oi)
                    {
                      ccw_node op = ccw_ir_instr_operand (ir, ins, oi);
                      if (ccw_ir_operand_is_const (ir, op))
                        {
                          int64_t val;
                          ccw_ir_const_int_value (ir, op, &val);
                          op_vals[oi] = val;
                          op_is_const[oi] = 1;
                          ops[oi] = NULL;
                        }
                      else
                        {
                          ops[oi] = ccw_ir_operand_name (ir, op);
                          op_is_const[oi] = 0;
                          op_vals[oi] = 0;
                        }
                    }

                  /* Map Weave IR opcodes to x86-64 instructions */
                  if (strcmp (opcode, "ret") == 0)
                    {
                      if (nops >= 1)
                        {
                          if (op_is_const[0])
                            APPEND_FMT ("  mov eax, %ld\n", (long)op_vals[0]);
                          else if (ops[0])
                            {
                              char *src0 = cephyr_stack_ref (&frame, ops[0]);
                              APPEND_FMT ("  mov eax, %s\n", src0);
                              free (src0);
                            }
                        }
                      APPEND_FMT ("  mov rsp, rbp\n  pop rbp\n");
                      APPEND_FMT ("  ret\n");
                    }
                  else if (strcmp (opcode, "iadd") == 0)
                    {
                      if (dest_ref && nops >= 2)
                        {
                          char *lhs = cephyr_stack_ref (&frame, ops[0]);
                          char *rhs = op_is_const[1] ? NULL
                                                      : cephyr_stack_ref (
                                                            &frame, ops[1]);
                          APPEND_FMT ("  mov eax, %s\n", lhs);
                          if (op_is_const[1])
                            APPEND_FMT ("  add eax, %ld\n", (long)op_vals[1]);
                          else
                            APPEND_FMT ("  add eax, %s\n", rhs);
                          APPEND_FMT ("  mov %s, eax\n", dest_ref);
                          free (lhs);
                          free (rhs);
                        }
                    }
                  else if (strcmp (opcode, "isub") == 0)
                    {
                      if (dest_ref && nops >= 2)
                        {
                          char *lhs = cephyr_stack_ref (&frame, ops[0]);
                          char *rhs = op_is_const[1] ? NULL
                                                      : cephyr_stack_ref (
                                                            &frame, ops[1]);
                          APPEND_FMT ("  mov eax, %s\n", lhs);
                          if (op_is_const[1])
                            APPEND_FMT ("  sub eax, %ld\n", (long)op_vals[1]);
                          else
                            APPEND_FMT ("  sub eax, %s\n", rhs);
                          APPEND_FMT ("  mov %s, eax\n", dest_ref);
                          free (lhs);
                          free (rhs);
                        }
                    }
                  else if (strcmp (opcode, "imul") == 0)
                    {
                      if (dest_ref && nops >= 2)
                        {
                          char *lhs = cephyr_stack_ref (&frame, ops[0]);
                          char *rhs = op_is_const[1] ? NULL
                                                      : cephyr_stack_ref (
                                                            &frame, ops[1]);
                          APPEND_FMT ("  mov eax, %s\n", lhs);
                          if (op_is_const[1])
                            APPEND_FMT ("  imul eax, %ld\n", (long)op_vals[1]);
                          else
                            APPEND_FMT ("  imul eax, %s\n", rhs);
                          APPEND_FMT ("  mov %s, eax\n", dest_ref);
                          free (lhs);
                          free (rhs);
                        }
                    }
                  else if (strcmp (opcode, "iand") == 0)
                    {
                      if (dest_ref && nops >= 2)
                        {
                          char *lhs = cephyr_stack_ref (&frame, ops[0]);
                          char *rhs = op_is_const[1] ? NULL
                                                      : cephyr_stack_ref (
                                                            &frame, ops[1]);
                          APPEND_FMT ("  mov eax, %s\n", lhs);
                          if (op_is_const[1])
                            APPEND_FMT ("  and eax, %ld\n", (long)op_vals[1]);
                          else
                            APPEND_FMT ("  and eax, %s\n", rhs);
                          APPEND_FMT ("  mov %s, eax\n", dest_ref);
                          free (lhs);
                          free (rhs);
                        }
                    }
                  else if (strcmp (opcode, "ior") == 0)
                    {
                      if (dest_ref && nops >= 2)
                        {
                          char *lhs = cephyr_stack_ref (&frame, ops[0]);
                          char *rhs = op_is_const[1] ? NULL
                                                      : cephyr_stack_ref (
                                                            &frame, ops[1]);
                          APPEND_FMT ("  mov eax, %s\n", lhs);
                          if (op_is_const[1])
                            APPEND_FMT ("  or eax, %ld\n", (long)op_vals[1]);
                          else
                            APPEND_FMT ("  or eax, %s\n", rhs);
                          APPEND_FMT ("  mov %s, eax\n", dest_ref);
                          free (lhs);
                          free (rhs);
                        }
                    }
                  else if (strcmp (opcode, "ixor") == 0)
                    {
                      if (dest_ref && nops >= 2)
                        {
                          char *lhs = cephyr_stack_ref (&frame, ops[0]);
                          char *rhs = op_is_const[1] ? NULL
                                                      : cephyr_stack_ref (
                                                            &frame, ops[1]);
                          APPEND_FMT ("  mov eax, %s\n", lhs);
                          if (op_is_const[1])
                            APPEND_FMT ("  xor eax, %ld\n", (long)op_vals[1]);
                          else
                            APPEND_FMT ("  xor eax, %s\n", rhs);
                          APPEND_FMT ("  mov %s, eax\n", dest_ref);
                          free (lhs);
                          free (rhs);
                        }
                    }
                  else if (strcmp (opcode, "shl") == 0)
                    {
                      if (dest_ref && nops >= 2)
                        {
                          char *lhs = cephyr_stack_ref (&frame, ops[0]);
                          char *rhs = cephyr_stack_ref (&frame, ops[1]);
                          APPEND_FMT ("  mov eax, %s\n", lhs);
                          APPEND_FMT ("  mov ecx, %s\n", rhs);
                          APPEND_FMT ("  shl eax, cl\n");
                          APPEND_FMT ("  mov %s, eax\n", dest_ref);
                          free (lhs);
                          free (rhs);
                        }
                    }
                  else if (strcmp (opcode, "ashr") == 0)
                    {
                      if (dest_ref && nops >= 2)
                        {
                          char *lhs = cephyr_stack_ref (&frame, ops[0]);
                          char *rhs = cephyr_stack_ref (&frame, ops[1]);
                          APPEND_FMT ("  mov eax, %s\n", lhs);
                          APPEND_FMT ("  mov ecx, %s\n", rhs);
                          APPEND_FMT ("  sar eax, cl\n");
                          APPEND_FMT ("  mov %s, eax\n", dest_ref);
                          free (lhs);
                          free (rhs);
                        }
                    }
                  else if (strcmp (opcode, "icmp.eq") == 0)
                    {
                      if (dest_ref && nops >= 2)
                        {
                          char *lhs = cephyr_stack_ref (&frame, ops[0]);
                          char *rhs = cephyr_stack_ref (&frame, ops[1]);
                          APPEND_FMT ("  mov eax, %s\n", lhs);
                          APPEND_FMT ("  cmp eax, %s\n", rhs);
                          APPEND_FMT ("  sete al\n");
                          APPEND_FMT ("  movzx eax, al\n");
                          APPEND_FMT ("  mov %s, eax\n", dest_ref);
                          free (lhs);
                          free (rhs);
                        }
                    }
                  else if (strcmp (opcode, "icmp.ne") == 0)
                    {
                      if (dest_ref && nops >= 2)
                        {
                          char *lhs = cephyr_stack_ref (&frame, ops[0]);
                          char *rhs = cephyr_stack_ref (&frame, ops[1]);
                          APPEND_FMT ("  mov eax, %s\n", lhs);
                          APPEND_FMT ("  cmp eax, %s\n", rhs);
                          APPEND_FMT ("  setne al\n");
                          APPEND_FMT ("  movzx eax, al\n");
                          APPEND_FMT ("  mov %s, eax\n", dest_ref);
                          free (lhs);
                          free (rhs);
                        }
                    }
                  else if (strcmp (opcode, "icmp.lt") == 0)
                    {
                      if (dest_ref && nops >= 2)
                        {
                          char *lhs = cephyr_stack_ref (&frame, ops[0]);
                          char *rhs = cephyr_stack_ref (&frame, ops[1]);
                          APPEND_FMT ("  mov eax, %s\n", lhs);
                          APPEND_FMT ("  cmp eax, %s\n", rhs);
                          APPEND_FMT ("  setl al\n");
                          APPEND_FMT ("  movzx eax, al\n");
                          APPEND_FMT ("  mov %s, eax\n", dest_ref);
                          free (lhs);
                          free (rhs);
                        }
                    }
                  else if (strcmp (opcode, "icmp.gt") == 0)
                    {
                      if (dest_ref && nops >= 2)
                        {
                          char *lhs = cephyr_stack_ref (&frame, ops[0]);
                          char *rhs = cephyr_stack_ref (&frame, ops[1]);
                          APPEND_FMT ("  mov eax, %s\n", lhs);
                          APPEND_FMT ("  cmp eax, %s\n", rhs);
                          APPEND_FMT ("  setg al\n");
                          APPEND_FMT ("  movzx eax, al\n");
                          APPEND_FMT ("  mov %s, eax\n", dest_ref);
                          free (lhs);
                          free (rhs);
                        }
                    }
                  else if (strcmp (opcode, "icmp.le") == 0)
                    {
                      if (dest_ref && nops >= 2)
                        {
                          char *lhs = cephyr_stack_ref (&frame, ops[0]);
                          char *rhs = cephyr_stack_ref (&frame, ops[1]);
                          APPEND_FMT ("  mov eax, %s\n", lhs);
                          APPEND_FMT ("  cmp eax, %s\n", rhs);
                          APPEND_FMT ("  setle al\n");
                          APPEND_FMT ("  movzx eax, al\n");
                          APPEND_FMT ("  mov %s, eax\n", dest_ref);
                          free (lhs);
                          free (rhs);
                        }
                    }
                  else if (strcmp (opcode, "icmp.ge") == 0)
                    {
                      if (dest_ref && nops >= 2)
                        {
                          char *lhs = cephyr_stack_ref (&frame, ops[0]);
                          char *rhs = cephyr_stack_ref (&frame, ops[1]);
                          APPEND_FMT ("  mov eax, %s\n", lhs);
                          APPEND_FMT ("  cmp eax, %s\n", rhs);
                          APPEND_FMT ("  setge al\n");
                          APPEND_FMT ("  movzx eax, al\n");
                          APPEND_FMT ("  mov %s, eax\n", dest_ref);
                          free (lhs);
                          free (rhs);
                        }
                    }
                  else if (strcmp (opcode, "branch") == 0)
                    {
                      if (nops >= 1 && ops[0])
                        APPEND_FMT ("  jmp .L%s\n", ops[0]);
                    }
                  else if (strcmp (opcode, "branch.if") == 0)
                    {
                      if (nops >= 3 && ops[0] && ops[1] && ops[2])
                        {
                          char *cond = cephyr_stack_ref (&frame, ops[0]);
                          APPEND_FMT ("  mov rax, %s\n", cond);
                          APPEND_FMT ("  test rax, rax\n");
                          APPEND_FMT ("  jne .L%s\n", ops[1]);
                          APPEND_FMT ("  jmp .L%s\n", ops[2]);
                          free (cond);
                        }
                    }
                  else if (strcmp (opcode, "call") == 0)
                    {
                      if (nops >= 1 && ops[0])
                        {
                          APPEND_FMT ("  call %s\n", ops[0]);
                          if (dest_ref)
                            APPEND_FMT ("  mov %s, eax\n", dest_ref);
                        }
                    }
                  else if (strcmp (opcode, "alloc") == 0)
                    {
                      if (dest_ref && nops >= 1)
                        {
                          if (op_is_const[0])
                            APPEND_FMT ("  sub rsp, %ld\n", (long)op_vals[0]);
                          else
                            {
                              char *src0 = cephyr_stack_ref (&frame, ops[0]);
                              APPEND_FMT ("  sub rsp, %s\n", src0);
                              free (src0);
                            }
                          APPEND_FMT ("  mov %s, rsp\n", dest_ref);
                        }
                    }
                  else if (strcmp (opcode, "load") == 0)
                    {
                      if (dest_ref && nops >= 1 && ops[0])
                        {
                          char *src0 = cephyr_stack_ref (&frame, ops[0]);
                          APPEND_FMT ("  mov eax, %s\n  mov %s, eax\n",
                                      src0, dest_ref);
                          free (src0);
                        }
                    }
                  else if (strcmp (opcode, "store") == 0)
                    {
                      if (nops >= 2 && ops[0] && ops[1])
                        {
                          char *dst0 = cephyr_stack_ref (&frame, ops[0]);
                          char *src1 = cephyr_stack_ref (&frame, ops[1]);
                          APPEND_FMT ("  mov rax, %s\n", src1);
                          APPEND_FMT ("  mov %s, rax\n", dst0);
                          free (dst0);
                          free (src1);
                        }
                    }
                  else if (strcmp (opcode, "ineg") == 0)
                    {
                      if (dest_ref && nops >= 1 && ops[0])
                        {
                          char *src0 = cephyr_stack_ref (&frame, ops[0]);
                          APPEND_FMT ("  mov eax, %s\n", src0);
                          APPEND_FMT ("  neg eax\n");
                          APPEND_FMT ("  mov %s, eax\n", dest_ref);
                          free (src0);
                        }
                    }
                  else if (strcmp (opcode, "logic.not") == 0)
                    {
                      if (dest_ref && nops >= 1 && ops[0])
                        {
                          char *src0 = cephyr_stack_ref (&frame, ops[0]);
                          APPEND_FMT ("  mov eax, %s\n", src0);
                          APPEND_FMT ("  test eax, eax\n");
                          APPEND_FMT ("  sete al\n");
                          APPEND_FMT ("  movzx eax, al\n");
                          APPEND_FMT ("  mov %s, eax\n", dest_ref);
                          free (src0);
                        }
                    }
                  else if (strcmp (opcode, "phi") == 0)
                     {
                       /* phi dest, val1, block1, val2, block2, ...
                        * In a simple codegen, pick the first value. */
                       if (dest_ref && nops >= 1 && ops[0])
                         {
                           char *src0 = cephyr_stack_ref (&frame, ops[0]);
                           APPEND_FMT ("  mov rax, %s\n", src0);
                           APPEND_FMT ("  mov %s, rax\n", dest_ref);
                           free (src0);
                         }
                     }
                    /* ---------- Kliche imperative opcodes (§6.1) ---------- */
                    else if (strcmp (opcode, "iconst") == 0)
                      {
                        if (dest_ref && nops >= 1 && op_is_const[0])
                          APPEND_FMT ("  mov rax, %ld\n  mov %s, rax\n",
                                      (long)op_vals[0], dest_ref);
                      }
                    else if (strcmp (opcode, "local.alloc") == 0)
                      {
                        if (dest_ref)
                          {
                            APPEND_FMT ("  sub rsp, 8\n");
                            APPEND_FMT ("  mov %s, rsp\n", dest_ref);
                          }
                      }
                    else if (strcmp (opcode, "local.store") == 0)
                      {
                        if (nops >= 2 && ops[0] && ops[1])
                          {
                            char *slot0 = cephyr_stack_ref (&frame, ops[0]);
                            char *slot1 = cephyr_stack_ref (&frame, ops[1]);
                            APPEND_FMT ("  mov rax, %s\n", slot0);
                            APPEND_FMT ("  mov rbx, %s\n", slot1);
                            APPEND_FMT ("  mov [rax], rbx\n");
                            free (slot0);
                            free (slot1);
                          }
                      }
                    else if (strcmp (opcode, "local.load") == 0)
                      {
                        if (dest_ref && nops >= 1 && ops[0])
                          {
                            char *slot0 = cephyr_stack_ref (&frame, ops[0]);
                            APPEND_FMT ("  mov rax, %s\n", slot0);
                            APPEND_FMT ("  mov rax, [rax]\n");
                            APPEND_FMT ("  mov %s, rax\n", dest_ref);
                            free (slot0);
                          }
                      }
                    else if (strcmp (opcode, "br") == 0)
                      {
                        if (nops >= 1 && ops[0])
                          APPEND_FMT ("  jmp .L%s\n", ops[0]);
                      }
                    else if (strcmp (opcode, "br.cond") == 0)
                      {
                        if (nops >= 3 && ops[0] && ops[1] && ops[2])
                          {
                            char *cond = cephyr_stack_ref (&frame, ops[0]);
                            APPEND_FMT ("  mov rax, %s\n", cond);
                            APPEND_FMT ("  test rax, rax\n");
                            APPEND_FMT ("  jne .L%s\n", ops[1]);
                            APPEND_FMT ("  jmp .L%s\n", ops[2]);
                            free (cond);
                          }
                      }
                    else if (strcmp (opcode, "array.alloc") == 0)
                      {
                        if (dest_ref && nops >= 2 && op_is_const[0])
                          {
                            int64_t total = op_vals[0] * 8;
                            APPEND_FMT ("  sub rsp, %ld\n", (long)total);
                            APPEND_FMT ("  mov %s, rsp\n", dest_ref);
                          }
                      }
                    else if (strcmp (opcode, "array.load") == 0)
                      {
                        if (dest_ref && nops >= 2 && ops[0] && ops[1])
                          {
                            char *slot0 = cephyr_stack_ref (&frame, ops[0]);
                            char *slot1 = cephyr_stack_ref (&frame, ops[1]);
                            APPEND_FMT ("  mov rax, %s\n", slot0);
                            APPEND_FMT ("  mov rbx, %s\n", slot1);
                            APPEND_FMT ("  shl rbx, 3\n");
                            APPEND_FMT ("  mov rax, [rax + rbx]\n");
                            APPEND_FMT ("  mov %s, rax\n", dest_ref);
                            free (slot0);
                            free (slot1);
                          }
                      }
                    else if (strcmp (opcode, "array.store") == 0)
                      {
                        if (nops >= 3 && ops[0] && ops[1] && ops[2])
                          {
                            char *slot0 = cephyr_stack_ref (&frame, ops[0]);
                            char *slot1 = cephyr_stack_ref (&frame, ops[1]);
                            char *slot2 = cephyr_stack_ref (&frame, ops[2]);
                            APPEND_FMT ("  mov rax, %s\n", slot0);
                            APPEND_FMT ("  mov rbx, %s\n", slot1);
                            APPEND_FMT ("  shl rbx, 3\n");
                            APPEND_FMT ("  mov rcx, %s\n", slot2);
                            APPEND_FMT ("  mov [rax + rbx], rcx\n");
                            free (slot0);
                            free (slot1);
                            free (slot2);
                          }
                      }
                    else if (strcmp (opcode, "logic.and") == 0)
                      {
                        if (dest_ref && nops >= 2 && ops[0] && ops[1])
                          {
                            char *slot0 = cephyr_stack_ref (&frame, ops[0]);
                            char *slot1 = cephyr_stack_ref (&frame, ops[1]);
                            APPEND_FMT ("  mov rax, %s\n", slot0);
                            APPEND_FMT ("  test rax, rax\n");
                            APPEND_FMT ("  setne al\n");
                            APPEND_FMT ("  mov rbx, %s\n", slot1);
                            APPEND_FMT ("  test rbx, rbx\n");
                            APPEND_FMT ("  setne bl\n");
                            APPEND_FMT ("  and al, bl\n");
                            APPEND_FMT ("  movzx rax, al\n");
                            APPEND_FMT ("  mov %s, rax\n", dest_ref);
                            free (slot0);
                            free (slot1);
                          }
                      }
                    else if (strcmp (opcode, "logic.or") == 0)
                      {
                        if (dest_ref && nops >= 2 && ops[0] && ops[1])
                          {
                            char *slot0 = cephyr_stack_ref (&frame, ops[0]);
                            char *slot1 = cephyr_stack_ref (&frame, ops[1]);
                            APPEND_FMT ("  mov rax, %s\n", slot0);
                            APPEND_FMT ("  test rax, rax\n");
                            APPEND_FMT ("  setne al\n");
                            APPEND_FMT ("  mov rbx, %s\n", slot1);
                            APPEND_FMT ("  test rbx, rbx\n");
                            APPEND_FMT ("  setne bl\n");
                            APPEND_FMT ("  or al, bl\n");
                            APPEND_FMT ("  movzx rax, al\n");
                            APPEND_FMT ("  mov %s, rax\n", dest_ref);
                            free (slot0);
                            free (slot1);
                          }
                      }
                    else if (strcmp (opcode, "opaque.expr") == 0)
                      {
                        /* skip — opaque expression placeholder */
                      }
                    /* ---------- arithmetic/comparison missing opcodes ---------- */
                  else if (strcmp (opcode, "idiv") == 0)
                    {
                        if (dest_ref && nops >= 2 && ops[0] && ops[1])
                          {
                            char *slot0 = cephyr_stack_ref (&frame, ops[0]);
                            APPEND_FMT ("  mov eax, dword %s\n", slot0);
                            APPEND_FMT ("  cdq\n");
                            if (op_is_const[1])
                              {
                                int64_t v = op_vals[1];
                                APPEND_FMT ("  mov ecx, %ld\n", (long)v);
                                APPEND_FMT ("  idiv ecx\n");
                              }
                            else
                              {
                                char *slot1 = cephyr_stack_ref (&frame, ops[1]);
                                APPEND_FMT ("  idiv dword %s\n", slot1);
                                free (slot1);
                              }
                            APPEND_FMT ("  mov dword %s, eax\n", dest_ref);
                            free (slot0);
                          }
                      }
                    else if (strcmp (opcode, "irem") == 0)
                      {
                        if (dest_ref && nops >= 2 && ops[0] && ops[1])
                          {
                            char *slot0 = cephyr_stack_ref (&frame, ops[0]);
                            APPEND_FMT ("  mov eax, dword %s\n", slot0);
                            APPEND_FMT ("  cdq\n");
                            if (op_is_const[1])
                              {
                                int64_t v = op_vals[1];
                                APPEND_FMT ("  mov ecx, %ld\n", (long)v);
                                APPEND_FMT ("  idiv ecx\n");
                              }
                            else
                              {
                                char *slot1 = cephyr_stack_ref (&frame, ops[1]);
                                APPEND_FMT ("  idiv dword %s\n", slot1);
                                free (slot1);
                              }
                            APPEND_FMT ("  mov dword %s, edx\n", dest_ref);
                            free (slot0);
                          }
                      }
                    else if (strcmp (opcode, "inot") == 0)
                      {
                        if (dest_ref && nops >= 1 && ops[0])
                          {
                            char *slot0 = cephyr_stack_ref (&frame, ops[0]);
                            APPEND_FMT ("  mov rax, %s\n", slot0);
                            APPEND_FMT ("  not rax\n");
                            APPEND_FMT ("  mov %s, rax\n", dest_ref);
                            free (slot0);
                          }
                      }
                    else if (strcmp (opcode, "lshr") == 0)
                      {
                        if (dest_ref && nops >= 2 && ops[0] && ops[1])
                          {
                            char *slot0 = cephyr_stack_ref (&frame, ops[0]);
                            char *slot1 = cephyr_stack_ref (&frame, ops[1]);
                            APPEND_FMT ("  mov rax, %s\n", slot0);
                            APPEND_FMT ("  mov rcx, %s\n", slot1);
                            APPEND_FMT ("  shr rax, cl\n");
                            APPEND_FMT ("  mov %s, rax\n", dest_ref);
                            free (slot0);
                            free (slot1);
                          }
                      }
                    /* ---------- type casts ---------- */
                    else if (strcmp (opcode, "id") == 0)
                      {
                        if (dest_ref && nops >= 1 && ops[0])
                          {
                            char *slot0 = cephyr_stack_ref (&frame, ops[0]);
                            APPEND_FMT ("  mov rax, %s\n"
                                        "  mov %s, rax\n",
                                        slot0, dest_ref);
                            free (slot0);
                          }
                      }
                    else if (strcmp (opcode, "sext") == 0)
                      {
                        if (dest_ref && nops >= 1 && ops[0])
                          {
                            char *slot0 = cephyr_stack_ref (&frame, ops[0]);
                            APPEND_FMT ("  movsx rax, dword %s\n"
                                        "  mov %s, rax\n",
                                        slot0, dest_ref);
                            free (slot0);
                          }
                      }
                    else if (strcmp (opcode, "trunc") == 0)
                      {
                        if (dest_ref && nops >= 1 && ops[0])
                          {
                            char *slot0 = cephyr_stack_ref (&frame, ops[0]);
                            APPEND_FMT ("  mov eax, dword %s\n"
                                        "  mov dword %s, eax\n",
                                        slot0, dest_ref);
                            free (slot0);
                          }
                      }
                    else
                      {
                        /* Unknown opcode: emit as comment */
                        APPEND_FMT ("  # unknown opcode: %s\n", opcode);
                      }
                  free (dest_ref);
                }
            }
          if (frame_size > 0)
            APPEND_FMT ("  mov rsp, rbp\n  pop rbp\n");
          cephyr_stack_frame_destroy (&frame);
        }
    }
  else if (strcmp (arch, "aarch64") == 0)
    {
      APPEND_FMT (".text\n");
      functions = ccw_ir_function_count (ir);
      for (i = 0; i < functions; ++i)
        {
          ccw_node fn = ccw_ir_function_ref (ir, i);
          const char *name = ccw_ir_function_name (ir, fn);
          if (name == NULL || *name == '\0')
            continue;
          APPEND_FMT (".global %s\n.type %s, @function\n%s:\n"
                      "  mov w0, #0\n  ret\n.size %s, .-%s\n",
                      name, name, name, name, name);
        }
    }
  else if (strcmp (arch, "riscv64") == 0)
    {
      APPEND_FMT (".text\n");
      functions = ccw_ir_function_count (ir);
      for (i = 0; i < functions; ++i)
        {
          ccw_node fn = ccw_ir_function_ref (ir, i);
          const char *name = ccw_ir_function_name (ir, fn);
          if (name == NULL || *name == '\0')
            continue;
          APPEND_FMT (".global %s\n.type %s, @function\n%s:\n"
                      "  li a0, 0\n  ret\n.size %s, .-%s\n",
                      name, name, name, name, name);
        }
    }
  else
    {
      APPEND_FMT (".text\n");
      functions = ccw_ir_function_count (ir);
      for (i = 0; i < functions; ++i)
        {
          ccw_node fn = ccw_ir_function_ref (ir, i);
          const char *name = ccw_ir_function_name (ir, fn);
          if (name == NULL || *name == '\0')
            continue;
          APPEND_FMT (".global %s\n.type %s, @function\n%s:\n"
                      "  xor eax, eax\n  ret\n.size %s, .-%s\n",
                      name, name, name, name, name);
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
          preprocessed = cpp_run_external (
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
          cephyr_cpp_result cpp = cpp_run_ucpp (
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
      ccw_executor *executor = ccw_executor_create ();
      const char *kernel_options[] = {
        "target=x86-64",
        NULL
      };
      if (executor == NULL
          || ccw_executor_abi_version (executor) != CCW_GLUE_ABI_VERSION
          || ccw_host_register_core_accessors (executor) != CCW_OK)
        {
          fprintf (stderr, "cephyr: cannot initialize kernel executor\n");
          ccw_executor_destroy (executor);
          ccw_plan_free (plan);
          return CEPHYR_ERR_SCHED;
        }
      ccw_oeuph_budget budget = ccw_oeuph_default_budget ();
      if (!ccw_rewrite_scheme_apply (
              plan, ir, manifest_dir, budget, CCW_COST_PERFORMANCE, NULL, 0,
              NULL, &err))
        {
          fprintf (stderr, "cephyr: rewrite error: %s\n", err.message);
          ccw_plan_free (plan);
          ccw_executor_destroy (executor);
          return CEPHYR_ERR_SCHED;
        }
      if (!ccw_plan_apply_kernels (plan, ir, manifest_dir, executor,
                                   kernel_options, &err))
        {
          fprintf (stderr, "cephyr: kernel error: %s\n", err.message);
          ccw_executor_destroy (executor);
          ccw_plan_free (plan);
          return CEPHYR_ERR_SCHED;
        }
      /* Write the plan for debugging */
      const char *plan_name = opts->sched_script ? "profile" : script_name;
      char plan_path[1024];
      snprintf (plan_path, sizeof (plan_path),
                "compilers/cephyr/sched/plans/%s.plan", plan_name);
      ccw_plan_write (plan, plan_path, &err);
      ccw_executor_destroy (executor);
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
      preprocessed = cpp_run_external (
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
      cpp_res = cpp_run_ucpp (
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

  /* Step 4: semantic analysis is dispatched through Oeuph's sema-salvo. */
  {
    static const char *const sema_rulesets[] = {
      "sema.scope.resolution", "sema.type.misc", "sema.mem.field",
      "sema.call.abi"
    };
    ccw_sema_report sema_report;
    char *sema_error = NULL;
    ccw_status sema_rc = ccw_sema_analyze (
        ir, CEPHYR_SEMA_SALVO_DIR, sema_rulesets,
        sizeof sema_rulesets / sizeof sema_rulesets[0], &sema_report,
        &sema_error);
    if (sema_rc != CCW_OK)
      {
        fprintf (stderr, "cephyr: semantic analysis error: %s\n",
                 sema_error ? sema_error : "unknown");
        free (sema_error);
        ccw_ir_module_destroy (ir);
        cephyr_cpp_result_free (&cpp_res);
        free (source_text);
        return CEPHYR_ERR_SEMA;
      }
  }
  /* Step 5: Run the Sched plan */
  result = run_sched_plan (ir, opts);

  /* Step 6: emit target assembly, then assemble unless -S was requested. */
  if (result == CEPHYR_SUCCESS && !opts->emit_ir)
    {
      /* §8: target text is a product of the scheduled code-generation
       * kernel.  Cephyr never synthesizes machine code in the host. */
      const char *assembly = NULL;
      char *generated_assembly = NULL;
      if (ccw_ir_function_count (ir) > 0)
        assembly = ccw_ir_attr_lookup (
            ir, ccw_ir_function_ref (ir, 0),
            "analysis.codegen.emit-x86-64.assembly");
      char assembly_template[] = "/tmp/cephyr-asm-XXXXXX";
      char object_template[] = "/tmp/cephyr-obj-XXXXXX";
      char *assembly_path = NULL;
      char *object_path = NULL;
      int fd;
      if (assembly == NULL)
        {
          generated_assembly = emit_target_assembly (ir, opts->target_triple);
          assembly = generated_assembly;
        }
      if (assembly == NULL)
        result = CEPHYR_ERR_INTERNAL;
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
          if (opts->stop_stage == CEPHYR_STOP_LINK && opts->output_path != NULL
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
              else if (opts->stop_stage == CEPHYR_STOP_NONE)
                {
                  result = link_object (opts, object_path);
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
          if (opts->stop_stage != CEPHYR_STOP_LINK && !opts->keep_temp
              && object_path != NULL)
            unlink (object_path);
        }
      free (assembly_path);
      free (generated_assembly);
      free (object_path);
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
      const char *toolchain_cpp = getenv ("CEPHYR_TOOLCHAIN_PREPROCESSOR");
      const char *toolchain_cpp2 = getenv ("CEPHYR_TOOLCHAIN_CPP");
      if (toolchain_cpp != NULL && *toolchain_cpp != '\0')
        effective.cpp_command = toolchain_cpp;
      else if (toolchain_cpp2 != NULL && *toolchain_cpp2 != '\0')
        effective.cpp_command = toolchain_cpp2;
      else if (environment_cpp != NULL && *environment_cpp != '\0')
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
  else
    {
      const char *toolchain_as = getenv ("CEPHYR_TOOLCHAIN_ASSEMBLER");
      const char *env_as = getenv ("CEPHYR_AS");
      if (toolchain_as != NULL && *toolchain_as != '\0')
        {
          effective.assembler = toolchain_as;
          effective.assembler_external = true;
        }
      else if (env_as != NULL && *env_as != '\0')
        {
          effective.assembler = env_as;
          effective.assembler_external = true;
        }
    }
  if (effective.assembler == NULL && profile.assembler != NULL)
    {
      effective.assembler = profile.assembler;
      effective.assembler_external = true;
    }
  {
    const char *env_ld = getenv ("CEPHYR_LD");
    const char *toolchain_ld = getenv ("CEPHYR_TOOLCHAIN_LINKER");
    if (env_ld != NULL && *env_ld != '\0')
      effective.linker = env_ld;
    else if (toolchain_ld != NULL && *toolchain_ld != '\0')
      effective.linker = toolchain_ld;
  }
  if (effective.linker == NULL && profile.linker != NULL)
    effective.linker = profile.linker;
  if (effective.assembler == NULL)
    {
      if (env_is_truthy ("CEPHYR_CCWAS_NO_EMBED"))
        effective.assembler_external = true;
      if (env_is_truthy ("CEPHYR_CCWLD_NO_EMBED"))
        {
          /* linker will be discovered as external */
        }
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
      include_paths = (const char **)malloc ((include_count + 1u)
                                             * sizeof (*include_paths));
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
      include_paths[include_count] = NULL;
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
