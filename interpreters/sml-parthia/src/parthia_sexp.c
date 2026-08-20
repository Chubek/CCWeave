/* Parthia surface S-expression reader.
 *
 * Swaff emits one complete surface tree.  SFSEXP owns parsing, quoting,
 * comments, and balanced-list validation; the small psx bridge below copies
 * only the shape and canonical atom spelling into Parthia's runtime arena. */

#include "parthia_rt.h"

#include "sexp.h"

#include <stdlib.h>
#include <string.h>

static psx *
psx_new (prt *rt, int is_list)
{
  psx *node = (psx *)pa_alloc (rt, sizeof (*node));
  if (node == NULL)
    return NULL;
  node->is_list = is_list;
  node->atom = NULL;
  node->items = NULL;
  node->count = 0;
  return node;
}

static char *
sfsexp_error (void)
{
  switch (sexp_errno)
    {
    case SEXP_ERR_MEMORY:
    case SEXP_ERR_MEM_LIMIT:
      return "out of memory while reading surface AST";
    case SEXP_ERR_INCOMPLETE:
      return "incomplete surface AST";
    case SEXP_ERR_BAD_PARAM:
    case SEXP_ERR_BADCONTENT:
    case SEXP_ERR_BAD_CONSTRUCTOR:
    default:
      return "invalid surface AST S-expression";
    }
}

static char *
copy_error (const char *message)
{
  size_t length = strlen (message);
  char *copy = (char *)malloc (length + 1u);
  if (copy != NULL)
    memcpy (copy, message, length + 1u);
  return copy;
}

static psx *convert_node (prt *rt, const sexp_t *source);

static int
is_sml_char_pair (const sexp_t *item)
{
  return item != NULL && item->ty == SEXP_VALUE && item->aty == SEXP_BASIC
         && item->val != NULL && strcmp (item->val, "#") == 0
         && item->next != NULL && item->next->ty == SEXP_VALUE
         && item->next->aty == SEXP_DQUOTE && item->next->val != NULL;
}

static psx *
convert_sml_char_pair (prt *rt, const sexp_t *item)
{
  const char *body = item->next->val;
  size_t length = strlen (body);
  psx *node = psx_new (rt, 0);
  if (node == NULL)
    return NULL;
  node->atom = (char *)pa_alloc (rt, length + 4u);
  if (node->atom == NULL)
    return NULL;
  node->atom[0] = '#';
  node->atom[1] = '"';
  memcpy (node->atom + 2, body, length);
  node->atom[length + 2u] = '"';
  node->atom[length + 3u] = '\0';
  return node;
}

static psx *
convert_list (prt *rt, const sexp_t *source)
{
  const sexp_t *item;
  psx *node = psx_new (rt, 1);
  size_t count = 0;
  size_t index = 0;

  if (node == NULL)
    return NULL;
  for (item = source->list; item != NULL; item = item->next)
    {
      count++;
      if (is_sml_char_pair (item))
        item = item->next;
    }
  if (count != 0)
    {
      node->items = (psx **)pa_alloc (rt, count * sizeof (*node->items));
      if (node->items == NULL)
        return NULL;
      for (item = source->list; item != NULL; item = item->next)
        {
          if (is_sml_char_pair (item))
            {
              node->items[index] = convert_sml_char_pair (rt, item);
              item = item->next;
            }
          else
            node->items[index] = convert_node (rt, item);
          if (node->items[index] == NULL)
            return NULL;
          index++;
        }
    }
  node->count = count;
  return node;
}

static psx *
convert_atom (prt *rt, const sexp_t *source)
{
  psx *node;
  if (source->val == NULL)
    return NULL;
  node = psx_new (rt, 0);
  if (node == NULL)
    return NULL;
  /* SFSEXP has already removed surrounding string quotes from dquoted
   * values.  Reconstruct the source spelling where needed so the existing
   * compiler can distinguish SML literals from identifiers. */
  if (source->aty == SEXP_DQUOTE)
    {
      size_t length = strlen (source->val);
      node->atom = (char *)pa_alloc (rt, length + 3u);
      if (node->atom == NULL)
        return NULL;
      node->atom[0] = '"';
      memcpy (node->atom + 1, source->val, length);
      node->atom[length + 1u] = '"';
      node->atom[length + 2u] = '\0';
    }
  else
    node->atom = pa_strdup (rt, source->val);
  return node;
}

static psx *
convert_node (prt *rt, const sexp_t *source)
{
  if (source == NULL)
    return NULL;
  return source->ty == SEXP_LIST ? convert_list (rt, source)
                                 : convert_atom (rt, source);
}

psx *
pa_sexp_read (prt *rt, const char *text, char **error)
{
  char *input;
  size_t length;
  sexp_t *parsed;
  psx *result;

  if (error != NULL)
    *error = NULL;
  if (rt == NULL || text == NULL)
    return NULL;
  length = strlen (text);
  input = (char *)malloc (length + 1u);
  if (input == NULL)
    {
      if (error != NULL)
        *error = copy_error ("out of memory while reading surface AST");
      return NULL;
    }
  memcpy (input, text, length + 1u);

  reset_sexp_errno ();
  parsed = parse_sexp (input, length);
  free (input);
  if (parsed == NULL)
    {
      if (error != NULL)
        *error = copy_error (sfsexp_error ());
      sexp_cleanup ();
      return NULL;
    }
  result = convert_node (rt, parsed);
  destroy_sexp (parsed);
  sexp_cleanup ();
  if (result == NULL && error != NULL)
    *error = copy_error ("out of memory while converting surface AST");
  return result;
}
