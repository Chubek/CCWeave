#include "../ccwld.h"
#include <stdio.h>
#include <string.h>
int
main (int argc, char **argv)
{
  const char *script = NULL, *out = "a.out", *target = "unknown";
  ccwld_error e;
  for (int i = 1; i < argc; i++)
    {
      if ((!strcmp (argv[i], "-T") || !strcmp (argv[i], "--script"))
          && i + 1 < argc)
        script = argv[++i];
      else if ((!strcmp (argv[i], "-o")) && i + 1 < argc)
        out = argv[++i];
      else if (!strcmp (argv[i], "--target") && i + 1 < argc)
        target = argv[++i];
    }
  if (!script)
    {
      fprintf (stderr, "ccwld: missing -T/--script\n");
      return 2;
    }
  if (!ccwld_run_script (script, target, out, &e))
    {
      fprintf (stderr, "ccwld: %s\n", e.message);
      return 1;
    }
  return 0;
}
