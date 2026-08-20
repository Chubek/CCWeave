/* Pattern parser for Oeuph rules.
 *
 * Patterns are data, so use the vendored SFSEXP parser for all syntax and
 * then lower its linked tree into the bounded canonical pattern representation
 * used by the e-graph. */

#include "ccw_oeuph_pattern.h"

#include "sexp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
  ccw_pattern *pat;
  char *reason;
  size_t reason_size;
  bool failed;
} pstate;

static void
pfail (pstate *s, const char *msg)
{
  if (!s->failed && s->reason != NULL && s->reason_size != 0)
    snprintf (s->reason, s->reason_size, "%s", msg);
  s->failed = true;
}

static int
alloc_node (pstate *s)
{
  int id;
  if (s->pat->count >= CCW_PAT_MAX_NODES)
    {
      pfail (s, "pattern too large");
      return -1;
    }
  id = s->pat->count++;
  memset (&s->pat->nodes[id], 0, sizeof (s->pat->nodes[id]));
  return id;
}

static size_t
children_count (const sexp_t *node)
{
  const sexp_t *child;
  size_t count = 0;
  for (child = node->list; child != NULL; child = child->next)
    count++;
  return count;
}

static const sexp_t *
child_at (const sexp_t *node, size_t index)
{
  const sexp_t *child = node->list;
  while (child != NULL && index != 0)
    {
      child = child->next;
      index--;
    }
  return child;
}

static int
parse_integer (const char *text, int64_t *value)
{
  char *end = NULL;
  long long parsed;
  if (text == NULL || *text == '\0')
    return 0;
  parsed = strtoll (text, &end, 10);
  if (end == text || *end != '\0')
    return 0;
  *value = (int64_t)parsed;
  return 1;
}

static int parse_node (pstate *s, const sexp_t *source);

static int
parse_atom (pstate *s, const sexp_t *source)
{
  int id;
  int64_t value;
  ccw_pat_node *node;
  if (source->val == NULL || source->aty == SEXP_DQUOTE)
    {
      pfail (s, "pattern atoms must be identifiers or integers");
      return -1;
    }
  id = alloc_node (s);
  if (id < 0)
    return -1;
  node = &s->pat->nodes[id];
  if (source->val[0] == '?')
    {
      node->kind = CCW_PAT_VAR;
      snprintf (node->text, sizeof (node->text), "%s", source->val);
    }
  else if (parse_integer (source->val, &value))
    {
      node->kind = CCW_PAT_CONST;
      node->value = value;
    }
  else
    {
      node->kind = CCW_PAT_OP;
      snprintf (node->text, sizeof (node->text), "%s", source->val);
    }
  return id;
}

static int
parse_list (pstate *s, const sexp_t *source)
{
  const sexp_t *head = child_at (source, 0);
  size_t count = children_count (source);
  int id;
  ccw_pat_node *node;

  if (head == NULL || head->ty != SEXP_VALUE || head->val == NULL)
    {
      pfail (s, "list must start with an opcode");
      return -1;
    }
  id = alloc_node (s);
  if (id < 0)
    return -1;
  node = &s->pat->nodes[id];

  if (strcmp (head->val, "iconst") == 0)
    {
      const sexp_t *arg = child_at (source, 1);
      int64_t value;
      if (count != 2 || arg == NULL || arg->ty != SEXP_VALUE
          || arg->val == NULL)
        {
          pfail (s, "iconst needs exactly one integer or ?var");
          return -1;
        }
      if (arg->val[0] == '?')
        {
          node->kind = CCW_PAT_CONST_VAR;
          snprintf (node->text, sizeof (node->text), "%s", arg->val);
        }
      else if (parse_integer (arg->val, &value))
        {
          node->kind = CCW_PAT_CONST;
          node->value = value;
        }
      else
        {
          pfail (s, "iconst needs an integer or ?var");
          return -1;
        }
      return id;
    }

  node->kind = CCW_PAT_OP;
  snprintf (node->text, sizeof (node->text), "%s", head->val);
  for (size_t i = 1; i < count; i++)
    {
      int child = parse_node (s, child_at (source, i));
      if (child < 0 || s->failed)
        return -1;
      if (node->nchildren >= CCW_PAT_MAX_CHILDREN)
        {
          pfail (s, "too many operands in pattern");
          return -1;
        }
      node->children[node->nchildren++] = child;
    }
  return id;
}

static int
parse_node (pstate *s, const sexp_t *source)
{
  if (source == NULL)
    {
      pfail (s, "missing pattern node");
      return -1;
    }
  return source->ty == SEXP_LIST ? parse_list (s, source)
                                 : parse_atom (s, source);
}

bool
ccw_pattern_parse (const char *text, ccw_pattern *out, char *reason,
                   size_t reason_size)
{
  char *input;
  size_t length;
  sexp_t *parsed;
  pstate state;
  int root;

  if (reason != NULL && reason_size != 0)
    reason[0] = '\0';
  if (text == NULL || out == NULL)
    return false;
  memset (out, 0, sizeof (*out));
  length = strlen (text);
  input = (char *)malloc (length + 1u);
  if (input == NULL)
    {
      if (reason != NULL && reason_size != 0)
        snprintf (reason, reason_size, "out of memory");
      return false;
    }
  memcpy (input, text, length + 1u);
  reset_sexp_errno ();
  parsed = parse_sexp (input, length);
  free (input);
  if (parsed == NULL)
    {
      if (reason != NULL && reason_size != 0)
        snprintf (reason, reason_size, "invalid pattern S-expression");
      sexp_cleanup ();
      return false;
    }

  state.pat = out;
  state.reason = reason;
  state.reason_size = reason_size;
  state.failed = false;
  root = parse_node (&state, parsed);
  out->root = root;
  destroy_sexp (parsed);
  sexp_cleanup ();
  return !state.failed && root >= 0;
}
