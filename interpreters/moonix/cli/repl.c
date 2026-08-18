#include "repl.h"

#include "linenoise.h"
#include "kstring.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MOONIX_REPL_PROMPT "> "
#define MOONIX_REPL_CONTINUATION_PROMPT ">> "
#define MOONIX_REPL_EOF_MARKER "<eof>"

static int append_line(char **source, size_t *source_len, const char *line)
{
    kstring_t text = { *source_len, *source_len, *source };
    if (*source_len != 0 && kputc('\n', &text) == EOF) return 0;
    if (kputs(line, &text) == EOF) return 0;
    *source_len = text.l;
    *source = ks_release(&text);
    return 1;
}

static int error_is_incomplete(const moonix_state *state)
{
    const char *message = moonix_last_error(state);
    const size_t marker_len = sizeof(MOONIX_REPL_EOF_MARKER) - 1u;
    size_t message_len = strlen(message);

    return message_len >= marker_len &&
           strcmp(message + message_len - marker_len,
                  MOONIX_REPL_EOF_MARKER) == 0;
}

static void warn_if_ephemeral_local(const char *source)
{
    static const char keyword[] = "local";
    const size_t keyword_len = sizeof(keyword) - 1u;

    source += strspn(source, " \t");
    if (strncmp(source, keyword, keyword_len) == 0 &&
        (source[keyword_len] == ' ' || source[keyword_len] == '\t')) {
        fputs("warning: locals do not survive across lines in interactive mode\n",
              stderr);
    }
}

static moonix_status load_expression(moonix_state *state,
                                     const char *source,
                                     size_t source_len)
{
    static const char prefix[] = "return ";
    kstring_t expression = { 0, 0, NULL };
    moonix_status status;

    if (memchr(source, '=', source_len) != NULL)
        return MOONIX_ERR_SYNTAX;
    if (source_len > (size_t)INT_MAX)
        return MOONIX_ERR_OOM;

    if (kputs(prefix, &expression) == EOF ||
        kputsn(source, (int)source_len, &expression) == EOF) {
        free(expression.s);
        return MOONIX_ERR_OOM;
    }
    status = moonix_load_buffer(state, expression.s, expression.l,
                                "=stdin");
    free(expression.s);
    return status;
}

static moonix_status execute_loaded(moonix_state *state)
{
    lua_State *lua = moonix_lua_state(state);
    moonix_status status = moonix_pcall(state, 0, LUA_MULTRET);
    int result_count;

    if (status != MOONIX_OK) return status;
    result_count = lua_gettop(lua);
    if (result_count == 0) return MOONIX_OK;
    lua_getglobal(lua, "print");
    lua_insert(lua, 1);
    return moonix_pcall(state, result_count, 0);
}

static void report_repl_error(const moonix_state *state)
{
    fprintf(stderr, "%s\n", moonix_last_error(state));
}

int moonix_repl(moonix_state *state)
{
    lua_State *lua;

    if (state == NULL) return 1;
    lua = moonix_lua_state(state);
    linenoiseInstallWindowChangeHandler();

    for (;;) {
        char *source = NULL;
        size_t source_len = 0;
        int first_line = 1;
        int done = 0;

        lua_settop(lua, 0);
        while (!done) {
            const char *prompt = first_line
                                     ? MOONIX_REPL_PROMPT
                                     : MOONIX_REPL_CONTINUATION_PROMPT;
            char *line = linenoise(prompt);
            moonix_status status;

            if (line == NULL) {
                free(source);
                if (linenoiseKeyType() == 1) {
                    lua_settop(lua, 0);
                    done = 1;
                    break;
                }
                linenoiseHistoryFree();
                lua_settop(lua, 0);
                return 0;
            }
            if (!append_line(&source, &source_len, line)) {
                free(line);
                free(source);
                fputs("moonix: out of memory\n", stderr);
                linenoiseHistoryFree();
                lua_settop(lua, 0);
                return 1;
            }
            free(line);

            if (first_line) {
                status = load_expression(state, source, source_len);
                if (status == MOONIX_OK) {
                    linenoiseHistoryAdd(source);
                    status = execute_loaded(state);
                    if (status != MOONIX_OK) report_repl_error(state);
                    done = 1;
                    continue;
                }
                if (status != MOONIX_ERR_SYNTAX) {
                    linenoiseHistoryAdd(source);
                    if (status == MOONIX_ERR_OOM)
                        fputs("out of memory\n", stderr);
                    else
                        report_repl_error(state);
                    done = 1;
                    continue;
                }
            }

            if (first_line)
                warn_if_ephemeral_local(source);
            status = moonix_load_buffer(state, source, source_len, "=stdin");
            if (status == MOONIX_ERR_SYNTAX && error_is_incomplete(state)) {
                first_line = 0;
                continue;
            }

            linenoiseHistoryAdd(source);
            if (status == MOONIX_OK)
                status = execute_loaded(state);
            if (status != MOONIX_OK) report_repl_error(state);
            done = 1;
        }
        free(source);
    }
}
