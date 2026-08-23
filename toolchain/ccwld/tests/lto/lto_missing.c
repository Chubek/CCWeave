/* lto_missing — a backend missing the mandatory entry points
 * (exports only ccwld_lto_begin).  CCWld must diagnose the missing
 * ccwld-lto symbols with exit 3, never guess at partial ABIs. */
#include "../../abi/ccwld-lto.h"

#include <stddef.h>

ccwld_lto_ctx *
ccwld_lto_begin (const ccwld_lto_config *cfg)
{
  (void)cfg;
  return NULL;
}
