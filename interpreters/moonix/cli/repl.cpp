#include "repl.h"

#include "kstring.h"
#include "moonix.h"

#include <QaMRpp.hpp>
#include <QaMRpp-Readline.hpp>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#define MOONIX_REPL_PROMPT "> "
#define MOONIX_REPL_CONTINUATION_PROMPT ">> "
#define MOONIX_REPL_EOF_MARKER "<eof>"

/* ── Lua keyword set for syntax highlighting ─────────────────────── */
static const std::set<std::string> lua_keywords = {
  "and",      "break",    "do",       "else",     "elseif",
  "end",      "false",    "for",      "function", "goto",
  "if",       "in",       "local",    "nil",      "not",
  "or",       "repeat",   "return",   "then",     "true",
  "until",    "while",
};

/* ── Lua operators for syntax highlighting ──────────────────────── */
static const std::set<std::string> lua_operators = {
  "+", "-", "*", "/", "//", "%", "^", "#",
  "==", "~=", "<=", ">=", "<", ">", "=",
  "(", ")", "{", "}", "[", "]", ";", ":", ",", ".", "..", "...",
};

/* ── Lua literals ────────────────────────────────────────────────── */
static const std::set<std::string> lua_literals = {
  "true", "false", "nil",
};

/* ── Completions ─────────────────────────────────────────────────── */
static const std::vector<std::string> lua_completions = {
  "and",      "break",    "do",       "else",     "elseif",
  "end",      "false",    "for",      "function", "goto",
  "if",       "in",       "local",    "nil",      "not",
  "or",       "repeat",   "return",   "then",     "true",
  "until",    "while",
  "assert",   "collectgarbage", "dofile",    "error",
  "getmetatable", "ipairs", "load",       "loadfile",
  "next",     "pairs",    "pcall",     "print",    "rawequal",
  "rawget",   "rawlen",   "rawset",    "require",  "select",
  "setmetatable", "tonumber", "tostring", "type",    "xpcall",
  "math",     "string",   "table",     "io",       "os",
  "coroutine","debug",    "_G",        "_VERSION",
  "moonix",
};

/* ── helpers ─────────────────────────────────────────────────────── */

static int
append_line (std::string &source, const char *line)
{
  if (!source.empty ())
    source += '\n';
  source += line;
  return 1;
}

static int
error_is_incomplete (const moonix_state *state)
{
  const char *message = moonix_last_error (state);
  const size_t marker_len = sizeof (MOONIX_REPL_EOF_MARKER) - 1u;
  size_t message_len = strlen (message);

  return message_len >= marker_len
         && strcmp (message + message_len - marker_len,
                    MOONIX_REPL_EOF_MARKER) == 0;
}

static void
warn_if_ephemeral_local (const char *source)
{
  static const char keyword[] = "local";
  const size_t keyword_len = sizeof (keyword) - 1u;

  source += strspn (source, " \t");
  if (strncmp (source, keyword, keyword_len) == 0
      && (source[keyword_len] == ' ' || source[keyword_len] == '\t'))
    {
      fputs ("warning: locals do not survive across lines in "
             "interactive mode\n",
             stderr);
    }
}

static moonix_status
load_expression (moonix_state *state, const char *source,
                 size_t source_len)
{
  static const char prefix[] = "return ";
  kstring_t expression = { 0, 0, NULL };
  moonix_status status;

  if (memchr (source, '=', source_len) != NULL)
    return MOONIX_ERR_SYNTAX;
  if (source_len > (size_t)INT_MAX)
    return MOONIX_ERR_OOM;

  if (kputs (prefix, &expression) == EOF
      || kputsn (source, (int)source_len, &expression) == EOF)
    {
      free (expression.s);
      return MOONIX_ERR_OOM;
    }
  status = moonix_load_buffer (state, expression.s, expression.l,
                               "=stdin");
  free (expression.s);
  return status;
}

static moonix_status
execute_loaded (moonix_state *state)
{
  lua_State *lua = moonix_lua_state (state);
  moonix_status status = moonix_pcall (state, 0, LUA_MULTRET);
  int result_count;

  if (status != MOONIX_OK)
    return status;
  result_count = lua_gettop (lua);
  if (result_count == 0)
    return MOONIX_OK;
  lua_getglobal (lua, "print");
  lua_insert (lua, 1);
  return moonix_pcall (state, result_count, 0);
}

static void
report_repl_error (const moonix_state *state)
{
  fprintf (stderr, "%s\n", moonix_last_error (state));
}

/* ── completion helper ───────────────────────────────────────────── */

static std::vector<std::string>
moonix_completer (const std::string &prefix)
{
  std::vector<std::string> results;
  for (const auto &kw : lua_completions)
    {
      if (kw.size () >= prefix.size ()
          && kw.compare (0, prefix.size (), prefix) == 0)
        results.push_back (kw);
    }
  std::sort (results.begin (), results.end ());
  return results;
}

/* ── REPL loop ───────────────────────────────────────────────────── */

int
moonix_repl (moonix_state *state)
{
  lua_State *lua;

  if (state == NULL)
    return 1;
  lua = moonix_lua_state (state);

  /* Configure the Readline instance with Lua syntax */
  qamrpp::Readline rl;
  {
    qamrpp::Readline::Syntax syn = { lua_keywords, lua_operators, lua_literals };
    rl.set_syntax(syn);
    rl.set_completer(moonix_completer);
  }
  for (;;)
    {
      std::string source;
      int first_line = 1;
      int done = 0;

      lua_settop (lua, 0);
      while (!done)
        {
          const char *prompt = first_line
                                   ? MOONIX_REPL_PROMPT
                                   : MOONIX_REPL_CONTINUATION_PROMPT;
          std::string line;
          bool ok = rl.read_line (prompt, line);
          moonix_status status;

          if (!ok)
            {
              /* Ctrl-D on empty line → exit */
              if (line.empty() && source.empty())
                {
                  lua_settop (lua, 0);
                  return 0;
                }
              /* Ctrl-D with accumulated source → error */
              if (first_line && source.empty())
                {
                  /* EOF at top-level → exit cleanly */
                  lua_settop (lua, 0);
                  done = 1;
                  break;
                }
              fputs ("moonix: unexpected end of input\n", stderr);
              lua_settop (lua, 0);
              return 1;
            }
          /* Ctrl-C clears the line */
          if (line.empty() && source.empty())
            {
              lua_settop (lua, 0);
              continue;
            }
          if (!line.empty ())
            rl.add_history (line);

          if (!append_line (source, line.c_str ()))
            {
              fputs ("moonix: out of memory\n", stderr);
              lua_settop (lua, 0);
              return 1;
            }

          if (first_line)
            {
              status = load_expression (state, source.c_str (),
                                        source.size ());
              if (status == MOONIX_OK)
                {
                  status = execute_loaded (state);
                  if (status != MOONIX_OK)
                    report_repl_error (state);
                  done = 1;
                  continue;
                }
              if (status != MOONIX_ERR_SYNTAX)
                {
                  if (status == MOONIX_ERR_OOM)
                    fputs ("out of memory\n", stderr);
                  else
                    report_repl_error (state);
                  done = 1;
                  continue;
                }
            }

          if (first_line)
            warn_if_ephemeral_local (source.c_str ());
          status = moonix_load_buffer (state, source.c_str (),
                                       source.size (), "=stdin");
          if (status == MOONIX_ERR_SYNTAX
              && error_is_incomplete (state))
            {
              first_line = 0;
              continue;
            }

          if (status == MOONIX_OK)
            status = execute_loaded (state);
          if (status != MOONIX_OK)
            report_repl_error (state);
          done = 1;
        }
    }
}
