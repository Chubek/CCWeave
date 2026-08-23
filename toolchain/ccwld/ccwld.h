#ifndef CCWLD_H
#define CCWLD_H

/* Include the ABI headers first */
#include "ccwld-lto.h"
#include "ccwld-plugin.h"

/* Include the rich plan IR and expression engine */
#include "plan/ccwld_plan.h"
#include "expr/ccwld_expr.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

  /* Version stamped into the producer note (§8) and the cache key (§7.3). */
#ifndef CCWLD_VERSION
#define CCWLD_VERSION "0.1.0"
#endif

  /* ccwld_output_simple is a convenience struct for the API.
   * The full plan output type is ccwld_output in plan/ccwld_plan.h. */
  typedef struct
  {
    const char *kind, *format, *entry, *soname, *osabi;
  } ccwld_output_simple;

  /* --- link options for ccwld_link_files convenience API --- */
  typedef struct
  {
    const char *kind;
    const char *format;
    const char *entry;
    const char *soname;
    const char *osabi;
    const char *const *search_paths;
    size_t search_path_count;
  } ccwld_link_options;

  /* --- Convenience: link a plan and emit the output --- */
  int ccwld_link_run (ccwld_plan *, const char *output, ccwld_error *);

  /* --- Convenience: link files without a subprocess --- */
  int ccwld_link_files (const char *target, const char *output,
                        const char *const *inputs, size_t input_count,
                        const ccwld_link_options *options, ccwld_error *);

  /* --- Error helper: set message + exit class (§9); printf-style --- */
  void ccwld_error_set (ccwld_error *e, int code, const char *fmt, ...);

  /* --- Emission backends --- */
  int ccwld_emit_lief (const char *input, const char *output,
                       const char *kind, const char *format,
                       const char *entry, const char *note,
                       ccwld_error *);
  int ccwld_emit_binaryen (const char *output, const char *entry,
                           ccwld_error *);

  /* --- Free utility --- */
  void ccwld_free (void *);

  /* --- Frontend entry points ---
   * ccwld_run_lua: LCCWLD.md §1 — `defines` is a NULL-terminated
   * key=value list (-D), `defsymbols` likewise (--defsym), both may be
   * NULL.  The Lua state is owned by the returned plan (hooks run
   * during the link) and is closed by ccwld_plan_free. */
  int ccwld_run_lua (const char *file, const char *target,
                     const char *const *defines,
                     const char *const *defsymbols, int unsafe_lua,
                     const ccwld_driver_defs *extra, ccwld_plan **out,
                     ccwld_error *);
  int ccwld_run_ldscript (const char *script, const char *script_path,
                          const char *target, const ccwld_driver_defs *extra,
                          ccwld_plan **out, ccwld_error *);

  /* Apply driver-level declarations to an unsealed plan (frontend
   * internal; both entry points call this before running the script). */
  int ccwld_apply_driver_defs (ccwld_plan *, const ccwld_driver_defs *,
                               ccwld_error *);
  /* -e override after the script ran (GNU: command line wins). */
  int ccwld_driver_entry_override (ccwld_plan *, const char *, ccwld_error *);

#ifdef __cplusplus
}
#endif
#endif
