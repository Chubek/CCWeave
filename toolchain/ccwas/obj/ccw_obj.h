#ifndef CCWAS_OBJ_H
#define CCWAS_OBJ_H
/* §9: Object writer — ELF64, PE, Mach-O emission */

#include "ccw_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

  /* Supported output formats */
  typedef enum
  {
    CCW_FMT_ELF,   /* ELF64 relocatable (.o) */
    CCW_FMT_PE,    /* PE/COFF (.obj) */
    CCW_FMT_MACHO, /* Mach-O (.o) */
    CCW_FMT_WASM   /* WebAssembly object module */
  } ccw_obj_format_t;

  /* Write the assembled unit to an object file.
   * Returns 1 on success, 0 on failure (error string set). */
  int ccw_obj_write (const ccw_unit_t *u, const char *path,
                     ccw_obj_format_t fmt, char **error);

  /* Get the default output format for the host platform. */
  ccw_obj_format_t ccw_obj_default_format (void);

#ifdef __cplusplus
}
#endif

#endif /* CCWAS_OBJ_H */
