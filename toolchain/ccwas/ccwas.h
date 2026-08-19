#ifndef CCWAS_H
#define CCWAS_H

#include "obj/ccw_obj.h"
#include "sema/ccw_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

  typedef struct
  {
    ccw_arch_t arch;
    const char *syntax;
    ccw_obj_format_t format;
    const char *const *defines;
    size_t define_count;
    int force_template;
    int unsafe_lua;
    int werror;
  } ccwas_options;

  /* Assemble an in-memory translation unit without invoking a subprocess. */
  int ccwas_assemble (const char *source, const char *filename,
                      const ccwas_options *options, const char *output_path,
                      char **error);

  /* File-oriented convenience wrapper for embedders. */
  int ccwas_assemble_file (const char *input_path,
                           const ccwas_options *options,
                           const char *output_path, char **error);

  void ccwas_free_error (char *error);

#ifdef __cplusplus
}
#endif
#endif
