#include "../ccw-ccwmk.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *
ccwmk_strdup (const char *s)
{
  if (!s)
    return NULL;
  size_t n = strlen (s) + 1;
  char *copy = (char *)malloc (n);
  if (copy)
    memcpy (copy, s, n);
  return copy;
}

static void
set_error (char **error_message, const char *message)
{
  if (!error_message)
    return;
  free (*error_message);
  *error_message = ccwmk_strdup (message);
}

int
ccwmk_load (const char *path, ccwmk_graph_t **out_graph, char **error_message)
{
  if (!out_graph || !path)
    {
      set_error (error_message, "ccwmk: invalid load request");
      return 0;
    }
  FILE *fp = fopen (path, "rb");
  if (!fp)
    {
      char buf[256];
      snprintf (buf, sizeof (buf), "ccwmk: %s: %s", path, strerror (errno));
      set_error (error_message, buf);
      return 0;
    }
  fclose (fp);

  ccwmk_graph_t *graph = ccwmk_graph_new ();
  if (!graph)
    {
      set_error (error_message, "ccwmk: out of memory");
      return 0;
    }
  graph->source_path = ccwmk_strdup (path);
  if (!graph->source_path)
    {
      ccwmk_graph_free (graph);
      set_error (error_message, "ccwmk: out of memory");
      return 0;
    }
  *out_graph = graph;
  return 1;
}
