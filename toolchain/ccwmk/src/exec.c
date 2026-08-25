#include "../ccw-ccwmk.h"

#include <stdlib.h>
#include <string.h>

static void
set_error (char **error_message, const char *message)
{
  if (!error_message)
    return;
  free (*error_message);
  *error_message = NULL;
  if (message)
    {
      size_t n = strlen (message) + 1;
      *error_message = (char *)malloc (n);
      if (*error_message)
        memcpy (*error_message, message, n);
    }
}

int
ccwmk_build (ccwmk_graph_t *graph, char **error_message)
{
  if (!graph)
    {
      set_error (error_message, "ccwmk: invalid graph");
      return 0;
    }
  set_error (error_message, "ccwmk: build pipeline not implemented yet");
  return 0;
}
