#include "../ccwld.h"

#include <stdio.h>
#include <string.h>

int
ccwld_emit_binaryen (const char *output, const char *entry,
                     ccwld_error *error)
{
  (void)output;
  (void)entry;
  if (error != NULL)
    {
      error->code = 4;
      snprintf (error->message, sizeof (error->message),
                "WASM emission requires CCWEAVE_ENABLE_BINARYEN=ON");
    }
  return 0;
}
