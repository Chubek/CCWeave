/* Cephyr preprocessor — §4.
 *
 * Wraps ucpp to implement C translation phases 1–6. Produces a
 * preprocessed token stream plus a line map so diagnostics always
 * report original file/line/column. If $CEPHYR_CPP is set, the
 * driver can use that external preprocessor instead. */

#ifndef CEPHYR_CPP_H
#define CEPHYR_CPP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- line-map entry ---------- */

/* A line-map entry maps a line in the preprocessed output to the
 * original source file, line, and column. */
typedef struct {
    const char *source_file;   /* original source path */
    int         source_line;   /* 1-based */
    int         source_column; /* 1-based */
    int         output_line;   /* 1-based line in preprocessed output */
} cephyr_line_map_entry;

/* ---------- preprocessor result ---------- */

typedef struct {
    char                 *text;          /* preprocessed source (malloc'd) */
    size_t                text_len;      /* length of preprocessed text */
    cephyr_line_map_entry *line_map;     /* array of entries (malloc'd) */
    int                   line_map_count;
    char                 *error_message; /* NULL on success */
} cephyr_cpp_result;

/* ---------- lifecycle ---------- */

/* Preprocess `source_text` (the original C source). Returns a result
 * that must be freed with cephyr_cpp_result_free(). On failure,
 * result->error_message is set and result->text is NULL. */
cephyr_cpp_result cephyr_cpp_preprocess(const char *source_text,
                                        size_t source_len,
                                        const char *source_name,
                                        const char *const *include_paths,
                                        int include_path_count);

/* Free a preprocessor result. Safe to call on a zeroed result. */
void cephyr_cpp_result_free(cephyr_cpp_result *res);

/* Run an external preprocessor ($CEPHYR_CPP) on a file. Returns
 * the preprocessed output as a malloc'd string (caller frees).
 * On failure, returns NULL and sets *error_message. */
char *cephyr_cpp_external(const char *source_path,
                          const char *cpp_command,
                          char **error_message);

/* Resolve the line-map entry for a given output line. Returns NULL
 * if the line has no mapping (e.g. builtin expansions). */
const cephyr_line_map_entry *cephyr_cpp_lookup_line(const cephyr_cpp_result *res,
                                                    int output_line);

#ifdef __cplusplus
}
#endif
#endif /* CEPHYR_CPP_H */
