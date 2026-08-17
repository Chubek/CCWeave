/* Cephyr semantic analysis — §5.
 *
 * Performs C type checking, integer promotions, usual arithmetic
 * conversions, and constant evaluation. Operates on a typed AST.
 * This is Cephyr-owned C11 host code; it runs before any Sched plan,
 * because C type semantics must be established before IR exists. */

#ifndef CEPHYR_SEMA_H
#define CEPHYR_SEMA_H

#include "cephyr_ast.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- diagnostics ---------- */

/* Stable diagnostic IDs per §9.1. */
#define CEPHYR_E0001 "CE0001: type mismatch in assignment"
#define CEPHYR_E0002 "CE0002: incompatible operand types for binary operator"
#define CEPHYR_E0003 "CE0003: undeclared identifier"
#define CEPHYR_E0004 "CE0004: redefinition of symbol"
#define CEPHYR_E0005 "CE0005: invalid operand type for unary operator"
#define CEPHYR_E0006 "CE0006: function called with wrong number of arguments"
#define CEPHYR_E0007 "CE0007: break outside of loop or switch"
#define CEPHYR_E0008 "CE0008: continue outside of loop"
#define CEPHYR_E0009 "CE0009: void expression in context requiring a value"
#define CEPHYR_E0010 "CE0010: not supported in this phase"

typedef enum {
    CEPHYR_SEV_NOTE = 0,
    CEPHYR_SEV_WARNING,
    CEPHYR_SEV_ERROR,
    CEPHYR_SEV_FATAL
} cephyr_severity;

typedef struct {
    cephyr_severity severity;
    const char     *id;          /* stable diagnostic ID */
    const char     *message;     /* human-readable message */
    const char     *source_file;
    int             source_line;
    int             source_column;
} cephyr_diagnostic;

/* Maximum diagnostics collected before bailing out. */
#define CEPHYR_MAX_DIAGNOSTICS 256

/* ---------- sema context ---------- */

typedef struct cephyr_sema_ctx cephyr_sema_ctx;

/* Create a sema context. */
cephyr_sema_ctx *cephyr_sema_create(void);
void             cephyr_sema_destroy(cephyr_sema_ctx *ctx);

/* ---------- sema phases ---------- */

/* Phase 1: build the symbol table. Walk the AST and register all
 * top-level declarations (functions, structs, unions, enums, typedefs,
 * global variables). Returns the number of errors. */
int cephyr_sema_declare(cephyr_sema_ctx *ctx, cephyr_ast_node *program);

/* Phase 2: resolve types. For each declaration, resolve type references
 * (typedefs, struct/union/enum tags) and compute complete types.
 * Returns the number of errors. */
int cephyr_sema_resolve_types(cephyr_sema_ctx *ctx, cephyr_ast_node *program);

/* Phase 3: type-check expressions and statements. Perform integer
 * promotions and usual arithmetic conversions. Returns the number of
 * errors. */
int cephyr_sema_check(cephyr_sema_ctx *ctx, cephyr_ast_node *program);

/* Run all sema phases. Returns 0 on success, or the number of errors. */
int cephyr_sema_run(cephyr_sema_ctx *ctx, cephyr_ast_node *program);

/* ---------- diagnostic collection ---------- */

int cephyr_sema_diagnostic_count(const cephyr_sema_ctx *ctx);
const cephyr_diagnostic *cephyr_sema_diagnostic_ref(const cephyr_sema_ctx *ctx, int idx);
void cephyr_sema_diagnostic_emit(FILE *out, const cephyr_diagnostic *diag);
int  cephyr_sema_error_count(const cephyr_sema_ctx *ctx);

/* ---------- type utilities ---------- */

/* Check if two types are compatible for assignment. */
bool cephyr_type_compatible(const cephyr_type *a, const cephyr_type *b);

/* Perform usual arithmetic conversions. Returns the common type. */
cephyr_type *cephyr_type_usual_arithmetic(cephyr_type *a, cephyr_type *b);

/* Integer promotion. */
cephyr_type *cephyr_type_integer_promote(cephyr_type *t);

/* Check if a type is an integer type. */
bool cephyr_type_is_integer(const cephyr_type *t);

/* Check if a type is a floating-point type. */
bool cephyr_type_is_float(const cephyr_type *t);

/* Check if a type is arithmetic (integer or float). */
bool cephyr_type_is_arithmetic(const cephyr_type *t);

/* Check if a type is scalar (arithmetic or pointer). */
bool cephyr_type_is_scalar(const cephyr_type *t);

/* Size of a type in bytes (for the target platform). */
size_t cephyr_type_size(const cephyr_type *t);

/* Alignment of a type in bytes. */
size_t cephyr_type_align(const cephyr_type *t);

/* Duplicate a type (deep copy). */
cephyr_type *cephyr_type_dup(const cephyr_type *t);

/* Free a type (deep free). */
void cephyr_type_free(cephyr_type *t);

#ifdef __cplusplus
}
#endif
#endif /* CEPHYR_SEMA_H */
