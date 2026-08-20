#include "ccw_test.h"
#include "sml_parthia.h"

#include <stdlib.h>
#include <string.h>
#ifndef CCW_SML_BASIS_NATIVE_PATH
#define CCW_SML_BASIS_NATIVE_PATH "libsml_basis.so"
#endif

static int
add_native (const ccw_sml_value *args, size_t nargs, ccw_sml_value *results,
            size_t nresults, void *user)
{
  (void)user;
  if (nargs != 2 || nresults != 1 || args[0].kind != CCW_SML_INT
      || args[1].kind != CCW_SML_INT)
    return 1;
  results[0].kind = CCW_SML_INT;
  results[0].integer = args[0].integer + args[1].integer;
  return 0;
}

int
main (void)
{
  const char *source
      = "signature ORD = sig type t val compare : t * t -> order end\n"
        "structure IntOrd : ORD = struct type t = int "
        "fun compare (x, y) = if x < y then LESS else GREATER end\n"
        "functor MakeSet (O : ORD) : sig type set val empty : set end = "
        "struct type set = O.t list val empty = [] end\n"
        "structure Set = MakeSet (IntOrd)\n"
        "signature REFINED = sig type t end where type t = int\n"
        "signature SHARED = sig structure A : ORD "
        "sharing type A.t = IntOrd.t end\n";
  ccw_sml_parthia_report report;
  ccw_sml_parthia_program *program;
  char *error = NULL;
  const char *surface;
  const char *core;

  program = ccw_sml_parthia_compile (source, strlen (source), &report, &error);
  CCW_CHECK (program != NULL, "module elaboration failed: %s",
             error ? error : "(no message)");
  free (error);
  if (program == NULL)
    return ccw_test_report ("sml-parthia");

  surface = ccw_sml_parthia_surface_ast (program);
  core = ccw_sml_parthia_core_ast (program);
  CCW_CHECK (surface != NULL && strstr (surface, "(signature") != NULL,
             "surface AST lost signature declarations");
  CCW_CHECK (strstr (surface, "(functor") != NULL
                 && strstr (surface, "(fctapp") != NULL,
             "surface AST lost functor declaration/application");
  CCW_CHECK (strstr (surface, "(sharing-type") != NULL
                 && strstr (surface, "(wheretype") != NULL,
             "surface AST lost module constraints");
  CCW_CHECK (report.parse.error_nodes == 0 && report.parse.missing_nodes == 0,
             "valid modular source produced parser errors");
  CCW_CHECK (
      report.parse.structure_count >= 2 && report.parse.signature_count >= 3
          && report.parse.functor_count >= 2 && report.parse.sharing_count >= 1
          && report.parse.wheretype_count >= 1,
      "module fact counts are incomplete");
  CCW_CHECK (report.functor_instances > 0
                 && report.functor_instances <= report.parse.functor_count,
             "functor applications were not deterministically materialized");
  CCW_CHECK (core != NULL && strstr (core, "(core-ml") != NULL
                 && strstr (core, "typed-facts") != NULL,
             "Parthia did not emit the signature-erased core");
  CCW_CHECK (
      strstr (core, "(instance fct.MakeSet)") != NULL,
      "functor instance name was not derived from its application path");

  {
    ccw_sml_parthia_runtime *runtime = ccw_sml_parthia_runtime_new ();
    ccw_sml_extension extension = { "add", add_native, NULL };
    ccw_sml_value args[2]
        = { { CCW_SML_INT, 2, 0.0, NULL }, { CCW_SML_INT, 3, 0.0, NULL } };
    ccw_sml_value result = { 0 };
    CCW_CHECK (runtime != NULL
                   && ccw_sml_parthia_register_extension (runtime, &extension)
                   && ccw_sml_parthia_call_native (runtime, "add", args, 2,
                                                   &result, 1)
                   && result.kind == CCW_SML_INT && result.integer == 5,
               "Parthia native extension/C interop failed");
    ccw_sml_parthia_runtime_free (runtime);
  }

  {
    ccw_sml_parthia_runtime *runtime = ccw_sml_parthia_runtime_new ();
    ccw_sml_parthia_program *loaded;
    const char directive[]
        = "use \"interpreters/sml-parthia/tests/fixtures/use-library.sml\";\n"
          "val local_value = loaded_from_library\n";
    loaded = ccw_sml_parthia_compile_with_runtime (
        runtime, directive, strlen (directive), NULL, &error);
    CCW_CHECK (loaded != NULL && ccw_sml_parthia_surface_ast (loaded) != NULL,
               "SML use directive failed: %s", error ? error : "(none)");
    ccw_sml_parthia_program_destroy (loaded);
    ccw_sml_parthia_runtime_free (runtime);
    free (error);
    error = NULL;
  }

  {
    char *resolved = ccw_sml_parthia_resolve_path ("no/such/file.sml");
    CCW_CHECK (resolved != NULL
                   && strcmp (resolved, "no/such/file.sml") == 0,
               "resolver did not pass literal paths through");
    free (resolved);
    CCW_CHECK (ccw_sml_parthia_resolve_path (
                   "parthia-resolver-no-such-bare-name") == NULL,
               "resolver invented a path for an unsearchable bare name");
  }

  {
    ccw_sml_ffi_library library
        = ccw_sml_parthia_ffi_open (CCW_SML_BASIS_NATIVE_PATH);
    void *symbol = ccw_sml_parthia_ffi_symbol (library, "ccw_sml_basis_abs");
    long long argument = -9, result = 0;
    CCW_CHECK (
        library != NULL && symbol != NULL
            && ccw_sml_parthia_ffi_call_i64 (symbol, &argument, 1, &result)
            && result == 9,
        "SML Basis FFI call failed");
    ccw_sml_parthia_ffi_close (library);
  }

  ccw_sml_parthia_program_destroy (program);
  return ccw_test_report ("sml-parthia");
}
