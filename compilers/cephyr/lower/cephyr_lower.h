/* Cephyr lowering — §6.
 *
 * Lowers the typed AST to Weave IR (core) via the Kliche imperative
 * stereotype. This is the last Cephyr-owned step before the Sched plan
 * takes over. The output is a Tilly-profile Weave IR module. */

#ifndef CEPHYR_LOWER_H
#define CEPHYR_LOWER_H

#include "../../sema/cephyr_ast.h"
#include "../../../ir/ccw_ir.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- lowering context ---------- */

typedef struct cephyr_lower_ctx cephyr_lower_ctx;

cephyr_lower_ctx *cephyr_lower_create(void);
void              cephyr_lower_destroy(cephyr_lower_ctx *ctx);

/* ---------- lowering ---------- */

/* Lower a typed AST program to a Tilly-profile Weave IR module.
 * Returns the module on success, or NULL on failure (with an error
 * message in *error_message, malloc'd, caller frees). */
ccw_ir *cephyr_lower_program(cephyr_lower_ctx *ctx,
                             const cephyr_ast_node *program,
                             const char *module_name,
                             char **error_message);

/* Lower a single function definition to a function in an existing IR module.
 * Returns the function node on success, or 0 on failure. */
ccw_node cephyr_lower_function(cephyr_lower_ctx *ctx,
                               ccw_ir *ir,
                               const cephyr_ast_node *func_def,
                               char **error_message);

/* ---------- type mapping ---------- */

/* Map a Cephyr type to a Weave IR type. */
ccw_ir_type cephyr_lower_map_type(const cephyr_type *ct);

#ifdef __cplusplus
}
#endif
#endif /* CEPHYR_LOWER_H */
