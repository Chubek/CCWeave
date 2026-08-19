/* Swaff parse-only frontends — surface-AST production (D-0046).
 *
 * A parse-only adapter walks the vendored Tree-sitter CST and returns a
 * deterministic surface AST as S-expression text, resolving only what the
 * language Definition assigns to the parse (for SML '97: infix fixity). No
 * type inference, signature matching, overloading, or representation decision
 * happens here — those belong to the consumer's elaborator. Like the rest of
 * Swaff, this header is free of Tree-sitter declarations so nothing above or
 * below depends on the parser framework. */

#ifndef CCW_SWAFF_PARSE_H
#define CCW_SWAFF_PARSE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int  error_nodes;      /* Tree-sitter ERROR nodes seen in the CST */
    int  missing_nodes;    /* Tree-sitter MISSING nodes seen in the CST */
    int  topdec_count;     /* top-level declarations/expressions emitted */
    int  ast_nodes;        /* byte length of the emitted AST (size proxy) */
    int  structure_count;  /* structure declarations/expressions */
    int  signature_count;  /* signature declarations/expressions */
    int  functor_count;    /* functor declarations/applications */
    int  sharing_count;    /* sharing and sharing type specifications */
    int  wheretype_count;  /* where type signature refinements */
    char message[256];
} ccw_sml_parse_report;

/* Parse SML '97 `source` into a deterministic, fixity-resolved surface AST.
 * Returns malloc'd S-expression text (caller frees), or NULL on failure with
 * *error_message set (malloc'd; caller frees). The CST is fully consumed;
 * punctuation and trivia are normalized away. Output is a pure function of
 * the source text (D-0052). */
char *ccw_swaff_parse_sml(const char *source, size_t source_len,
                          ccw_sml_parse_report *report, char **error_message);

#ifdef __cplusplus
}
#endif
#endif /* CCW_SWAFF_PARSE_H */
