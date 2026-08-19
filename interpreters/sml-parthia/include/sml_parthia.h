/* Parthia SML '97 module elaboration façade.
 *
 * The Swaff adapter owns parsing and fixity resolution.  This interface owns
 * the semantic boundary: module declarations are checked and converted to a
 * deterministic, signature-erased core description before any Kliche/IR
 * lowering occurs (§2, D-0052). */

#ifndef CCW_SML_PARTHIA_H
#define CCW_SML_PARTHIA_H

#include "ccw_swaff_parse.h"
#include "sched.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

  typedef struct ccw_sml_parthia_program ccw_sml_parthia_program;
  typedef struct ccw_sml_parthia_runtime ccw_sml_parthia_runtime;
  typedef enum
  {
    CCW_SML_NIL = 0,
    CCW_SML_INT = 1,
    CCW_SML_BOOL = 2,
    CCW_SML_REAL = 3,
    CCW_SML_POINTER = 4
  } ccw_sml_value_kind;
  typedef struct
  {
    ccw_sml_value_kind kind;
    long long integer;
    double real;
    void *pointer;
  } ccw_sml_value;
  typedef int (*ccw_sml_native_fn) (const ccw_sml_value *, size_t,
                                    ccw_sml_value *, size_t, void *);
  typedef struct
  {
    const char *name;
    ccw_sml_native_fn invoke;
    void *userdata;
  } ccw_sml_extension;
  /* Shared objects export:
   *   const ccw_sml_extension *ccw_sml_parthia_extension_init(void); */

  typedef struct
  {
    ccw_sml_parse_report parse;
    int module_facts;
    int value_facts;
    int type_facts;
    int functor_instances;
    char message[256];
  } ccw_sml_parthia_report;

  /* Parse and elaborate an SML source file.  The returned object owns both the
   * surface AST and the signature-erased core description. */
  ccw_sml_parthia_program *
  ccw_sml_parthia_compile (const char *source, size_t source_len,
                           ccw_sml_parthia_report *report,
                           char **error_message);
  ccw_sml_parthia_program *ccw_sml_parthia_compile_with_runtime (
      ccw_sml_parthia_runtime *runtime, const char *source, size_t source_len,
      ccw_sml_parthia_report *report, char **error_message);
  ccw_sml_parthia_program *ccw_sml_parthia_compile_file (
      ccw_sml_parthia_runtime *runtime, const char *path,
      ccw_sml_parthia_report *report, char **error_message);

  void ccw_sml_parthia_program_destroy (ccw_sml_parthia_program *program);

  /* Load one of Parthia's sealed AOT scheduler pipelines.  `level` accepts
   * "O0", "O1", or "O2"; NULL selects O2.  The returned plan is owned by the
   * caller and must be released with ccw_plan_free(). */
  int ccw_sml_parthia_load_plan (const char *level, const char *manifest_dir,
                                 const char *sched_dir, ccw_plan **out,
                                 char **error_message);

  /* Stable serialized forms for pipeline hand-off and reproducibility tests.
   * The returned pointers remain owned by `program`. */
  const char *
  ccw_sml_parthia_surface_ast (const ccw_sml_parthia_program *program);
  const char *
  ccw_sml_parthia_core_ast (const ccw_sml_parthia_program *program);

  /* D-0046/D-0052 module facts. */
  int ccw_sml_parthia_structure_count (const ccw_sml_parthia_program *program);
  int ccw_sml_parthia_signature_count (const ccw_sml_parthia_program *program);
  int ccw_sml_parthia_functor_count (const ccw_sml_parthia_program *program);
  int ccw_sml_parthia_sharing_count (const ccw_sml_parthia_program *program);
  int ccw_sml_parthia_wheretype_count (const ccw_sml_parthia_program *program);

  ccw_sml_parthia_runtime *ccw_sml_parthia_runtime_new (void);
  void ccw_sml_parthia_runtime_free (ccw_sml_parthia_runtime *);
  int ccw_sml_parthia_register_extension (ccw_sml_parthia_runtime *,
                                          const ccw_sml_extension *);
  int ccw_sml_parthia_call_native (ccw_sml_parthia_runtime *, const char *,
                                   const ccw_sml_value *, size_t,
                                   ccw_sml_value *, size_t);
  int ccw_sml_parthia_load_extension (ccw_sml_parthia_runtime *, const char *);
  /* Shared objects are loaded through the vendored dynalo bridge; scalar calls
   * use dyncall and never marshal aggregate values across this C ABI. */
  typedef void *ccw_sml_ffi_library;
  ccw_sml_ffi_library ccw_sml_parthia_ffi_open (const char *);
  void *ccw_sml_parthia_ffi_symbol (ccw_sml_ffi_library, const char *);
  void ccw_sml_parthia_ffi_close (ccw_sml_ffi_library);
  int ccw_sml_parthia_ffi_call_i64 (void *, const long long *, size_t,
                                    long long *);

#ifdef __cplusplus
}
#endif
#endif
