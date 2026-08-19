#include "sml_parthia.h"
#include "ccw_test.h"

#include <stdlib.h>
#include <string.h>

int main(void)
{
    const char *source =
        "signature ORD = sig type t val compare : t * t -> order end\n"
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

    program = ccw_sml_parthia_compile(source, strlen(source), &report, &error);
    CCW_CHECK(program != NULL, "module elaboration failed: %s",
              error ? error : "(no message)");
    free(error);
    if (program == NULL) return ccw_test_report("sml-parthia");

    surface = ccw_sml_parthia_surface_ast(program);
    core = ccw_sml_parthia_core_ast(program);
    CCW_CHECK(surface != NULL && strstr(surface, "(signature") != NULL,
              "surface AST lost signature declarations");
    CCW_CHECK(strstr(surface, "(functor") != NULL &&
                  strstr(surface, "(fctapp") != NULL,
              "surface AST lost functor declaration/application");
    CCW_CHECK(strstr(surface, "(sharing-type") != NULL &&
                  strstr(surface, "(wheretype") != NULL,
              "surface AST lost module constraints");
    CCW_CHECK(report.parse.error_nodes == 0 &&
                  report.parse.missing_nodes == 0,
              "valid modular source produced parser errors");
    CCW_CHECK(report.parse.structure_count >= 2 &&
                  report.parse.signature_count >= 3 &&
                  report.parse.functor_count >= 2 &&
                  report.parse.sharing_count >= 1 &&
                  report.parse.wheretype_count >= 1,
              "module fact counts are incomplete");
    CCW_CHECK(report.functor_instances > 0 &&
                  report.functor_instances <= report.parse.functor_count,
              "functor applications were not deterministically materialized");
    CCW_CHECK(core != NULL && strstr(core, "(core-ml") != NULL &&
                  strstr(core, "typed-facts") != NULL,
              "Parthia did not emit the signature-erased core");
    CCW_CHECK(strstr(core, "(instance fct.MakeSet)") != NULL,
              "functor instance name was not derived from its application path");

    ccw_sml_parthia_program_destroy(program);
    return ccw_test_report("sml-parthia");
}
