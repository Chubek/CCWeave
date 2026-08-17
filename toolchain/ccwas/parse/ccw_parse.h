#ifndef CCWAS_PARSE_H
#define CCWAS_PARSE_H
/* §4: MPC-based assembly parser — architecture-agnostic with per-target grammar */

#include "ccw_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Parse an assembly source text into a unit's statement vector.
 * Returns 1 on success, 0 on failure (error string set). */
int ccw_parse_asm(ccw_unit_t *u, const char *source, const char *filename,
                  char **error);

/* Parse a single line of assembly. Returns 1 if a statement was produced,
 * 0 if empty/comment, -1 on error. */
int ccw_parse_line(const char *line, ccw_stmt_t *stmt, ccw_arch_t arch,
                   const char *syntax, char **error);

/* Parse a register name, returning the canonical name, or NULL */
const char *ccw_parse_reg(ccw_arch_t arch, const char *name);

/* Parse a memory operand string like "[rax + 8]" or "[rbx + rcx*4 - 16]" */
int ccw_parse_mem(ccw_arch_t arch, const char *s, ccw_mem_t *mem, char **error);

/* Parse comma-separated operands */
int ccw_parse_operands(ccw_arch_t arch, const char *s, ccw_operand_t **out,
                       size_t *count, char **error);

/* Parse a data directive value list */
int ccw_parse_data_values(const char *s, ccw_data_vec_t *vec, int width,
                          char **error);

#ifdef __cplusplus
}
#endif

#endif /* CCWAS_PARSE_H */
