#include "repl.h"
#include "sml_parthia.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *
read_stream (FILE *stream, size_t *length)
{
  size_t used = 0;
  size_t capacity = 4096;
  char *buffer = (char *)malloc (capacity + 1u);
  if (buffer == NULL)
    return NULL;
  for (;;)
    {
      size_t got = fread (buffer + used, 1, capacity - used, stream);
      used += got;
      if (got == 0)
        {
          if (ferror (stream))
            {
              free (buffer);
              return NULL;
            }
          break;
        }
      if (used == capacity)
        {
          char *grown;
          capacity *= 2u;
          grown = (char *)realloc (buffer, capacity + 1u);
          if (grown == NULL)
            {
              free (buffer);
              return NULL;
            }
          buffer = grown;
        }
    }
  buffer[used] = '\0';
  *length = used;
  return buffer;
}

static void
print_help (FILE *stream)
{
  fputs (
      "Usage: sml-parthia [OPTIONS] [FILE.sml]\n"
      "\n"
      "Run an SML '97 source file with the Parthia AoT runtime.\n"
      "With no file, start the interactive REPL.\n"
      "\n"
      "Options:\n"
      "  -h, --help  Show this help text and exit.\n"
      "\n"
      "REPL directives:\n"
      "  #help              List REPL directives.\n"
      "  #open MODULE       Show a module's structure/signatures.\n"
      "  #use \"FILE\"        Load and compile SML source.\n"
      "  #load LIB          Load a native library from SML_PARTHIA_PATH.\n"
      "  #quit              Leave the REPL.\n"
      "\n"
      "Source and library directive targets (use, load, CM.make) resolve\n"
      "as given first, then through each directory in the comma-separated\n"
      "SML_PARTHIA_PATH.\n",
      stream);
}

int
main (int argc, char **argv)
{
  FILE *input = stdin;
  char *source;
  size_t length;
  char *error = NULL;
  ccw_sml_parthia_runtime *runtime;
  char *result = NULL;

  if (argc == 2
      && (strcmp (argv[1], "-h") == 0 || strcmp (argv[1], "--help") == 0))
    {
      print_help (stdout);
      return 0;
    }
  if (argc > 2)
    {
      fprintf (stderr, "Try 'sml-parthia --help' for usage.\n");
      return 2;
    }
  if (argc == 1)
    return sml_parthia_repl ();

  if (argc == 2)
    {
      input = fopen (argv[1], "rb");
      if (input == NULL)
        {
          fprintf (stderr, "sml-parthia: cannot open %s\n", argv[1]);
          return 1;
        }
    }
  source = read_stream (input, &length);
  if (argc == 2)
    fclose (input);
  if (source == NULL)
    {
      fprintf (stderr, "sml-parthia: cannot read input\n");
      return 1;
    }
  runtime = ccw_sml_parthia_runtime_new ();
  if (runtime == NULL)
    {
      free (source);
      fprintf (stderr, "sml-parthia: cannot create runtime\n");
      return 1;
    }
  if (!ccw_sml_parthia_run (runtime, source, length, &result, &error))
    {
      free (source);
      ccw_sml_parthia_runtime_free (runtime);
      fprintf (stderr, "sml-parthia: %s\n", error ? error : "execution failed");
      free (error);
      return 1;
    }
  free (source);
  free (result);
  ccw_sml_parthia_runtime_free (runtime);
  free (error);
  return 0;
}
