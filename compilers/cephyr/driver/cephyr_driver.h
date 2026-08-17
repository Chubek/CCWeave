/* Cephyr driver — §8.
 *
 * CLI entry point, toolchain discovery, plugin loading, and Sched plan
 * orchestration. The driver ties together: preprocessor → Swaff C
 * frontend → sema → lowering → Sched plan execution. */

#ifndef CEPHYR_DRIVER_H
#define CEPHYR_DRIVER_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- optimization level ---------- */

typedef enum {
    CEPHYR_O0 = 0,
    CEPHYR_O1 = 1,
    CEPHYR_O2 = 2
} cephyr_opt_level;

/* ---------- compilation options ---------- */

typedef struct {
    const char        *source_path;       /* input .c file */
    const char        *output_path;       /* output .s file (NULL = stdout) */
    cephyr_opt_level   opt_level;         /* optimization level */
    const char        *cpp_command;       /* external preprocessor (NULL = use ucpp) */
    const char        *target_triple;     /* e.g. "x86_64-linux-gnu" */
    const char *const *include_paths;     /* -I paths */
    int                include_path_count;
    const char *const *defines;           /* -D defines */
    int                define_count;
    bool               emit_ir;           /* dump IR instead of assembly */
    bool               keep_temp;         /* keep intermediate files */
    const char        *plugin_dir;        /* extra plugin search path */
    const char        *sched_script;      /* custom Sched script (NULL = use O{N}.lua) */
    /* Toolchain discovery */
    const char        *assembler;         /* system assembler (NULL = discover) */
    const char        *linker;            /* system linker (NULL = discover) */
} cephyr_options;

/* ---------- compilation result ---------- */

typedef enum {
    CEPHYR_SUCCESS = 0,
    CEPHYR_ERR_PREPROCESSOR  = 1,
    CEPHYR_ERR_PARSE         = 2,
    CEPHYR_ERR_SEMA          = 3,
    CEPHYR_ERR_LOWER         = 4,
    CEPHYR_ERR_SCHED         = 5,
    CEPHYR_ERR_ASSEMBLE      = 6,
    CEPHYR_ERR_LINK          = 7,
    CEPHYR_ERR_INTERNAL      = 99
} cephyr_result;

/* ---------- driver API ---------- */

/* Initialize default options for a given source file. */
void cephyr_options_init(cephyr_options *opts, const char *source_path);

/* Compile a C source file to assembly. Returns a result code. */
cephyr_result cephyr_compile(const cephyr_options *opts);

/* Discover the system assembler. Returns a static string. */
const char *cephyr_discover_assembler(const char *target_triple);

/* Discover the system linker. Returns a static string. */
const char *cephyr_discover_linker(const char *target_triple);

/* Get a human-readable string for a result code. */
const char *cephyr_result_string(cephyr_result r);

#ifdef __cplusplus
}
#endif
#endif /* CEPHYR_DRIVER_H */
