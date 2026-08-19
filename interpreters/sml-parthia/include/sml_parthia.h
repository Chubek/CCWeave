/* Parthia SML '97 module elaboration façade.
 *
 * The Swaff adapter owns parsing and fixity resolution.  This interface owns
 * the semantic boundary: module declarations are checked and converted to a
 * deterministic, signature-erased core description before any Kliche/IR
 * lowering occurs (§2, D-0052). */

#ifndef CCW_SML_PARTHIA_H
#define CCW_SML_PARTHIA_H

#include "ccw_swaff_parse.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ccw_sml_parthia_program ccw_sml_parthia_program;

typedef struct {
    ccw_sml_parse_report parse;
    int module_facts;
    int value_facts;
    int type_facts;
    int functor_instances;
    char message[256];
} ccw_sml_parthia_report;

/* Parse and elaborate an SML source file.  The returned object owns both the
 * surface AST and the signature-erased core description. */
ccw_sml_parthia_program *ccw_sml_parthia_compile(
    const char *source, size_t source_len,
    ccw_sml_parthia_report *report, char **error_message);

void ccw_sml_parthia_program_destroy(ccw_sml_parthia_program *program);

/* Stable serialized forms for pipeline hand-off and reproducibility tests.
 * The returned pointers remain owned by `program`. */
const char *ccw_sml_parthia_surface_ast(
    const ccw_sml_parthia_program *program);
const char *ccw_sml_parthia_core_ast(
    const ccw_sml_parthia_program *program);

/* D-0046/D-0052 module facts. */
int ccw_sml_parthia_structure_count(
    const ccw_sml_parthia_program *program);
int ccw_sml_parthia_signature_count(
    const ccw_sml_parthia_program *program);
int ccw_sml_parthia_functor_count(
    const ccw_sml_parthia_program *program);
int ccw_sml_parthia_sharing_count(
    const ccw_sml_parthia_program *program);
int ccw_sml_parthia_wheretype_count(
    const ccw_sml_parthia_program *program);

#ifdef __cplusplus
}
#endif
#endif
