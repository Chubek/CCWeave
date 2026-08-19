/* Loads a stdrewrite ruleset file (§7.3).
 *
 * Format (Scheme s-expressions, read without an engine since rules are
 * data, not code):
 *
 *   (ruleset opt.arith)
 *   (rule mul-two-to-shift
 *         (imul ?x (iconst 2)) (shl ?x (iconst 1))
 *         :bidirectional #t)
 *
 * Every file MUST declare a ruleset name; rules are unordered within it. */

#include "ccw_oeuph.h"
#include "kstring.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
skip_blanks_and_comments (const char **p)
{
  for (;;)
    {
      while (**p != '\0' && isspace ((unsigned char)**p))
        (*p)++;
      if (**p == ';')
        {
          while (**p != '\0' && **p != '\n')
            (*p)++;
          continue;
        }
      return;
    }
}

/* Reads one balanced s-expression into a fresh string. */
static char *
read_form (const char **p)
{
  skip_blanks_and_comments (p);
  if (**p != '(')
    return NULL;
  const char *start = *p;
  int depth = 0;
  while (**p != '\0')
    {
      if (**p == '(')
        depth++;
      else if (**p == ')')
        {
          depth--;
          if (depth == 0)
            {
              (*p)++;
              break;
            }
        }
      else if (**p == ';')
        {
          while (**p != '\0' && **p != '\n')
            (*p)++;
          continue;
        }
      (*p)++;
    }
  if (depth != 0)
    return NULL;
  size_t n = (size_t)(*p - start);
  kstring_t form = { 0, 0, NULL };
  if (kputsn (start, (int)n, &form) == EOF)
    return NULL;
  return ks_release (&form);
}

/* Copies the next whitespace-delimited token or balanced sub-form. */
static char *
next_item (const char **p)
{
  skip_blanks_and_comments (p);
  if (**p == '\0' || **p == ')')
    return NULL;
  const char *start = *p;
  if (**p == '(')
    {
      int depth = 0;
      while (**p != '\0')
        {
          if (**p == '(')
            depth++;
          else if (**p == ')')
            {
              depth--;
              if (depth == 0)
                {
                  (*p)++;
                  break;
                }
            }
          (*p)++;
        }
    }
  else
    {
      while (**p != '\0' && !isspace ((unsigned char)**p) && **p != ')')
        (*p)++;
    }
  size_t n = (size_t)(*p - start);
  kstring_t item = { 0, 0, NULL };
  if (kputsn (start, (int)n, &item) == EOF)
    return NULL;
  return ks_release (&item);
}

static char *
read_whole_file (const char *path)
{
  FILE *fp = fopen (path, "rb");
  if (fp == NULL)
    return NULL;
  kstring_t buf = { 0, 0, NULL };
  char chunk[4096];
  size_t got;
  while ((got = fread (chunk, 1, sizeof (chunk), fp)) > 0)
    {
      if (kputsn (chunk, (int)got, &buf) == EOF)
        {
          free (buf.s);
          fclose (fp);
          return NULL;
        }
    }
  fclose (fp);
  return ks_release (&buf);
}

static void
set_err (char **error_message, const char *msg)
{
  if (error_message == NULL)
    return;
  kstring_t copy = { 0, 0, NULL };
  if (msg != NULL && kputs (msg, &copy) != EOF)
    *error_message = ks_release (&copy);
  else
    *error_message = NULL;
}

ccw_oeuph_ruleset *
ccw_oeuph_ruleset_load (const char *path, char **error_message)
{
  if (error_message)
    *error_message = NULL;
  char *text = read_whole_file (path);
  if (text == NULL)
    {
      set_err (error_message, "cannot read ruleset file");
      return NULL;
    }

  ccw_oeuph_ruleset *rs = NULL;
  const char *cursor = text;
  char *form = NULL;

  while ((form = read_form (&cursor)) != NULL)
    {
      const char *inner = form + 1; /* skip '(' */
      char *head = next_item (&inner);
      if (head == NULL)
        {
          free (form);
          continue;
        }

      if (strcmp (head, "ruleset") == 0)
        {
          char *name = next_item (&inner);
          if (name == NULL)
            {
              set_err (error_message, "ruleset form needs a name");
              free (head);
              free (form);
              free (text);
              ccw_oeuph_ruleset_destroy (rs);
              return NULL;
            }
          if (rs == NULL)
            rs = ccw_oeuph_ruleset_create (name);
          free (name);
        }
      else if (strcmp (head, "rule") == 0)
        {
          if (rs == NULL)
            {
              set_err (error_message,
                       "rule appears before the ruleset declaration");
              free (head);
              free (form);
              free (text);
              return NULL;
            }
          char *name = next_item (&inner);
          char *lhs = next_item (&inner);
          char *rhs = next_item (&inner);
          bool bidirectional = true; /* bidirectional by default (§7.3) */
          char *opt = NULL;
          while ((opt = next_item (&inner)) != NULL)
            {
              if (strcmp (opt, ":bidirectional") == 0)
                {
                  char *value = next_item (&inner);
                  if (value != NULL && strcmp (value, "#f") == 0)
                    bidirectional = false;
                  free (value);
                }
              free (opt);
            }
          if (name != NULL && lhs != NULL && rhs != NULL)
            {
              char *rule_err = NULL;
              if (ccw_oeuph_rule_add (rs, name, lhs, rhs, bidirectional, NULL,
                                      &rule_err)
                  != CCW_OK)
                {
                  if (error_message)
                    *error_message = rule_err;
                  else
                    free (rule_err);
                  free (name);
                  free (lhs);
                  free (rhs);
                  free (head);
                  free (form);
                  free (text);
                  ccw_oeuph_ruleset_destroy (rs);
                  return NULL;
                }
            }
          free (name);
          free (lhs);
          free (rhs);
        }
      free (head);
      free (form);
    }
  free (text);

  if (rs == NULL)
    set_err (error_message, "file declares no ruleset");
  return rs;
}
