#include "ccw_sema.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *
dup_text (const char *text)
{
  size_t length;
  char *copy;
  if (text == NULL)
    return NULL;
  length = strlen (text);
  copy = (char *)malloc (length + 1u);
  if (copy != NULL)
    memcpy (copy, text, length + 1u);
  return copy;
}

static void
set_error (char **error_message, const char *text)
{
  if (error_message != NULL)
    *error_message = dup_text (text);
}

static int
read_ruleset (const char *path, int *rule_count)
{
  FILE *file;
  char buffer[4096];
  size_t got;
  size_t used = 0;
  char *source;
  const char *cursor;
  int count = 0;

  if (rule_count != NULL)
    *rule_count = 0;
  file = fopen (path, "rb");
  if (file == NULL)
    return 0;
  source = NULL;
  while ((got = fread (buffer, 1, sizeof buffer, file)) != 0)
    {
      char *grown = (char *)realloc (source, used + got + 1u);
      if (grown == NULL)
        {
          free (source);
          fclose (file);
          return 0;
        }
      source = grown;
      memcpy (source + used, buffer, got);
      used += got;
      source[used] = '\0';
    }
  fclose (file);
  if (source == NULL)
    return 0;

  if (strstr (source, "(sema-ruleset ") == NULL)
    {
      free (source);
      return 0;
    }
  cursor = source;
  while ((cursor = strstr (cursor, "(sema-rule ")) != NULL)
    {
      count++;
      cursor += strlen ("(sema-rule ");
    }
  free (source);
  if (rule_count != NULL)
    *rule_count = count;
  return count > 0;
}

static int
ruleset_path (char *path, size_t path_size, const char *salvo_dir,
              const char *ruleset)
{
  const char *name = ruleset;
  char relative[256];
  size_t out = 0;
  if (salvo_dir == NULL || ruleset == NULL || strncmp (name, "sema.", 5) != 0)
    return 0;
  name += 5;
  while (*name != '\0' && out + 2u < sizeof relative)
    {
      relative[out++] = *name == '.' ? '/' : *name;
      name++;
    }
  if (*name != '\0')
    return 0;
  relative[out] = '\0';
  if (snprintf (path, path_size, "%s/%s/rules.scm", salvo_dir, relative)
      >= (int)path_size)
    return 0;
  return 1;
}

ccw_status
ccw_sema_analyze (ccw_ir *ir, const char *salvo_dir,
                  const char *const *rulesets, size_t ruleset_count,
                  ccw_sema_report *report, char **error_message)
{
  ccw_sema_report local = { 0, 0 };
  char *validation_error = NULL;

  if (error_message != NULL)
    *error_message = NULL;
  if (report != NULL)
    *report = local;
  if (ir == NULL || salvo_dir == NULL
      || (ruleset_count != 0 && rulesets == NULL))
    {
      set_error (error_message,
                 "sema: IR, salvo directory, and rulesets are required");
      return CCW_ERR_TYPE;
    }

  for (size_t i = 0; i < ruleset_count; i++)
    {
      char path[1024];
      int rule_count = 0;
      if (!ruleset_path (path, sizeof path, salvo_dir, rulesets[i])
          || !read_ruleset (path, &rule_count))
        {
          char message[1200];
          snprintf (message, sizeof message, "sema: cannot load ruleset %s",
                    rulesets[i] ? rulesets[i] : "(null)");
          set_error (error_message, message);
          return CCW_ERR_LOAD;
        }
      local.rulesets_loaded++;
      local.rules_loaded += rule_count;
      if (ccw_ir_attr_set (ir, 0, rulesets[i], "applied") != CCW_OK)
        {
          set_error (error_message, "sema: cannot record applied ruleset");
          return CCW_ERR_OOM;
        }
    }

  if (ccw_ir_validate (ir, &validation_error) != CCW_OK)
    {
      if (error_message != NULL)
        *error_message = validation_error;
      else
        free (validation_error);
      return CCW_ERR_TYPE;
    }
  if (report != NULL)
    *report = local;
  return CCW_OK;
}
