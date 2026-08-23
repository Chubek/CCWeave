/* §7.3: the link cache — 4-lane FNV-1a key over the full influence set. */
#include "ccwld_cache.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* --- 4-lane FNV-1a (same regime as the plan hash) --- */

typedef struct
{
  uint64_t h1, h2, h3, h4;
} lane_t;

static void
lane_init (lane_t *l)
{
  l->h1 = 14695981039346656037ULL;
  l->h2 = 1099511628211ULL;
  l->h3 = 0x9e3779b97f4a7c15ULL;
  l->h4 = 0xc6a4a7935bd1e995ULL;
}

static void
lane_bytes (lane_t *l, const void *data, size_t n)
{
  const unsigned char *p = data;
  for (size_t i = 0; i < n; i++)
    {
      uint8_t b = p[i];
      l->h1 ^= b;
      l->h1 *= 1099511628211ULL;
      l->h2 = (l->h2 ^ b) * 1099511628211ULL;
      l->h3 = (l->h3 ^ b) * 1099511628211ULL;
      l->h4 = (l->h4 ^ b) * 1099511628211ULL;
    }
}

/* boundary fence: the same bytes at different boundaries differ */
static void
lane_sep (lane_t *l)
{
  uint8_t fence = 0x1f;
  lane_bytes (l, &fence, 1);
}

static void
lane_str (lane_t *l, const char *s)
{
  if (!s)
    {
      uint8_t z = 0;
      lane_bytes (l, &z, 1);
      return;
    }
  lane_bytes (l, s, strlen (s) + 1);
  lane_sep (l);
}

static void
lane_hex (lane_t *l, char out[65])
{
  snprintf (out, 65, "%016" PRIx64 "%016" PRIx64 "%016" PRIx64
                    "%016" PRIx64,
            l->h1, l->h2, l->h3, l->h4);
}

/* --- input file content hash --- */

static void
hash_file (lane_t *l, const char *path)
{
  FILE *f = fopen (path, "rb");
  if (!f)
    {
      lane_str (l, "<missing>");
      lane_str (l, path);
      return;
    }
  char buf[8192];
  size_t n;
  while ((n = fread (buf, 1, sizeof (buf), f)) > 0)
    lane_bytes (l, buf, n);
  lane_sep (l);
  fclose (f);
}

/* --- key --- */

int
ccwld_cache_key (const ccwld_plan *p, char out[65])
{
  if (!p || !out)
    return 0;

  lane_t l;
  lane_init (&l);

  lane_str (&l, "ccwld-cache-v1");
  lane_str (&l, CCWLD_VERSION);
  lane_str (&l, p->target);
  lane_str (&l, p->plan_hash); /* the serialized declarative plan */

  /* input contents in link order */
  for (size_t i = 0; i < p->ninputs; i++)
    {
      lane_str (&l, p->inputs[i].path);
      uint8_t flags = (uint8_t)((p->inputs[i].as_needed ? 1 : 0)
                                | (p->inputs[i].startup ? 2 : 0)
                                | (p->inputs[i].is_group ? 4 : 0));
      lane_bytes (&l, &flags, 1);
      lane_sep (&l);
      hash_file (&l, p->inputs[i].path);
    }

  /* LTO + plugin identity */
  lane_str (&l, p->lto.pipeline);
  uint8_t jobs = (uint8_t)(p->lto.jobs & 0xff);
  lane_bytes (&l, &jobs, 1);
  lane_sep (&l);
  for (size_t i = 0; i < p->nplugins; i++)
    {
      lane_str (&l, p->plugins[i].path);
      lane_str (&l, p->plugins[i].options);
    }

  /* pipeline options */
  uint8_t opts = (uint8_t)((p->options.gc_sections ? 1 : 0)
                           | (p->options.as_needed_default ? 2 : 0)
                           | (p->options.reproducible ? 4 : 0));
  lane_bytes (&l, &opts, 1);
  lane_sep (&l);

  lane_hex (&l, out);
  return 1;
}

/* --- lookup / store (plain copy; the emitter guarantees determinism) --- */

static int
copy_file (const char *from, const char *to)
{
  FILE *a = fopen (from, "rb");
  if (!a)
    return 0;
  FILE *b = fopen (to, "wb");
  if (!b)
    {
      fclose (a);
      return 0;
    }
  char buf[8192];
  size_t n;
  int ok = 1;
  while ((n = fread (buf, 1, sizeof (buf), a)) > 0)
    if (fwrite (buf, 1, n, b) != n)
      {
        ok = 0;
        break;
      }
  fclose (a);
  if (fclose (b) != 0)
    ok = 0;
  return ok;
}

int
ccwld_cache_lookup (const char *dir, const char *key, const char *output,
                    ccwld_error *e)
{
  char path[4096];
  snprintf (path, sizeof (path), "%s/%s", dir, key);
  FILE *f = fopen (path, "rb");
  if (!f)
    return 0; /* miss */
  fclose (f);
  if (!copy_file (path, output))
    {
      ccwld_error_set (e, CCWLD_EXIT_INTERNAL,
                       "cache hit for '%s' but the artifact could not be "
                       "copied to '%s'",
                       key, output);
      return -1;
    }
  return 1;
}

int
ccwld_cache_store (const char *dir, const char *key, const char *output,
                   ccwld_error *e)
{
  mkdir (dir, 0777); /* best effort: exists in the common case */
  char path[4096];
  snprintf (path, sizeof (path), "%s/%s", dir, key);
  char tmp[4112];
  snprintf (tmp, sizeof (tmp), "%s.tmp%d", path, (int)getpid ());
  if (!copy_file (output, tmp))
    {
      ccwld_error_set (e, CCWLD_EXIT_INTERNAL,
                       "cache store to '%s' failed", path);
      return 0;
    }
  if (rename (tmp, path) != 0)
    {
      remove (tmp);
      ccwld_error_set (e, CCWLD_EXIT_INTERNAL,
                       "cache store to '%s' failed", path);
      return 0;
    }
  return 1;
}
