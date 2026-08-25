#include "ccw-ccwmk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
usage (FILE *out)
{
  fputs ("usage: ccwmk [Weavefile]\n", out);
}

int
main (int argc, char **argv)
{
  const char *path = "Weavefile";
  if (argc > 1)
    {
      if (!strcmp (argv[1], "-h") || !strcmp (argv[1], "--help"))
        {
          usage (stdout);
          return 0;
        }
      if (!strcmp (argv[1], "--version"))
        {
          printf ("ccwmk %s\n", CCWMK_VERSION);
          return 0;
        }
      path = argv[1];
    }

  ccwmk_graph_t *graph = NULL;
  char *error_message = NULL;
  if (!ccwmk_load (path, &graph, &error_message))
    {
      fprintf (stderr, "ccwmk: %s\n",
               error_message ? error_message : "load failed");
      free (error_message);
      ccwmk_graph_free (graph);
      return 2;
    }

  int ok = ccwmk_build (graph, &error_message);
  if (!ok)
    {
      fprintf (stderr, "ccwmk: %s\n",
               error_message ? error_message : "build failed");
      free (error_message);
      ccwmk_graph_free (graph);
      return 2;
    }

  ccwmk_graph_free (graph);
  return 0;
}

