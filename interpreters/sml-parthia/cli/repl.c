#include "repl.h"

#include "linenoise.h"
#include "kstring.h"
#include "sml_parthia.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SML_PARTHIA_REPL_PROMPT "- "
#define SML_PARTHIA_REPL_CONTINUATION_PROMPT "= "

static int append_line(char **source, size_t *source_len, const char *line)
{
    kstring_t text = { *source_len, *source_len, *source };
    if (*source_len != 0 && kputc('\n', &text) == EOF) return 0;
    if (kputs(line, &text) == EOF) return 0;
    *source_len = text.l;
    *source = ks_release(&text);
    return 1;
}

static int has_non_whitespace(const char *source, size_t length)
{
    size_t i;
    for (i = 0; i < length; ++i)
        if (source[i] != ' ' && source[i] != '\t' &&
            source[i] != '\r' && source[i] != '\n')
            return 1;
    return 0;
}

static int run_phrase(const char *source, size_t length)
{
    ccw_sml_parthia_report report;
    ccw_sml_parthia_program *program;
    char *error = NULL;
    char *history;

    if (!has_non_whitespace(source, length)) return 1;
    program = ccw_sml_parthia_compile(source, length, &report, &error);
    if (program == NULL) {
        fprintf(stderr, "sml-parthia: %s\n", error ? error : "compile failed");
        free(error);
        return 0;
    }

    history = (char *)malloc(length + 1u);
    if (history != NULL) {
        memcpy(history, source, length);
        history[length] = '\0';
        linenoiseHistoryAdd(history);
        free(history);
    }
    fputs(ccw_sml_parthia_core_ast(program), stdout);
    fputc('\n', stdout);
    ccw_sml_parthia_program_destroy(program);
    free(error);
    return 1;
}

/*
 * Consume every complete phrase in source.  The first two consecutive
 * semicolons are the SML phrase terminator; any suffix remains buffered for
 * the next prompt.
 */
static int process_phrases(char **source, size_t *source_len)
{
    int status = 1;
    for (;;) {
        char *terminator;
        size_t phrase_length;
        size_t consumed;

        if (*source == NULL || *source_len < 2u) break;
        terminator = strstr(*source, ";;");
        if (terminator == NULL) break;
        phrase_length = (size_t)(terminator - *source);
        if (!run_phrase(*source, phrase_length)) status = 0;

        consumed = phrase_length + 2u;
        memmove(*source, *source + consumed, *source_len - consumed);
        *source_len -= consumed;
        (*source)[*source_len] = '\0';
    }
    return status;
}

int sml_parthia_repl(void)
{
    char *source = NULL;
    size_t source_len = 0;
    int status = 0;

    linenoiseInstallWindowChangeHandler();
    for (;;) {
        const char *prompt = source_len == 0
                                 ? SML_PARTHIA_REPL_PROMPT
                                 : SML_PARTHIA_REPL_CONTINUATION_PROMPT;
        char *line = linenoise(prompt);

        if (line == NULL) {
            if (linenoiseKeyType() == 1) {
                free(source);
                fputc('\n', stdout);
                source = NULL;
                source_len = 0;
                continue;
            }
            if (source_len != 0) {
                fputs("sml-parthia: unexpected end of input; expected ;;\n",
                      stderr);
                status = 1;
            }
            free(source);
            linenoiseHistoryFree();
            return status;
        }

        if (!append_line(&source, &source_len, line)) {
            free(line);
            free(source);
            linenoiseHistoryFree();
            fputs("sml-parthia: out of memory\n", stderr);
            return 1;
        }
        free(line);
        if (!process_phrases(&source, &source_len)) status = 1;
    }
}
