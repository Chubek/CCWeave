/* lto_badabi — an LTO backend whose session rejects every ABI version.
 * ccwld_lto_begin returning NULL is fatal with exit 3; this backend
 * also answers ccwld_lto_last_error(NULL) so the diagnostic carries a
 * backend message. */
#include "../../abi/ccwld-lto.h"

#include <stddef.h>

static const char MSG[] = "reference backend: unsupported abi version";

ccwld_lto_ctx *
ccwld_lto_begin (const ccwld_lto_config *cfg)
{
  (void)cfg;
  return NULL;
}

int
ccwld_lto_add_module (ccwld_lto_ctx *c, const void *b, size_t n,
                      const char *name)
{
  (void)c;
  (void)b;
  (void)n;
  (void)name;
  return -1;
}

int
ccwld_lto_run (ccwld_lto_ctx *c,
               void (*emit) (void *, const void *, size_t, const char *),
               void *u)
{
  (void)c;
  (void)emit;
  (void)u;
  return -1;
}

void
ccwld_lto_end (ccwld_lto_ctx *c)
{
  (void)c;
}

const char *
ccwld_lto_last_error (ccwld_lto_ctx *c)
{
  (void)c;
  return MSG;
}
