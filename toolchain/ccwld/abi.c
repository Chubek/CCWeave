#include "ccwld-lto.h"
#include <stdlib.h>
#include <string.h>
struct ccwld_lto_ctx
{
  ccwld_lto_config cfg;
  char *error;
};
ccwld_lto_ctx *
ccwld_lto_begin (const ccwld_lto_config *c)
{
  if (!c || c->abi_version != CCWLD_LTO_ABI_VERSION)
    return NULL;
  ccwld_lto_ctx *x = calloc (1, sizeof (*x));
  if (x)
    x->cfg = *c;
  return x;
}
int
ccwld_lto_add_module (ccwld_lto_ctx *c, const void *b, size_t n,
                      const char *name)
{
  (void)c;
  (void)b;
  (void)n;
  (void)name;
  return 0;
}
int
ccwld_lto_run (ccwld_lto_ctx *c,
               void (*emit) (void *, const void *, size_t, const char *),
               void *u)
{
  (void)c;
  (void)emit;
  (void)u;
  return 0;
}
void
ccwld_lto_end (ccwld_lto_ctx *c)
{
  if (c)
    {
      free (c->error);
      free (c);
    }
}
const char *
ccwld_lto_last_error (ccwld_lto_ctx *c)
{
  return c ? c->error : NULL;
}
