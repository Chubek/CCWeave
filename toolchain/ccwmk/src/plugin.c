#include "../ccw-ccwmk.h"

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

ccwmk_registry_t *
ccwmk_registry_new (void)
{
  return (ccwmk_registry_t *)calloc (1, sizeof (ccwmk_registry_t));
}

void
ccwmk_registry_free (ccwmk_registry_t *registry)
{
  if (!registry)
    return;
  free (registry->scanners);
  registry->scanners = NULL;
  registry->scanner_count = 0;
  registry->scanner_capacity = 0;
}

static int
ensure_scanner_capacity (ccwmk_registry_t *registry, size_t need)
{
  if (registry->scanner_capacity >= need)
    return 1;
  size_t next = registry->scanner_capacity ? registry->scanner_capacity * 2 : 4;
  while (next < need)
    next *= 2;
  void *p = realloc (registry->scanners, next * sizeof (*registry->scanners));
  if (!p)
    return 0;
  registry->scanners = (ccwmk_scanner_t *)p;
  registry->scanner_capacity = next;
  return 1;
}

int
ccwmk_registry_register_scanner (ccwmk_registry_t *registry,
                                 const ccwmk_scanner_t *scanner,
                                 char **error_message)
{
  if (!registry || !scanner || !scanner->language || !scanner->scan)
    {
      set_error (error_message, "ccwmk: invalid scanner");
      return 0;
    }
  for (size_t i = 0; i < registry->scanner_count; i++)
    if (!strcmp (registry->scanners[i].language, scanner->language))
      {
        registry->scanners[i] = *scanner;
        return 1;
      }
  if (!ensure_scanner_capacity (registry, registry->scanner_count + 1))
    {
      set_error (error_message, "ccwmk: out of memory");
      return 0;
    }
  registry->scanners[registry->scanner_count++] = *scanner;
  return 1;
}

const ccwmk_scanner_t *
ccwmk_registry_find_scanner (const ccwmk_registry_t *registry,
                             const char *language)
{
  if (!registry || !language)
    return NULL;
  for (size_t i = 0; i < registry->scanner_count; i++)
    if (registry->scanners[i].language
        && !strcmp (registry->scanners[i].language, language))
      return &registry->scanners[i];
  return NULL;
}

int
ccwmk_plugin_init (ccwmk_registry_t *registry)
{
  (void)registry;
  return 0;
}
