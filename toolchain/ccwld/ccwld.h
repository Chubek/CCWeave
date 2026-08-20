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

  /* --- Emission backends --- */
  int ccwld_emit_lief (const char *input, const char *output,
                       const char *kind, const char *format,
                       const char *entry, const char *note,
                       ccwld_error *);
  int ccwld_emit_binaryen (const char *output, const char *entry,
                           ccwld_error *);

  /* --- Free utility --- */
  void ccwld_free (void *);

  /* --- Frontend entry points --- */
  int ccwld_run_lua (const char *, const char *, ccwld_plan **, ccwld_error *);
  int ccwld_run_script (const char *, const char *, const char *,
                        ccwld_error *);
  int ccwld_run_ldscript (const char *script, const char *target,
                          ccwld_plan **out, ccwld_error *e);

#ifdef __cplusplus
}
#endif
#endif
