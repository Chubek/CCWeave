/* Cephyr driver — §8.
 *
 * CLI entry point, toolchain discovery, plugin loading, and Sched plan
 * orchestration. The driver ties together: preprocessor → Swaff C
 * frontend → sema → lowering → Sched plan execution. */

#ifndef CEPHYR_DRIVER_H
#define CEPHYR_DRIVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C"
{
#endif

  /* ---------- optimization level ---------- */

  typedef enum
  {
    CEPHYR_O0 = 0,
    CEPHYR_O1 = 1,
    CEPHYR_O2 = 2
  } cephyr_opt_level;

  typedef enum
  {
    CEPHYR_STOP_NONE = 0,
    CEPHYR_STOP_PREPROCESS,
    CEPHYR_STOP_ASSEMBLER_SCRIPT,
    CEPHYR_STOP_LINK
  } cephyr_stop_stage;

  /* ---------- compilation options ---------- */

  typedef struct
  {
    const char *source_path;    /* input .c file */
    const char *output_path;    /* .s with -S, object otherwise */
    cephyr_opt_level opt_level; /* optimization level */
    const char *cpp_command;    /* external preprocessor (NULL = use ucpp) */
    const char *target_triple;  /* e.g. "x86_64-linux-gnu" */
    const char *const *include_paths; /* -I paths */
    int include_path_count;
    const char *const *defines; /* -D defines */
    int define_count;
    const char *const *preprocessor_options; /* -Wp,a,b */
    int preprocessor_option_count;
    const char *const *preprocessor_args; /* -Xpreprocessor arg */
    int preprocessor_arg_count;
    const char *const *assembler_options; /* -Wa,a,b */
    int assembler_option_count;
    const char *const *assembler_args; /* -Xassembler arg */
    int assembler_arg_count;
    const char *const *linker_options; /* -Wl,a,b */
    int linker_option_count;
    const char *const *linker_args; /* -Xlinker arg */
    int linker_arg_count;
    const char *const *library_paths; /* -L paths */
    int library_path_count;
    const char *const *libraries; /* -l names */
    int library_count;
    const char *const *start_files; /* crt start files, linked first */
    int start_file_count;
    bool pic;
    bool pie;
    bool shared;
    bool pic_explicit;
    bool pie_explicit;
    bool shared_explicit;
    bool emit_ir; /* dump IR instead of assembly */
    cephyr_stop_stage stop_stage;
    bool keep_temp;           /* keep intermediate files */
    const char *plugin_dir;   /* extra plugin search path */
    const char *sched_script; /* custom Sched script (NULL = use O{N}.lua) */
    const char *profile_path; /* CEPHYR.yaml/.toml override */
    const char *manifest_dir; /* manifest directory */
    const char *stdlib_manifest; /* stdlib manifest (NULL = discover
                                  * via $CEPHYR_STDLIB_MANIFEST or
                                  * stdlib-salvo/libc/Libc.yaml) */
    bool opt_level_explicit;
    bool target_explicit;
    bool cpp_explicit;
    bool manifest_explicit;
    /* Toolchain discovery */
    const char *assembler;   /* system assembler (NULL = discover) */
    bool assembler_external; /* invoke assembler as a command */
    const char *linker;      /* system linker (NULL = discover) */
  } cephyr_options;

  /* ---------- compilation result ---------- */

  typedef enum
  {
    CEPHYR_SUCCESS = 0,
    CEPHYR_ERR_PREPROCESSOR = 1,
    CEPHYR_ERR_PARSE = 2,
    CEPHYR_ERR_SEMA = 3,
    CEPHYR_ERR_LOWER = 4,
    CEPHYR_ERR_SCHED = 5,
    CEPHYR_ERR_ASSEMBLE = 6,
    CEPHYR_ERR_LINK = 7,
    CEPHYR_ERR_INTERNAL = 99
  } cephyr_result;

  /* ---------- driver API ---------- */

  /* Initialize default options for a given source file. */
  void cephyr_options_init (cephyr_options *opts, const char *source_path);

  /* Compile a C source file to assembly. Returns a result code. */
  cephyr_result cephyr_compile (const cephyr_options *opts);

  /* Discover the default assembler. The returned string is heap-allocated. */
  const char *cephyr_discover_assembler (const char *target_triple);
  /* Return the ccwas architecture spelling for a Cephyr target triple. */
  const char *cephyr_target_arch (const char *target_triple);
  /* Print the triples accepted by --target. */
  void cephyr_list_target_triples (FILE *out);

  /* Discover the linker (CCWld by default). The returned string is
   * heap-allocated. */
  const char *cephyr_discover_linker (const char *target_triple);

  /* Get a human-readable string for a result code. */
  const char *cephyr_result_string (cephyr_result r);

#ifdef __cplusplus
}
#endif
#endif /* CEPHYR_DRIVER_H */
