#include "../ccwld.h"

#include <stdio.h>

int
ccwld_emit_lief (const char *input, const char *output, const char *kind,
                 const char *format, const char *entry, const char *note,
                 ccwld_error *error)
{
  (void)input;
  (void)output;
  (void)kind;
  (void)format;
  (void)entry;
  (void)note;
  if (error != NULL)
    {
      error->code = 4;
      snprintf (error->message, sizeof (error->message),
                "ELF emission requires CCWEAVE_ENABLE_LIEF=ON");
    }
  return 0;
}
