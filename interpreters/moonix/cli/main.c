#include "kstring.h"
#include "moonix.h"
#include "repl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
usage (FILE *stream)
{
  fputs ("usage: moonix [--tier=t0|t1|t2] [-e chunk] [script]\n"
         "       moonix --dump-bytecode script output\n"
         "       moonix -v\n",
         stream);
}

static int
parse_tier (const char *value, moonix_tier *tier)
{
  if (strcmp (value, "t0") == 0)
    *tier = MOONIX_TIER_T0;
  else if (strcmp (value, "t1") == 0)
    *tier = MOONIX_TIER_T1;
  else if (strcmp (value, "t2") == 0)
    *tier = MOONIX_TIER_T2;
  else
    return 0;
  return 1;
}

static int
report (moonix_state *state, moonix_status status)
{
  if (status == MOONIX_OK)
    return 0;
  fprintf (stderr, "moonix: %s: %s\n", moonix_status_string (status),
           moonix_last_error (state));
  return 1;
}

static int
dump_bytecode (moonix_state *state, const char *input, const char *output)
{
  FILE *file;
  char buffer[4096];
  kstring_t source = { 0, 0, NULL };
  moonix_chunk chunk;
  moonix_status status;
  file = fopen (input, "rb");
  if (file == NULL)
    {
      perror (input);
      return 1;
    }
  while (!feof (file))
    {
      size_t n = fread (buffer, 1, sizeof (buffer), file);
      if (n != 0 && kputsn (buffer, (int)n, &source) == EOF)
        {
          free (source.s);
          fclose (file);
          fprintf (stderr, "moonix: cannot read %s\n", input);
          return 1;
        }
      if (ferror (file))
        {
          free (source.s);
          fclose (file);
          fprintf (stderr, "moonix: cannot read %s\n", input);
          return 1;
        }
    }
  fclose (file);
  status = moonix_compile (state, source.s, source.l, input, &chunk);
  free (source.s);
  if (status != MOONIX_OK)
    return report (state, status);
  file = fopen (output, "wb");
  if (file == NULL)
    {
      moonix_chunk_clear (&chunk);
      fprintf (stderr, "moonix: cannot write %s\n", output);
      return 1;
    }
  if (fwrite (chunk.data, 1, chunk.size, file) != chunk.size)
    {
      fclose (file);
      moonix_chunk_clear (&chunk);
      fprintf (stderr, "moonix: cannot write %s\n", output);
      return 1;
    }
  if (fclose (file) != 0)
    {
      moonix_chunk_clear (&chunk);
      fprintf (stderr, "moonix: cannot write %s\n", output);
      return 1;
    }
  moonix_chunk_clear (&chunk);
  return 0;
}

int
main (int argc, char **argv)
{
  moonix_options options;
  moonix_state *state;
  const char *expression = NULL;
  const char *script = NULL;
  const char *dump_input = NULL;
  const char *dump_output = NULL;

  moonix_options_init (&options);
  for (int i = 1; i < argc; ++i)
    {
      if (strcmp (argv[i], "-v") == 0 || strcmp (argv[i], "--version") == 0)
        {
          printf ("Moonix %d.%d (Lua %s)\n", MOONIX_VERSION_MAJOR,
                  MOONIX_VERSION_MINOR, LUA_VERSION_RELEASE);
          return 0;
        }
      if (strncmp (argv[i], "--tier=", 7) == 0)
        {
          if (!parse_tier (argv[i] + 7, &options.tier))
            {
              usage (stderr);
              return 2;
            }
        }
      else if (strcmp (argv[i], "-e") == 0 && i + 1 < argc)
        {
          expression = argv[++i];
        }
      else if (strcmp (argv[i], "--dump-bytecode") == 0 && i + 2 < argc)
        {
          dump_input = argv[++i];
          dump_output = argv[++i];
        }
      else if (argv[i][0] == '-')
        {
          usage (stderr);
          return 2;
        }
      else if (script == NULL)
        {
          script = argv[i];
        }
      else
        {
          usage (stderr);
          return 2;
        }
    }

  state = moonix_newstate (&options);
  if (state == NULL)
    {
      fputs ("moonix: could not initialize runtime\n", stderr);
      return 1;
    }
  int result;
  if (dump_input != NULL)
    result = dump_bytecode (state, dump_input, dump_output);
  else if (expression != NULL)
    result = report (state, moonix_dostring (state, expression, "=(command)"));
  else if (script != NULL)
    result = report (state, moonix_dofile (state, script));
  else
    result = moonix_repl (state);
  moonix_close (state);
  return result;
}
