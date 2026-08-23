/* §7.3: the link cache.
 *
 * The cache key is a content-addressable hash over everything that can
 * influence the output: the serialized plan (itself hashed), the content
 * of every input file, the LTO pipeline identity, plugin identities, the
 * pipeline options, and CCWLD_VERSION.  Two links with the same key must
 * produce byte-identical outputs, so a hit is a pure file copy. */
#ifndef CCWLD_CACHE_H
#define CCWLD_CACHE_H

#include "../ccwld.h"

#ifdef __cplusplus
extern "C"
{
#endif

  /* Compute the cache key for a sealed plan (64 hex chars + NUL).
   * Reads every input file; missing files are hashed by name so the
   * key stays stable instead of failing the lookup outright. */
  int ccwld_cache_key (const ccwld_plan *p, char out[65]);

  /* On a hit, copy the cached artifact to `output` and return 1. */
  int ccwld_cache_lookup (const char *dir, const char *key,
                          const char *output, ccwld_error *e);

  /* Store `output` under `key` (best effort: failures are diagnostics,
   * not link errors). */
  int ccwld_cache_store (const char *dir, const char *key,
                         const char *output, ccwld_error *e);

#ifdef __cplusplus
}
#endif
#endif /* CCWLD_CACHE_H */
