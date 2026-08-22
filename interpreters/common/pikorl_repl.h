/* PikoRL REPL — C-compatible wrapper around QaMRpp's Readline.
 * § — PikoRL integration for CCWeave interpreters.
 *
 * The interface mirrors linenoise to minimise churn in existing REPL code.
 */
#ifndef CCW_PIKORL_REPL_H
#define CCW_PIKORL_REPL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle to the PikoRL REPL state. */
typedef struct pikorl_repl pikorl_repl;

/* Completion callback — receives the current prefix, returns count of
 * matches written into the pre-allocated `out` array (max `max_out`). */
typedef int (*pikorl_completion_fn)(const char *prefix, char **out,
                                    int max_out);

/* Create a new PikoRL REPL.
 *
 * `bundle_path` — path to the PikoRL bundle directory (MANIFEST.json + Lua
 *                 DSLs).  Pass NULL or "" to skip bundle loading.
 *
 * Returns NULL on failure. */
pikorl_repl *pikorl_repl_create(const char *bundle_path);

/* Read a line with the given prompt.
 *
 * Returns a malloc'd string that the caller must free, or NULL on EOF
 * (Ctrl‑D on an empty line).  The returned string does *not* include the
 * trailing newline. */
char *pikorl_repl_readline(pikorl_repl *repl, const char *prompt);

/* Add a line to the in-memory history. */
void pikorl_repl_add_history(pikorl_repl *repl, const char *line);

/* Set the tab-completion callback.  Pass NULL to disable. */
void pikorl_repl_set_completer(pikorl_repl *repl, pikorl_completion_fn fn);

/* Configure syntax highlighting.
 *
 * `keywords` — NULL-terminated array of keyword strings.
 * `literals` — NULL-terminated array of literal strings (true, false, nil).
 * `use_color` — 1 to enable ANSI colour, 0 to disable. */
void pikorl_repl_set_syntax(pikorl_repl *repl, const char **keywords,
                            const char **literals, int use_color);

/* Returns 1 if the last readline was interrupted by Ctrl‑C, 0 otherwise. */
int pikorl_repl_ctrl_c_pressed(pikorl_repl *repl);

/* Save history to a file (appends).  Returns 0 on success. */
int pikorl_repl_save_history(pikorl_repl *repl, const char *path);

/* Load history from a file.  Returns 0 on success. */
int pikorl_repl_load_history(pikorl_repl *repl, const char *path);

/* Destroy the REPL and free all associated resources. */
void pikorl_repl_destroy(pikorl_repl *repl);

#ifdef __cplusplus
}
#endif

#endif /* CCW_PIKORL_REPL_H */
