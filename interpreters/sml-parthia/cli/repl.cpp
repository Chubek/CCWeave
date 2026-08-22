#include "repl.h"

#include "kstring.h"
#include "sml_parthia.h"

#include <QaMRpp.hpp>
#include <QaMRpp-Readline.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#define SML_PARTHIA_REPL_PROMPT "- "
#define SML_PARTHIA_REPL_CONTINUATION_PROMPT "= "

/* ── SML keyword set for syntax highlighting ─────────────────────── */
static const std::set<std::string> sml_keywords = {
  "abstype", "and",     "andalso", "as",      "case",
  "datatype","do",      "else",    "end",     "eqtype",
  "exception","fn",     "fun",     "functor", "handle",
  "if",      "in",      "include", "infix",   "infixr",
  "let",     "local",   "nonfix",  "of",      "op",
  "open",    "orelse",  "raise",   "rec",     "sharing",
  "sig",     "signature","struct", "structure","then",
  "type",    "val",     "where",   "while",   "with",
  "withtype",
};

static const std::set<std::string> sml_operators = {
  "+", "-", "*", "/", "div", "mod", "=", "<>", "<", ">",
  "<=", ">=", ":=", "::", "@", "^", "::", "=>", "->",
  "(", ")", "{", "}", "[", "]", ";", ",", ".", ":", "|",
  "!", "before", "o",
};

static const std::set<std::string> sml_literals = {
  "true", "false", "nil", "NONE", "SOME", "ref",
};

/* ── Completions ─────────────────────────────────────────────────── */
static const std::vector<std::string> sml_completions = {
  "abstype", "and",     "andalso", "as",      "case",
  "datatype","do",      "else",    "end",     "eqtype",
  "exception","fn",     "fun",     "functor", "handle",
  "if",      "in",      "include", "infix",   "infixr",
  "let",     "local",   "nonfix",  "of",      "op",
  "open",    "orelse",  "raise",   "rec",     "sharing",
  "sig",     "signature","struct", "structure","then",
  "type",    "val",     "where",   "while",   "with",
  "withtype",
  "int", "real", "string", "char", "bool", "unit",
  "list", "option", "vector", "array", "ref", "exn", "word",
  "print", "map", "foldl", "foldr", "app", "rev", "length",
  "hd", "tl", "null", "implode", "explode", "chr", "ord",
  "str", "size", "substring", "concat",
  "Int", "Real", "String", "Char", "Bool", "List", "Option",
  "Vector", "Array",
};

/* ── helpers ─────────────────────────────────────────────────────── */

static int
has_non_whitespace (const std::string &source)
{
  for (char c : source)
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
      return 1;
  return 0;
}

static int
run_phrase (const std::string &source,
            ccw_sml_parthia_runtime *runtime)
{
  ccw_sml_parthia_report report;
  ccw_sml_parthia_program *program;
  char *error = NULL;

  if (source.empty () || !has_non_whitespace (source))
    return 1;

  program = ccw_sml_parthia_compile_with_runtime (
      runtime, source.c_str (), source.size (), &report, &error);
  if (program == NULL)
    {
      fprintf (stderr, "sml-parthia: %s\n",
               error ? error : "compile failed");
      free (error);
      return 0;
    }

  fputs (ccw_sml_parthia_core_ast (program), stdout);
  fputc ('\n', stdout);
  ccw_sml_parthia_program_destroy (program);
  free (error);
  return 1;
}

static int
print_module_signatures (const std::string &session,
                         const std::string &module)
{
  if (session.empty () || module.empty ())
    return 0;

  size_t pos = 0;
  int found = 0;
  while (pos < session.size ())
    {
      size_t line_end = session.find ('\n', pos);
      if (line_end == std::string::npos)
        line_end = session.size ();

      std::string line = session.substr (pos, line_end - pos);
      size_t name_start = 0;
      while (name_start < line.size ()
             && (line[name_start] == ' ' || line[name_start] == '\t'))
        name_start++;

      const char *prefix = nullptr;
      if (line.compare (name_start, 10, "structure ") == 0)
        prefix = "structure ";
      else if (line.compare (name_start, 10, "signature ") == 0)
        prefix = "signature ";

      if (!prefix)
        {
          pos = line_end + 1;
          continue;
        }

      std::string name
          = line.substr (name_start + 10,
                         line.size () - name_start - 10);
      /* trim trailing whitespace / parens */
      while (!name.empty ()
             && (name.back () == ' ' || name.back () == '\t'
                 || name.back () == ')'))
        name.pop_back ();

      if (name == module)
        {
          fwrite (session.c_str () + pos, 1, line_end - pos, stdout);
          fputc ('\n', stdout);
          found = 1;
        }
      pos = line_end + 1;
    }
  if (!found)
    fprintf (stderr, "sml-parthia: module %s is not open\n",
             module.c_str ());
  return found;
}

static int
handle_directive (const std::string &line,
                  ccw_sml_parthia_runtime *runtime,
                  std::string &session)
{
  char command[32] = { 0 };
  char argument[1024] = { 0 };
  const char *p = line.c_str ();
  int consumed;

  while (isspace ((unsigned char)*p))
    p++;
  consumed = sscanf (p, "#%31s %1023[^\n]", command, argument);
  if (consumed < 1)
    return 0;

  if (strcmp (command, "help") == 0)
    {
      puts ("#help              list directives");
      puts ("#open MODULE       show a module's structure/signatures");
      puts ("#use \"FILE\"        load and compile SML source");
      puts ("#load LIB          load a native library from SML_PARTHIA_PATH");
      puts ("#quit              leave the REPL");
      puts ("targets resolve as given, then through comma-separated");
      puts ("SML_PARTHIA_PATH directories");
      return 1;
    }
  if (strcmp (command, "quit") == 0 || strcmp (command, "q") == 0)
    return -1;

  if (strcmp (command, "open") == 0)
    {
      char module[256];
      if (sscanf (argument, "%255s", module) != 1)
        {
          fputs ("sml-parthia: #open expects a module name\n", stderr);
          return 1;
        }
      print_module_signatures (session, module);
      return 1;
    }

  if (strcmp (command, "load") == 0)
    {
      char name[1024];
      char *path;
      if (sscanf (argument, " \"%1023[^\"]\"", name) != 1
          && sscanf (argument, "%1023s", name) != 1)
        {
          fputs ("sml-parthia: #load expects a library name\n", stderr);
          return 1;
        }
      path = ccw_sml_parthia_resolve_path (name);
      if (!path)
        {
          fprintf (stderr,
                   "sml-parthia: %s not found in SML_PARTHIA_PATH\n",
                   name);
          return 1;
        }
      if (!ccw_sml_parthia_load_native (runtime, path))
        {
          fprintf (stderr, "sml-parthia: cannot load %s\n", path);
          free (path);
          return 1;
        }
      free (path);
      return 1;
    }

  if (strcmp (command, "use") == 0)
    {
      char name[1024];
      ccw_sml_parthia_program *program;
      ccw_sml_parthia_report report;
      char *error = NULL;
      char *path;
      if (sscanf (argument, " \"%1023[^\"]\"", name) != 1)
        {
          fputs ("sml-parthia: #use expects a quoted source path\n",
                 stderr);
          return 1;
        }
      path = ccw_sml_parthia_resolve_path (name);
      if (!path)
        {
          fprintf (stderr,
                   "sml-parthia: %s not found in SML_PARTHIA_PATH\n",
                   name);
          return 1;
        }
      program = ccw_sml_parthia_compile_file (runtime, path, &report,
                                              &error);
      if (!program)
        {
          fprintf (stderr, "sml-parthia: %s\n",
                   error ? error : "source load failed");
          free (error);
          free (path);
          return 1;
        }
      if (session.empty ())
        {
          const char *ast = ccw_sml_parthia_surface_ast (program);
          if (ast)
            session = ast;
        }
      fputs (ccw_sml_parthia_core_ast (program), stdout);
      fputc ('\n', stdout);
      ccw_sml_parthia_program_destroy (program);
      free (error);
      free (path);
      return 1;
    }

  fprintf (stderr, "sml-parthia: unknown directive #%s (try #help)\n",
           command);
  return 1;
}

/*
 * Consume every complete phrase in source.  The first two consecutive
 * semicolons are the SML phrase terminator; any suffix remains buffered for
 * the next prompt.
 */
static int
process_phrases (std::string &source,
                 ccw_sml_parthia_runtime *runtime,
                 std::string &session)
{
  int status = 1;
  for (;;)
    {
      size_t pos = source.find (";;");
      if (pos == std::string::npos)
        break;

      std::string phrase = source.substr (0, pos);
      if (!run_phrase (phrase, runtime))
        status = 0;
      else
        session += phrase + "\n";

      source.erase (0, pos + 2);
    }
  return status;
}

/* ── completion helper ───────────────────────────────────────────── */

static std::vector<std::string>
sml_parthia_completer (const std::string &prefix)
{
  std::vector<std::string> results;
  for (const auto &kw : sml_completions)
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
sml_parthia_repl (void)
{
  std::string source;
  std::string session;
  int status = 0;
  ccw_sml_parthia_runtime *runtime = ccw_sml_parthia_runtime_new ();

  if (!runtime)
    {
      fputs ("sml-parthia: cannot initialize REPL runtime\n", stderr);
      return 1;
    }

  /* Configure the Readline instance with SML syntax */
  qamrpp::Readline rl;
  {
    qamrpp::Readline::Syntax syn
        = { sml_keywords, sml_operators, sml_literals };
    rl.set_syntax (syn);
    rl.set_completer (sml_parthia_completer);
  }

  for (;;)
    {
      const char *prompt = source.empty ()
                               ? SML_PARTHIA_REPL_PROMPT
                               : SML_PARTHIA_REPL_CONTINUATION_PROMPT;
      std::string line;
      bool ok = rl.read_line (prompt, line);

      if (!ok)
        {
          if (line.empty () && source.empty ())
            {
              /* Ctrl-D on empty line → exit */
              ccw_sml_parthia_runtime_free (runtime);
              return status;
            }
          if (!source.empty ())
            {
              fputs ("sml-parthia: unexpected end of input; "
                     "expected ;;\n",
                     stderr);
              status = 1;
            }
          ccw_sml_parthia_runtime_free (runtime);
          return status;
        }

      /* Ctrl-C clears the line */
      if (line.empty () && source.empty ())
        {
          continue;
        }

      if (!line.empty ())
        rl.add_history (line);

      if (source.empty () && !line.empty () && line[0] == '#')
        {
          int directive_status
              = handle_directive (line, runtime, session);
          if (directive_status < 0)
            {
              ccw_sml_parthia_runtime_free (runtime);
              return status;
            }
          if (!directive_status)
            status = 1;
          continue;
        }

      if (!source.empty ())
        source += '\n';
      source += line;

      if (!process_phrases (source, runtime, session))
        status = 1;
    }
}
