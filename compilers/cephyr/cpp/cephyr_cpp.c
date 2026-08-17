/* Cephyr preprocessor — wraps ucpp (§4).
 *
 * ucpp is a C11 implementation of translation phases 1–6. We use it as
 * a library (compiled with STAND_ALONE not defined) to produce a
 * preprocessed token stream. A line map is built so diagnostics always
 * report original source locations. */

#define _POSIX_C_SOURCE 200809L

#include "cephyr_cpp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kstring.h"
#include "kvec.h"

/* ucpp internal headers. We compile ucpp with STAND_ALONE undefined. */
#include "../../../third_party/ucpp/mem.h"
#include "../../../third_party/ucpp/cpp.h"
#include "../../../third_party/ucpp/tune.h"
#include "../../../third_party/ucpp/ucppi.h"

/* ---------- line-map builder ---------- */

typedef struct {
    kvec_t(cephyr_line_map_entry) entries;
    kvec_t(char *)          filenames;
    int                     current_line;  /* 1-based, in output */
    int                     source_line;   /* 1-based, in current source file */
    int                     source_idx;    /* index into filenames[] */
} line_map_builder;

static char *cephyr_cpp_strdup(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s) + 1u;
    char *copy = malloc(n);
    if (copy) memcpy(copy, s, n);
    return copy;
}

static void lmb_init(line_map_builder *lmb)
{
    kv_init(lmb->entries);
    kv_init(lmb->filenames);
    lmb->current_line = 1;
    lmb->source_line = 1;
    lmb->source_idx = -1;
}

static void lmb_add_file(line_map_builder *lmb, const char *filename)
{
    kv_push(char *, lmb->filenames, cephyr_cpp_strdup(filename));
    lmb->source_idx = (int)kv_size(lmb->filenames) - 1;
}

static void lmb_add_entry(line_map_builder *lmb, int output_line,
                          int source_line, int source_idx)
{
    cephyr_line_map_entry entry = {
        .source_file = cephyr_cpp_strdup(
            (source_idx >= 0) ? lmb->filenames.a[source_idx] : "<builtin>"),
        .source_line = source_line,
        .source_column = 1,
        .output_line = output_line
    };
    kv_push(cephyr_line_map_entry, lmb->entries, entry);
}

static void lmb_free(line_map_builder *lmb)
{
    for (size_t i = 0; i < kv_size(lmb->filenames); i++)
        free(kv_A(lmb->filenames, i));
    kv_destroy(lmb->filenames);
    kv_destroy(lmb->entries);
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
    char **ucpp_include_paths = NULL;
    if (include_path_count > 0) {
        ucpp_include_paths = calloc((size_t)include_path_count + 1u,
                                    sizeof(*ucpp_include_paths));
        if (!ucpp_include_paths) {
            result.error_message = cephyr_cpp_strdup("out of memory");
            return result;
        }
        for (int i = 0; i < include_path_count; i++)
            ucpp_include_paths[i] = (char *)include_paths[i];
    }
    init_include_path(ucpp_include_paths);
    free(ucpp_include_paths);

    /* Step 5: dependencies */
    emit_dependencies = 0;

    /* Step 6: set the source filename */
    set_init_filename((char *)(source_name ? source_name : "<input>"), 0);

    /* Step 7: initialise lexer */
    struct lexer_state ls;
    init_lexer_state(&ls);
    init_lexer_mode(&ls);
    ls.flags |= HANDLE_ASSERTIONS | HANDLE_PRAGMA | LINE_NUM | KEEP_OUTPUT;

    /* Step 8: feed source text through a temporary file */
    FILE *tmpf = tmpfile();
    if (!tmpf) {
        result.error_message = cephyr_cpp_strdup(
            "failed to create temporary file for preprocessor");
        return result;
    }
    fwrite(source_text, 1, source_len, tmpf);
    rewind(tmpf);
    ls.input = tmpf;

    /* Step 9: collect output */
    kstring_t output = { 0, 0, NULL };
    line_map_builder lmb;
    lmb_init(&lmb);
    lmb_add_file(&lmb, source_name ? source_name : "<input>");
    enter_file(&ls, ls.flags);

    /* Step 10: tokenize */
    int tok;
    int status;
    int add_newline = 0;

    while ((status = lex(&ls)) < CPPERR_EOF) {
        if (status >= CPPERR) continue;
        tok = ls.ctok->type;
        if (tok == NEWLINE) {
            lmb.current_line++;
            if (add_newline) {
                (void)kputc('\n', &output);
            }
            add_newline = 0;
        } else if (tok == CONTEXT) {
            /* #line directive or file change. */
            lmb_add_file(&lmb, ls.ctok->name ? ls.ctok->name : "<builtin>");
            lmb.source_line = (int)ls.ctok->line;
            add_newline = 0;
        } else if (tok == COMMENT) {
            /* Replace comments with a single space */
            if (output.l > 0 && output.s[output.l - 1] != ' ' &&
                output.s[output.l - 1] != '\n') {
                (void)kputc(' ', &output);
            }
        } else if (tok == NONE) {
            /* Whitespace — collapse to single space */
            if (output.l > 0 && output.s[output.l - 1] != ' ' &&
                output.s[output.l - 1] != '\n') {
                (void)kputc(' ', &output);
            }
        } else {
            /* Emit the token text */
            const char *token_text = token_name(ls.ctok);
            if (token_text) {
                if (output.l == 0 || output.s[output.l - 1] == '\n')
                    lmb_add_entry(&lmb, lmb.current_line, (int)ls.ctok->line,
                                  lmb.source_idx);
                size_t tlen = strlen(token_text);
                if (output.l > 0 && output.s[output.l - 1] != ' ' &&
                    output.s[output.l - 1] != '\n' &&
                    output.s[output.l - 1] != '(' &&
                    output.s[output.l - 1] != '[' &&
                    output.s[output.l - 1] != '{' &&
                    *token_text != ')' && *token_text != ']' &&
                    *token_text != '}' && *token_text != ',' &&
                    *token_text != ';') {
                    (void)kputc(' ', &output);
                }
                (void)kputsn(token_text, (int)tlen, &output);
            }
            add_newline = 1;
        }
    }

    /* Add final newline */
    if (output.l > 0 && output.s[output.l - 1] != '\n') {
        (void)kputc('\n', &output);
    }

    /* kstring maintains a trailing NUL; make an empty result NUL-terminated. */
    if (output.s == NULL) {
        (void)ks_resize(&output, 1);
        if (output.s) output.s[0] = '\0';
    }

    /* Cleanup */
    free_lexer_state(&ls);
    wipeout();

    /* Build result */
    result.text_len = output.l;
    result.text = ks_release(&output);
    result.line_map = lmb.entries.a;
    result.line_map_count = (int)lmb.entries.n;
    lmb.entries.a = NULL; /* ownership transferred */
    lmb.entries.n = lmb.entries.m = 0;
    lmb_free(&lmb);

    return result;
}

void cephyr_cpp_result_free(cephyr_cpp_result *res)
{
    if (!res) return;
    free(res->text);
    for (int i = 0; i < res->line_map_count; i++)
        free((char *)res->line_map[i].source_file);
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
        if (error_message)
            *error_message = cephyr_cpp_strdup("no external preprocessor command set");
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
            *error_message = cephyr_cpp_strdup(buf);
        }
        return NULL;
    }

    /* Read all output */
    kstring_t output = { 0, 0, NULL };
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), pipe)) > 0) {
        if (kputsn(buf, (int)n, &output) == EOF) {
            free(output.s);
            (void)pclose(pipe);
            if (error_message)
                *error_message = cephyr_cpp_strdup("out of memory");
            return NULL;
        }
    }

    int status = pclose(pipe);
    if (status != 0) {
        free(output.s);
        if (error_message) {
            char buf[512];
            snprintf(buf, sizeof(buf), "external preprocessor exited with status %d", status);
            *error_message = cephyr_cpp_strdup(buf);
        }
        return NULL;
    }

    return ks_release(&output);
}
