/* Loads a rewrite-salvo ruleset file (§7.3).
 *
 * The complete file is wrapped in one synthetic list and parsed by SFSEXP.
 * This preserves all top-level forms while avoiding a second, subtly
 * different parenthesis/comment scanner in the host. */

#include "ccw_oeuph.h"
#include "kstring.h"

#include "cstring.h"
#include "sexp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *
read_whole_file (const char *path)
{
  FILE *fp = fopen (path, "rb");
  kstring_t buf = { 0, 0, NULL };
  char chunk[4096];
  size_t got;
  if (fp == NULL)
    return NULL;
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
  kstring_t copy = { 0, 0, NULL };
  if (error_message == NULL)
    return;
  *error_message = NULL;
  if (msg != NULL && kputs (msg, &copy) != EOF)
    *error_message = ks_release (&copy);
}

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

static const sexp_t *
form_item (const sexp_t *form, size_t index)
{
  const sexp_t *item = form != NULL ? form->list : NULL;
  while (item != NULL && index != 0)
    {
      item = item->next;
      index--;
    }
  return item;
}

static size_t
form_length (const sexp_t *form)
{
  const sexp_t *item;
  size_t count = 0;
  for (item = form != NULL ? form->list : NULL; item != NULL;
       item = item->next)
    count++;
  return count;
}

static int
is_atom (const sexp_t *item, const char *text)
{
  return item != NULL && item->ty == SEXP_VALUE && item->val != NULL
         && strcmp (item->val, text) == 0;
}

static char *
print_form (const sexp_t *form)
{
  CSTRING *printed = NULL;
  char *copy;
  size_t length;
  if (form == NULL || print_sexp_cstr (&printed, form, 64) < 0
      || printed == NULL || printed->base == NULL)
    {
      if (printed != NULL)
        sdestroy (printed);
      return NULL;
    }
  length = strlen (printed->base);
  copy = (char *)malloc (length + 1u);
  if (copy != NULL)
    memcpy (copy, printed->base, length + 1u);
  sdestroy (printed);
  return copy;
}

static void
destroy_parse (sexp_t *root)
{
  if (root != NULL)
    destroy_sexp (root);
  sexp_cleanup ();
}

ccw_oeuph_ruleset *
ccw_oeuph_ruleset_load (const char *path, char **error_message)
{
  char *source;
  char *wrapped;
  size_t source_length;
  kstring_t input = { 0, 0, NULL };
  sexp_t *root;
  const sexp_t *form;
  ccw_oeuph_ruleset *ruleset = NULL;

  if (error_message != NULL)
    *error_message = NULL;
  source = read_whole_file (path);
  if (source == NULL)
    {
      set_err (error_message, "cannot read ruleset file");
      return NULL;
    }
  source_length = strlen (source);
  if (kputs ("(ccw-ruleset-root\n", &input) == EOF
      || kputsn (source, (int)source_length, &input) == EOF
      || kputs ("\n)", &input) == EOF)
    {
      free (source);
      free (input.s);
      set_err (error_message, "out of memory while reading ruleset file");
      return NULL;
    }
  free (source);
  wrapped = ks_release (&input);
  reset_sexp_errno ();
  root = parse_sexp (wrapped, strlen (wrapped));
  free (wrapped);
  if (root == NULL)
    {
      set_err (error_message, "invalid ruleset S-expression");
      sexp_cleanup ();
      return NULL;
    }

  form = root->list != NULL ? root->list->next : NULL;
  for (; form != NULL; form = form->next)
    {
      const sexp_t *head;
      size_t length;
      if (form->ty != SEXP_LIST)
        {
          set_err (error_message, "ruleset top-level form must be a list");
          destroy_parse (root);
          ccw_oeuph_ruleset_destroy (ruleset);
          return NULL;
        }
      head = form_item (form, 0);
      length = form_length (form);
      if (is_atom (head, "ruleset"))
        {
          const sexp_t *name = form_item (form, 1);
          if (length != 2 || name == NULL || name->ty != SEXP_VALUE
              || name->val == NULL)
            {
              set_err (error_message, "ruleset form needs one name");
              destroy_parse (root);
              ccw_oeuph_ruleset_destroy (ruleset);
              return NULL;
            }
          if (ruleset == NULL)
            ruleset = ccw_oeuph_ruleset_create (name->val);
        }
      else if (is_atom (head, "rule"))
        {
          const sexp_t *name = form_item (form, 1);
          const sexp_t *lhs = form_item (form, 2);
          const sexp_t *rhs = form_item (form, 3);
          bool bidirectional = true;
          char *lhs_text;
          char *rhs_text;
          char *rule_error = NULL;
          if (ruleset == NULL)
            {
              set_err (error_message,
                       "rule appears before the ruleset declaration");
              destroy_parse (root);
              return NULL;
            }
          if (length < 4 || name == NULL || name->ty != SEXP_VALUE
              || name->val == NULL || lhs == NULL || rhs == NULL)
            {
              set_err (error_message, "rule needs name, lhs, and rhs");
              destroy_parse (root);
              ccw_oeuph_ruleset_destroy (ruleset);
              return NULL;
            }
          for (size_t i = 4; i + 1 < length; i += 2)
            {
              const sexp_t *option = form_item (form, i);
              const sexp_t *value = form_item (form, i + 1);
              if (is_atom (option, ":bidirectional"))
                bidirectional = !is_atom (value, "#f");
            }
          lhs_text = print_form (lhs);
          rhs_text = print_form (rhs);
          if (lhs_text == NULL || rhs_text == NULL
              || ccw_oeuph_rule_add (ruleset, name->val, lhs_text, rhs_text,
                                      bidirectional, NULL, &rule_error)
                   != CCW_OK)
            {
              if (error_message != NULL)
                *error_message = rule_error != NULL
                                     ? rule_error
                                     : dup_text ("invalid rule");
              else
                free (rule_error);
              free (lhs_text);
              free (rhs_text);
              destroy_parse (root);
              ccw_oeuph_ruleset_destroy (ruleset);
              return NULL;
            }
          free (lhs_text);
          free (rhs_text);
        }
    }
  destroy_parse (root);
  if (ruleset == NULL)
    set_err (error_message, "file declares no ruleset");
  return ruleset;
}
