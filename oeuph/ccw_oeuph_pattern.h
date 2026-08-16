/* Oeuph pattern language.
 *
 * A pattern is an s-expression over opcodes, pattern variables, and
 * integer constants:
 *
 *   (imul ?x (iconst 8))     matches a multiply by literal 8
 *   (shl ?x (iconst 3))      the equivalent left shift
 *   ?x                       a variable binding any e-class
 *
 * Patterns denote equivalences only; there is no "rewrite to fix"
 * direction (§7.2). */

#ifndef CCW_OEUPH_PATTERN_H
#define CCW_OEUPH_PATTERN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    CCW_PAT_VAR = 0,     /* ?x            */
    CCW_PAT_CONST,       /* (iconst 8)    */
    CCW_PAT_CONST_VAR,   /* (iconst ?k)   */
    CCW_PAT_OP           /* (opcode args) */
} ccw_pat_kind;

#define CCW_PAT_MAX_CHILDREN 4
#define CCW_PAT_MAX_NODES    64

typedef struct {
    ccw_pat_kind kind;
    char         text[32];   /* opcode or variable name */
    int64_t      value;      /* CCW_PAT_CONST */
    int          children[CCW_PAT_MAX_CHILDREN];
    int          nchildren;
} ccw_pat_node;

typedef struct {
    ccw_pat_node nodes[CCW_PAT_MAX_NODES];
    int          count;
    int          root;
} ccw_pattern;

/* Returns true on success; on failure writes a short reason. */
bool ccw_pattern_parse(const char *text, ccw_pattern *out,
                       char *reason, size_t reason_size);

#endif /* CCW_OEUPH_PATTERN_H */
