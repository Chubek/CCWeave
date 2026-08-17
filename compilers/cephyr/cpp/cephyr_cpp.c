/* Cephyr preprocessor — wraps ucpp (§4).
 *
 * ucpp is a C11 implementation of translation phases 1–6. We use it as
 * a library (compiled with STAND_ALONE not defined) to produce a
 * preprocessed token stream. A line map is built so diagnostics always
 * report original source locations. */

#include "cephyr_cpp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ucpp internal headers. We compile ucpp with STAND_ALONE undefined. */
#include "../../../third_party/ucpp/mem.h"
#include "../../../third_party/ucpp/cpp.h"
#include "../../../third_party/ucpp/tune.h"

/* ---------- line-map builder ---------- */

typedef struct {
    cephyr_line_map_entry *entries;
    int                     count;
    int                     capacity;
    char                  **filenames;
    int                     filename_count;
    int                     filename_capacity;
    int                     current_line;  /* 1-based, in output */
    int                     source_line;   /* 1-based, in current source file */
    int                     source_idx;    /* index into filenames[] */
} line_map_builder;

static void lmb_init(line_map_builder *lmb)
{
    memset(lmb, 0, sizeof(*lmb));
    lmb->current_line = 1;
    lmb->source_line = 1;
    lmb->source_idx = -1;
}

static void lmb_add_file(line_map_builder *lmb, const char *filename)
{
    if (lmb->filename_count >= lmb->filename_capacity) {
        lmb->filename_capacity = lmb->filename_capacity ? lmb->filename_capacity * 2 : 8;
        lmb->filenames = realloc(lmb->filenames,
                                 (size_t)lmb->filename_capacity * sizeof(char *));
    }
    lmb->filenames[lmb->filename_count] = strdup(filename);
    lmb->source_idx = lmb->filename_count;
    lmb->filename_count++;
}

static void lmb_add_entry(line_map_builder *lmb, int output_line,
                          int source_line, int source_idx)
{
    if (lmb->count >= lmb->capacity) {
        lmb->capacity = lmb->capacity ? lmb->capacity * 2 : 256;
        lmb->entries = realloc(lmb->entries,
                               (size_t)lmb->capacity * sizeof(cephyr_line_map_entry));
    }
    cephyr_line_map_entry *e = &lmb->entries[lmb->count++];
    e->source_file   = (source_idx >= 0) ? lmb->filenames[source_idx] : "<builtin>";
    e->source_line   = source_line;
    e->source_column = 1;
    e->output_line   = output_line;
}

static void lmb_free(line_map_builder *lmb)
{
    free(lmb->entries);
    for (int i = 0; i < lmb->filename_count; i++)
        free(lmb->filenames[i]);
    free(lmb->filenames);
}

/* ---------- preprocessor wrapper ---------- */

cephyr_cpp_result cephyr_cpp_preprocess(const char *source_text,
                                        size_t source_len,
                                        const char *source_name,
                                        const char *const *include_paths,
                                        int include_path_count)
{
    cephyr_cpp_result result;
    memset(&result, 0, sizeof(result));

    /* Step 1: initialise ucpp */
    init_cpp();

    /* Step 2: configure builtin macros */
    no_special_macros = 0;
    emit_defines = emit_assertions = 0;

    /* Step 3: initialise tables (with assertions) */
    init_tables(1);

    /* Step 4: set include paths */
    init_include_path(include_path_count);
    for (int i = 0; i < include_path_count; i++)
        add_include_path(strdup(include_paths[i]));

    /* Step 5: dependencies */
    emit_dependencies = 0;

    /* Step 6: set the source filename */
    set_init_filename(source_name, 0);

    /* Step 7: initialise lexer */
    struct lexer_state ls;
    init_lexer_state(&ls);
    init_lexer_mode(&ls);
    ls.flags |= HANDLE_ASSERTIONS | HANDLE_PRAGMA | LINE_NUM | KEEP_OUTPUT;

    /* Step 8: feed source text through a temporary file */
    FILE *tmpf = tmpfile();
    if (!tmpf) {
        result.error_message = strdup("failed to create temporary file for preprocessor");
        return result;
    }
    fwrite(source_text, 1, source_len, tmpf);
    rewind(tmpf);
    ls.input = tmpf;

    /* Step 9: collect output */
    char *output = NULL;
    size_t output_len = 0;
    size_t output_cap = 0;
    line_map_builder lmb;
    lmb_init(&lmb);
    lmb_add_file(&lmb, source_name ? source_name : "<input>");

    /* Step 10: tokenize */
    int tok;
    int add_newline = 0;

    while ((tok = next_token(&ls)) != CPPERR) {
        if (tok == NEWLINE) {
            lmb.current_line++;
            lmb.source_line++;
            if (add_newline) {
                if (output_len + 2 > output_cap) {
                    output_cap = output_cap ? output_cap * 2 : 4096;
                    output = realloc(output, output_cap);
                }
                output[output_len++] = '\n';
            }
            add_newline = 0;
        } else if (tok == CONTEXT) {
            /* #line directive or file change */
            lmb_add_entry(&lmb, lmb.current_line, lmb.source_line, lmb.source_idx);
            /* The lexer state has the new file/line info */
            add_newline = 0;
        } else if (tok == COMMENT) {
            /* Replace comments with a single space */
            if (output_len > 0 && output[output_len - 1] != ' ' &&
                output[output_len - 1] != '\n') {
                if (output_len + 1 > output_cap) {
                    output_cap = output_cap ? output_cap * 2 : 4096;
                    output = realloc(output, output_cap);
                }
                output[output_len++] = ' ';
            }
        } else if (tok == NONE) {
            /* Whitespace — collapse to single space */
            if (output_len > 0 && output[output_len - 1] != ' ' &&
                output[output_len - 1] != '\n') {
                if (output_len + 1 > output_cap) {
                    output_cap = output_cap ? output_cap * 2 : 4096;
                    output = realloc(output, output_cap);
                }
                output[output_len++] = ' ';
            }
        } else {
            /* Emit the token text */
            const char *token_text = token_name(&ls);
            if (token_text) {
                size_t tlen = strlen(token_text);
                if (output_len + tlen + 2 > output_cap) {
                    output_cap = output_cap ? output_cap * 2 : 4096;
                    if (output_cap < output_len + tlen + 2)
                        output_cap = output_len + tlen + 4096;
                    output = realloc(output, output_cap);
                }
                if (output_len > 0 && output[output_len - 1] != ' ' &&
                    output[output_len - 1] != '\n' &&
                    output[output_len - 1] != '(' &&
                    output[output_len - 1] != '[' &&
                    output[output_len - 1] != '{' &&
                    *token_text != ')' && *token_text != ']' &&
                    *token_text != '}' && *token_text != ',' &&
                    *token_text != ';') {
                    output[output_len++] = ' ';
                }
                memcpy(output + output_len, token_text, tlen);
                output_len += tlen;
            }
            add_newline = 1;
        }
    }

    /* Add final newline */
    if (output_len > 0 && output[output_len - 1] != '\n') {
        if (output_len + 1 > output_cap) {
            output_cap = output_cap ? output_cap * 2 : 4096;
            output = realloc(output, output_cap);
        }
        output[output_len++] = '\n';
    }

    /* Null-terminate */
    if (output_len + 1 > output_cap) {
        output_cap = output_len + 1;
        output = realloc(output, output_cap);
    }
    output[output_len] = '\0';

    /* Cleanup */
    fclose(tmpf);
    wipe_assertions();
    wipe_defines();

    /* Build result */
    result.text = output;
    result.text_len = output_len;
    result.entries = lmb.entries;
    result.line_map_count = lmb.count;
    lmb.entries = NULL; /* ownership transferred */
    lmb.count = 0;
    lmb_free(&lmb);

    return result;
}

void cephyr_cpp_result_free(cephyr_cpp_result *res)
{
    if (!res) return;
    free(res->text);
    free(res->line_map);
    free(res->error_message);
    memset(res, 0, sizeof(*res));
}

const cephyr_line_map_entry *cephyr_cpp_lookup_line(const cephyr_cpp_result *res,
                                                    int output_line)
{
    if (!res || !res->line_map) return NULL;
    /* Binary search for the closest entry at or before output_line */
    int lo = 0, hi = res->line_map_count - 1;
    const cephyr_line_map_entry *best = NULL;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (res->line_map[mid].output_line <= output_line) {
            best = &res->line_map[mid];
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return best;
}

char *cephyr_cpp_external(const char *source_path,
                          const char *cpp_command,
                          char **error_message)
{
    if (!cpp_command) {
        if (error_message) *error_message = strdup("no external preprocessor command set");
        return NULL;
    }

    /* Build command: cpp_command source_path */
    size_t cmd_len = strlen(cpp_command) + strlen(source_path) + 4;
    char *cmd = malloc(cmd_len);
    snprintf(cmd, cmd_len, "%s %s", cpp_command, source_path);

    FILE *pipe = popen(cmd, "r");
    free(cmd);
    if (!pipe) {
        if (error_message) {
            char buf[512];
            snprintf(buf, sizeof(buf), "failed to run external preprocessor: %s", cpp_command);
            *error_message = strdup(buf);
        }
        return NULL;
    }

    /* Read all output */
    char *output = NULL;
    size_t output_len = 0;
    size_t output_cap = 0;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), pipe)) > 0) {
        if (output_len + n + 1 > output_cap) {
            output_cap = output_cap ? output_cap * 2 : 4096;
            if (output_cap < output_len + n + 1)
                output_cap = output_len + n + 1;
            output = realloc(output, output_cap);
        }
        memcpy(output + output_len, buf, n);
        output_len += n;
    }

    int status = pclose(pipe);
    if (status != 0) {
        free(output);
        if (error_message) {
            char buf[512];
            snprintf(buf, sizeof(buf), "external preprocessor exited with status %d", status);
            *error_message = strdup(buf);
        }
        return NULL;
    }

    if (output) {
        if (output_len + 1 > output_cap) {
            output_cap = output_len + 1;
            output = realloc(output, output_cap);
        }
        output[output_len] = '\0';
    }
    return output;
}
