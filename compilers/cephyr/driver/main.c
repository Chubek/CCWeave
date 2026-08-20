#define _POSIX_C_SOURCE 200809L

/* Cephyr CLI entry point.
 *
 * Usage: cephyr [options] <source>
 *
 * Options:
 *   -o <file>      Output file (default: a.out)
 *   -O0, -O1, -O2  Optimization level
 *   -I <dir>       Add include path
 *   -D <name>      Define macro
 *   -S             Emit assembly (stop after compilation)
 *   -c             Compile only (don't link)
 *   -E             Preprocess only
 *   --emit-ir      Dump Weave IR text
 *   --target <t>   Target triple
 *   --cpp <cmd>    Use external preprocessor
 *   --help         Show help
 *   --version      Show version
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "../profile/cephyr_profile.h"
#include "cephyr_driver.h"
#include "ketopt.h"
#include "kstring.h"
#include "kvec.h"

enum
{
  CEPHYR_OPT_EMIT_IR = 256,
  CEPHYR_OPT_TARGET,
  CEPHYR_OPT_CPP,
  CEPHYR_OPT_PROFILE,
  CEPHYR_OPT_XASSEMBLER,
  CEPHYR_OPT_XPREPROCESSOR,
  CEPHYR_OPT_XLINKER,
  CEPHYR_OPT_FPIC,
  CEPHYR_OPT_FPIE,
  CEPHYR_OPT_SHARED,
  CEPHYR_OPT_HELP,
  CEPHYR_OPT_VERSION,
  CEPHYR_OPT_LIST_TRIPLES
};

static const ko_longopt_t cephyr_long_options[]
    = { { "emit-ir", ko_no_argument, CEPHYR_OPT_EMIT_IR },
        { "target", ko_required_argument, CEPHYR_OPT_TARGET },
        { "cpp", ko_required_argument, CEPHYR_OPT_CPP },
        { "profile", ko_required_argument, CEPHYR_OPT_PROFILE },
        { "Xassembler", ko_required_argument, CEPHYR_OPT_XASSEMBLER },
        { "Xpreprocessor", ko_required_argument, CEPHYR_OPT_XPREPROCESSOR },
        { "Xlinker", ko_required_argument, CEPHYR_OPT_XLINKER },
        { "fPIC", ko_no_argument, CEPHYR_OPT_FPIC },
        { "fPIE", ko_no_argument, CEPHYR_OPT_FPIE },
        { "PIC", ko_no_argument, CEPHYR_OPT_FPIC },
        { "PIE", ko_no_argument, CEPHYR_OPT_FPIE },
        { "shared", ko_no_argument, CEPHYR_OPT_SHARED },
        { "help", ko_no_argument, CEPHYR_OPT_HELP },
        { "version", ko_no_argument, CEPHYR_OPT_VERSION },
        { "lstriples", ko_no_argument, CEPHYR_OPT_LIST_TRIPLES },
        { NULL, 0, 0 } };

static void
print_help (const char *prog)
{
  printf ("Cephyr C compiler v0.1.0 — CCWeave-based C17 compiler\n");
  printf ("Usage: %s [options] <source>\n\n", prog);
  printf ("Options:\n");
  printf ("  -o <file>      Output file (default: stdout)\n");
  printf ("  -O0, -O1, -O2  Optimization level (default: -O0)\n");
  printf ("  -I <dir>       Add include path\n");
  printf ("  -D <name>      Define preprocessor macro\n");
  printf (
      "  -Wp,a,b        Pass comma-separated options to the preprocessor\n");
  printf ("  -Wa,a,b        Pass comma-separated options to the assembler\n");
  printf ("  -Wl,a,b        Pass comma-separated options to the linker\n");
  printf ("  -Xpreprocessor <arg>\n");
  printf ("  -Xassembler <arg>\n");
  printf ("  -Xlinker <arg>\n");
  printf ("  -L <dir>       Add linker library search path\n");
  printf ("  -l <name>      Link a shared/static library\n");
  printf ("  -fPIC          Generate position-independent code\n");
  printf ("  -fPIE          Generate position-independent executable code\n");
  printf ("  -PIC/-PIE      Accepted aliases for -fPIC/-fPIE\n");
  printf ("  -shared        Link a shared object\n");
  printf ("  -S/-s          Emit assembly and stop before assembling\n");
  printf ("  -E             Preprocess only\n");
  printf ("  -c             Compile only (don't link)\n");
  printf ("  .s/.as/.asm    Assemble directly with CCWas by default\n");
  printf ("  .S             Preprocess, then assemble\n");
  printf ("  --emit-ir      Dump Weave IR text\n");
  printf ("  --target <t>   Target triple (default: x86_64-linux-gnu)\n");
  printf ("  --lstriples     List supported target triples and exit\n");
  printf ("  --cpp <cmd>    Use external preprocessor\n");
  printf ("  --profile <p>  Load CEPHYR.yaml/toml profile\n");
  printf ("Environment:\n");
  printf ("  CEPHYR_AS, CEPHYR_LD      Override assembler/linker discovery\n");
  printf ("  CEPHYR_AS_EXTENSIONS      Comma-separated extra assembler "
          "suffixes\n");
  printf ("  CEPHYR_STDLIB_MANIFEST    Stdlib manifest (default: in-tree\n");
  printf ("                            stdlib-salvo/libc/Libc.yaml)\n");
  printf ("  profile init [--yaml|--toml]\n");
  printf ("  run <command>  Run a named profile command\n");
  printf ("  --help         Show this help\n");
  printf ("  --version      Show version\n");
}

typedef kvec_t (char *) cephyr_string_vector;

static void
string_vector_init (cephyr_string_vector *vector)
{
  kv_init (*vector);
}

static void
string_vector_destroy (cephyr_string_vector *vector)
{
  for (size_t i = 0; i < vector->n; ++i)
    free (vector->a[i]);
  kv_destroy (*vector);
  kv_init (*vector);
}

static int
string_vector_push (cephyr_string_vector *vector, const char *value)
{
  char *copy;
  if (value == NULL || *value == '\0')
    return 0;
  {
    kstring_t text = { 0, 0, NULL };
    if (kputs (value, &text) == EOF)
      return 0;
    copy = ks_release (&text);
  }
  if (copy == NULL)
    return 0;
  if (vector->n == vector->m
      && kv_resize (char *, *vector, vector->m ? vector->m * 2u : 4u) == NULL)
    {
      free (copy);
      return 0;
    }
  vector->a[vector->n++] = copy;
  return 1;
}

static int
string_vector_push_csv (cephyr_string_vector *vector, const char *value)
{
  const char *start = value;
  const char *comma;
  char *piece;
  size_t length;
  if (value == NULL || *value == '\0')
    return 0;
  for (;;)
    {
      comma = strchr (start, ',');
      length = comma ? (size_t)(comma - start) : strlen (start);
      if (length == 0)
        return 0;
      {
        kstring_t text = { 0, 0, NULL };
        if (kputsn (start, (int)length, &text) == EOF)
          return 0;
        piece = ks_release (&text);
      }
      if (piece == NULL)
        return 0;
      if (!string_vector_push (vector, piece))
        {
          free (piece);
          return 0;
        }
      free (piece);
      if (comma == NULL)
        break;
      start = comma + 1;
    }
  return 1;
}

static void
request_stop_stage (cephyr_options *options, cephyr_stop_stage stage)
{
  if (options->stop_stage == CEPHYR_STOP_NONE || stage < options->stop_stage)
    options->stop_stage = stage;
}

static char **
normalize_long_options (int argc, char **argv, int *out_argc)
{
  kvec_t (char *) vector = { 0, 0, NULL };
  char **normalized;
  int count = 0;
  if (kv_resize (char *, vector, (size_t)argc + 1u) == NULL)
    return NULL;
  normalized = vector.a;
  memset (normalized, 0, ((size_t)argc + 1u) * sizeof (*normalized));
  for (int i = 0; i < argc; ++i)
    {
      if (!strcmp (argv[i], "-Xassembler")
          || !strcmp (argv[i], "-Xpreprocessor")
          || !strcmp (argv[i], "-Xlinker"))
        {
          const char *name = argv[i] + 1;
          if (i + 1 >= argc)
            {
              kv_destroy (vector);
              return NULL;
            }
          normalized[count++] = (char *)(name[1] == 'a'   ? "--Xassembler"
                                         : name[1] == 'p' ? "--Xpreprocessor"
                                                          : "--Xlinker");
          normalized[count++] = argv[++i];
        }
      else if (!strcmp (argv[i], "-fPIC") || !strcmp (argv[i], "-PIC"))
        {
          normalized[count++] = "--fPIC";
        }
      else if (!strcmp (argv[i], "-fPIE") || !strcmp (argv[i], "-PIE"))
        {
          normalized[count++] = "--fPIE";
        }
      else if (!strcmp (argv[i], "-shared"))
        {
          normalized[count++] = "--shared";
        }
      else
        {
          normalized[count++] = argv[i];
        }
    }
  normalized[count] = NULL;
  *out_argc = count;
  return normalized;
}

static void
print_version (void)
{
  printf ("Cephyr v0.1.0 — CCWeave-based C17 compiler\n");
  printf ("Copyright (c) 2026 CCWeave Project\n");
}

static int
profile_init_command (int argc, char **argv)
{
  cephyr_profile_format format = CEPHYR_PROFILE_YAML;
  const char *path = "CEPHYR.yaml";
  char error[512] = { 0 };
  for (int i = 3; i < argc; ++i)
    {
      if (!strcmp (argv[i], "--yaml"))
        {
          format = CEPHYR_PROFILE_YAML;
          path = "CEPHYR.yaml";
        }
      else if (!strcmp (argv[i], "--toml"))
        {
          format = CEPHYR_PROFILE_TOML;
          path = "CEPHYR.toml";
        }
      else
        {
          fprintf (stderr, "cephyr: unknown profile init option '%s'\n",
                   argv[i]);
          return 2;
        }
    }
  if (!cephyr_profile_init_file (path, format, error, sizeof (error)))
    {
      fprintf (stderr, "cephyr: %s\n", error);
      return 1;
    }
  printf ("created %s\n", path);
  return 0;
}

static int
run_profile_command (int argc, char **argv)
{
  const char *command_name;
  const char *explicit_profile = NULL;
  char *profile_path = NULL;
  char error[512] = { 0 };
  cephyr_profile profile;
  const cephyr_profile_command *command;
  int status;

  if (argc < 3)
    {
      fprintf (stderr, "Usage: %s run <command> [--profile PATH]\n", argv[0]);
      return 2;
    }
  command_name = argv[2];
  for (int i = 3; i < argc; ++i)
    {
      if (!strcmp (argv[i], "--profile") && i + 1 < argc)
        explicit_profile = argv[++i];
      else
        {
          fprintf (stderr, "cephyr: unknown run option '%s'\n", argv[i]);
          return 2;
        }
    }
  if (explicit_profile != NULL)
    {
      kstring_t path = { 0, 0, NULL };
      if (explicit_profile == NULL || kputs (explicit_profile, &path) == EOF)
        {
          return 1;
        }
      profile_path = ks_release (&path);
    }
  else
    profile_path = cephyr_profile_discover (".", error, sizeof (error));
  memset (&profile, 0, sizeof (profile));
  if (profile_path == NULL
      || !cephyr_profile_load (profile_path, &profile, error, sizeof (error)))
    {
      fprintf (stderr, "cephyr: profile error: %s\n",
               error[0] ? error : "cannot load profile");
      free (profile_path);
      return 1;
    }
  command = cephyr_profile_find_command (&profile, command_name);
  if (command == NULL)
    {
      fprintf (stderr, "cephyr: profile command '%s' is not defined\n",
               command_name);
      cephyr_profile_destroy (&profile);
      free (profile_path);
      return 1;
    }
  status = system (command->command);
  cephyr_profile_destroy (&profile);
  free (profile_path);
  if (status == -1)
    {
      fprintf (stderr, "cephyr: cannot run command '%s'\n", command_name);
      return 1;
    }
  if (WIFEXITED (status))
    return WEXITSTATUS (status);
  if (WIFSIGNALED (status))
    {
      fprintf (stderr, "cephyr: command '%s' terminated by signal %d\n",
               command_name, WTERMSIG (status));
      return 128 + WTERMSIG (status);
    }
  return 1;
}

int
main (int argc, char **argv)
{
  if (argc >= 2 && !strcmp (argv[1], "profile") && argc >= 3
      && !strcmp (argv[2], "init"))
    return profile_init_command (argc, argv);
  if (argc >= 2 && !strcmp (argv[1], "run"))
    return run_profile_command (argc, argv);

  cephyr_options opts;
  memset (&opts, 0, sizeof (opts));
  opts.opt_level = CEPHYR_O0;
  kvec_t (const char *) include_paths;
  kvec_t (const char *) defines;
  cephyr_string_vector preprocessor_options;
  cephyr_string_vector preprocessor_args;
  cephyr_string_vector assembler_options;
  cephyr_string_vector assembler_args;
  cephyr_string_vector linker_options;
  cephyr_string_vector linker_args;
  cephyr_string_vector library_paths;
  cephyr_string_vector libraries;
  kv_init (include_paths);
  kv_init (defines);
  string_vector_init (&preprocessor_options);
  string_vector_init (&preprocessor_args);
  string_vector_init (&assembler_options);
  string_vector_init (&assembler_args);
  string_vector_init (&linker_options);
  string_vector_init (&linker_args);
  string_vector_init (&library_paths);
  string_vector_init (&libraries);

  const char *source_path = NULL;
  int exit_code = 1;
  int parse_argc = argc;
  char **parse_argv = normalize_long_options (argc, argv, &parse_argc);
  ketopt_t parser = KETOPT_INIT;
  int opt;

  /* Klib's ketopt handles short/long options and keeps collection storage
   * in Klib vectors until compilation has consumed it. */
  if (parse_argv == NULL)
    {
      fprintf (stderr, "cephyr: malformed option\n");
      goto cleanup;
    }
  while ((opt = ketopt (&parser, parse_argc, parse_argv, 1,
                        "o:I:D:O:W:L:l:SEschV", cephyr_long_options))
         != -1)
    {
      switch (opt)
        {
        case 'o':
          opts.output_path = parser.arg;
          break;
        case 'I':
          kv_push (const char *, include_paths, parser.arg);
          break;
        case 'D':
          kv_push (const char *, defines, parser.arg);
          break;
        case 'W':
          if (parser.arg == NULL
              || (parser.arg[0] != 'p' && parser.arg[0] != 'a'
                  && parser.arg[0] != 'l')
              || parser.arg[1] != ',')
            {
              fprintf (stderr, "cephyr: expected -Wp, -Wa, or -Wl options\n");
              goto cleanup;
            }
          if (!string_vector_push_csv (
                  parser.arg[0] == 'p'   ? &preprocessor_options
                  : parser.arg[0] == 'a' ? &assembler_options
                                         : &linker_options,
                  parser.arg + 2))
            {
              fprintf (stderr,
                       "cephyr: invalid comma-separated option list\n");
              goto cleanup;
            }
          break;
        case 'L':
          if (!string_vector_push (&library_paths, parser.arg))
            {
              fprintf (stderr, "cephyr: out of memory storing -L path\n");
              goto cleanup;
            }
          break;
        case 'l':
          if (!string_vector_push (&libraries, parser.arg))
            {
              fprintf (stderr, "cephyr: out of memory storing -l library\n");
              goto cleanup;
            }
          break;
        case 'O':
          opts.opt_level_explicit = true;
          if (!parser.arg || strcmp (parser.arg, "0") == 0)
            opts.opt_level = CEPHYR_O0;
          else if (strcmp (parser.arg, "1") == 0)
            opts.opt_level = CEPHYR_O1;
          else if (strcmp (parser.arg, "2") == 0)
            opts.opt_level = CEPHYR_O2;
          else
            {
              fprintf (stderr, "cephyr: invalid optimization level '-O%s'\n",
                       parser.arg ? parser.arg : "");
              goto cleanup;
            }
          break;
        case 'S':
        case 's':
          request_stop_stage (&opts, CEPHYR_STOP_ASSEMBLER_SCRIPT);
          break;
        case 'c':
          request_stop_stage (&opts, CEPHYR_STOP_LINK);
          break;
        case 'E':
          request_stop_stage (&opts, CEPHYR_STOP_PREPROCESS);
          break;
        case 'h':
        case CEPHYR_OPT_HELP:
          print_help (argv[0]);
          exit_code = 0;
          goto cleanup;
        case 'V':
        case CEPHYR_OPT_VERSION:
          print_version ();
          exit_code = 0;
          goto cleanup;
        case CEPHYR_OPT_LIST_TRIPLES:
          cephyr_list_target_triples (stdout);
          exit_code = 0;
          goto cleanup;
        case CEPHYR_OPT_EMIT_IR:
          opts.emit_ir = true;
          break;
        case CEPHYR_OPT_TARGET:
          opts.target_triple = parser.arg;
          opts.target_explicit = true;
          break;
        case CEPHYR_OPT_CPP:
          opts.cpp_command = parser.arg;
          opts.cpp_explicit = true;
          break;
        case CEPHYR_OPT_PROFILE:
          opts.profile_path = parser.arg;
          break;
        case CEPHYR_OPT_XASSEMBLER:
          if (!string_vector_push (&assembler_args, parser.arg))
            {
              fprintf (stderr,
                       "cephyr: out of memory storing assembler argument\n");
              goto cleanup;
            }
          break;
        case CEPHYR_OPT_XPREPROCESSOR:
          if (!string_vector_push (&preprocessor_args, parser.arg))
            {
              fprintf (
                  stderr,
                  "cephyr: out of memory storing preprocessor argument\n");
              goto cleanup;
            }
          break;
        case CEPHYR_OPT_XLINKER:
          if (!string_vector_push (&linker_args, parser.arg))
            {
              fprintf (stderr,
                       "cephyr: out of memory storing linker argument\n");
              goto cleanup;
            }
          break;
        case CEPHYR_OPT_FPIC:
          opts.pic = true;
          opts.pic_explicit = true;
          break;
        case CEPHYR_OPT_FPIE:
          opts.pie = true;
          opts.pie_explicit = true;
          break;
        case CEPHYR_OPT_SHARED:
          opts.shared = true;
          opts.shared_explicit = true;
          break;
        case ':':
          fprintf (stderr, "cephyr: option '-%c' requires an argument\n",
                   parser.opt);
          goto cleanup;
        case '?':
        default:
          fprintf (stderr, "cephyr: unknown option\n");
          goto cleanup;
        }
    }

  if (parser.ind < parse_argc)
    source_path = parse_argv[parser.ind++];
  if (parser.ind < parse_argc)
    {
      fprintf (stderr, "cephyr: more than one source file specified\n");
      goto cleanup;
    }
  if (!source_path)
    {
      fprintf (stderr, "cephyr: no source file specified\n");
      fprintf (stderr, "Usage: %s [options] <source.c>\n", argv[0]);
      goto cleanup;
    }

  opts.source_path = source_path;
  opts.include_paths = (const char *const *)include_paths.a;
  opts.include_path_count = (int)kv_size (include_paths);
  opts.defines = (const char *const *)defines.a;
  opts.define_count = (int)kv_size (defines);
  opts.preprocessor_options = (const char *const *)preprocessor_options.a;
  opts.preprocessor_option_count = (int)preprocessor_options.n;
  opts.preprocessor_args = (const char *const *)preprocessor_args.a;
  opts.preprocessor_arg_count = (int)preprocessor_args.n;
  opts.assembler_options = (const char *const *)assembler_options.a;
  opts.assembler_option_count = (int)assembler_options.n;
  opts.assembler_args = (const char *const *)assembler_args.a;
  opts.assembler_arg_count = (int)assembler_args.n;
  opts.linker_options = (const char *const *)linker_options.a;
  opts.linker_option_count = (int)linker_options.n;
  opts.linker_args = (const char *const *)linker_args.a;
  opts.linker_arg_count = (int)linker_args.n;
  opts.library_paths = (const char *const *)library_paths.a;
  opts.library_path_count = (int)library_paths.n;
  opts.libraries = (const char *const *)libraries.a;
  opts.library_count = (int)libraries.n;

  /* Compile */
  cephyr_result result = cephyr_compile (&opts);

  if (result != CEPHYR_SUCCESS)
    {
      fprintf (stderr, "cephyr: compilation failed: %s\n",
               cephyr_result_string (result));
      goto cleanup;
    }

  exit_code = 0;

cleanup:
  kv_destroy (include_paths);
  kv_destroy (defines);
  string_vector_destroy (&preprocessor_options);
  string_vector_destroy (&preprocessor_args);
  string_vector_destroy (&assembler_options);
  string_vector_destroy (&assembler_args);
  string_vector_destroy (&linker_options);
  string_vector_destroy (&linker_args);
  string_vector_destroy (&library_paths);
  string_vector_destroy (&libraries);
  free (parse_argv);
  return exit_code;
}
