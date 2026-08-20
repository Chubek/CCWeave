/* Parthia surface S-expression reader.
 *
 * The Swaff parse-only adapter emits the surface AST as S-expression text.
 * Atoms are raw SML tokens; string and character constants keep their
 * source quotes and may contain whitespace, parentheses, and escapes, so
 * the reader treats a double-quoted span (and the #"c" form) as one atom. */

#include "parthia_rt.h"

#include <stdlib.h>
#include <string.h>

typedef struct
{
  prt *rt;
  const char *text;
  size_t len;
  size_t pos;
  const char *error;
} srd;

static psx *
sexp_new (srd *r, int is_list)
{
  psx *node = (psx *)pa_alloc (r->rt, sizeof (*node));
  if (node == NULL)
    return NULL;
  node->is_list = is_list;
  node->atom = NULL;
  node->items = NULL;
  node->count = 0;
  return node;
}

static void
skip_space (srd *r)
{
  while (r->pos < r->len)
    {
      char c = r->text[r->pos];
      if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
        break;
      r->pos++;
    }
}

static psx *read_node (srd *r);

static psx *
read_list (srd *r)
{
  psx *node = sexp_new (r, 1);
  size_t cap = 0;
  if (node == NULL)
    return NULL;
  r->pos++; /* consume '(' */
  for (;;)
    {
      psx *child;
      skip_space (r);
      if (r->pos >= r->len)
        {
          r->error = "unbalanced '(' in surface AST";
          return NULL;
        }
      if (r->text[r->pos] == ')')
        {
          r->pos++;
          return node;
        }
      child = read_node (r);
      if (child == NULL)
        return NULL;
      if (node->count == cap)
        {
          size_t grown = cap == 0 ? 8u : cap * 2u;
          psx **items
              = (psx **)pa_alloc (r->rt, grown * sizeof (*items));
          if (items == NULL)
            return NULL;
          if (node->items != NULL)
            memcpy (items, node->items, node->count * sizeof (*items));
          node->items = items;
          cap = grown;
        }
      node->items[node->count++] = child;
    }
}

static psx *
read_atom (srd *r)
{
  size_t start = r->pos;
  int quoted = 0;
  psx *node;
  /* SML string constants are emitted with their quotes; #"x" is the
   * character form.  Scan to the closing unescaped quote. */
  if (r->text[r->pos] == '"'
      || (r->text[r->pos] == '#' && r->pos + 1 < r->len
          && r->text[r->pos + 1] == '"'))
    {
      if (r->text[r->pos] == '#')
        r->pos++;
      r->pos++; /* opening quote */
      quoted = 1;
      while (r->pos < r->len)
        {
          char c = r->text[r->pos];
          if (c == '\\' && r->pos + 1 < r->len)
            {
              r->pos += 2;
              continue;
            }
          if (c == '"')
            {
              r->pos++;
              break;
            }
          r->pos++;
        }
    }
  if (!quoted)
    while (r->pos < r->len)
      {
        char c = r->text[r->pos];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '('
            || c == ')')
          break;
        r->pos++;
      }
  node = sexp_new (r, 0);
  if (node == NULL)
    return NULL;
  node->atom = pa_strndup (r->rt, r->text + start, r->pos - start);
  if (node->atom == NULL)
    return NULL;
  return node;
}

static psx *
read_node (srd *r)
{
  skip_space (r);
  if (r->pos >= r->len)
    {
      r->error = "unexpected end of surface AST";
      return NULL;
    }
  if (r->text[r->pos] == ')')
    {
      r->error = "unbalanced ')' in surface AST";
      return NULL;
    }
  if (r->text[r->pos] == '(')
    return read_list (r);
  return read_atom (r);
}

psx *
pa_sexp_read (prt *rt, const char *text, char **error)
{
  srd r;
  psx *node;
  if (text == NULL)
    return NULL;
  r.rt = rt;
  r.text = text;
  r.len = strlen (text);
  r.pos = 0;
  r.error = NULL;
  node = read_node (&r);
  if (node == NULL)
    {
      if (error != NULL)
        {
          const char *msg = r.error != NULL ? r.error : "out of memory";
          size_t len = strlen (msg);
          char *copy = (char *)malloc (len + 1u);
          if (copy != NULL)
            memcpy (copy, msg, len + 1u);
          *error = copy;
        }
      return NULL;
    }
  skip_space (&r);
  if (r.pos != r.len)
    {
      if (error != NULL)
        {
          const char *msg = "trailing text after surface AST";
          char *copy = (char *)malloc (strlen (msg) + 1u);
          if (copy != NULL)
            strcpy (copy, msg);
          *error = copy;
        }
      return NULL;
    }
  return node;
}
