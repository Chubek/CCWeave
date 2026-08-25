#include "ast.h"

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
free_node (ccwmk_ast_node_t *node)
{
  if (!node)
    return;
  free (node->path);
  switch (node->kind)
    {
    case CCWMK_AST_VARIABLE:
      free (node->data.variable.name);
      free (node->data.variable.value);
      break;
    case CCWMK_AST_RULE:
      free (node->data.rule.target);
      for (size_t i = 0; i < node->data.rule.prerequisite_count; i++)
        free (node->data.rule.prerequisites[i]);
      for (size_t i = 0; i < node->data.rule.recipe_count; i++)
        free (node->data.rule.recipes[i]);
      free (node->data.rule.prerequisites);
      free (node->data.rule.recipes);
      break;
    case CCWMK_AST_TARGET:
      free (node->data.target.kind);
      free (node->data.target.name);
      for (size_t i = 0; i < node->data.target.attr_count; i++)
        {
          free (node->data.target.attrs[i].key);
          free (node->data.target.attrs[i].value);
        }
      free (node->data.target.attrs);
      break;
    case CCWMK_AST_FETCH:
      free (node->data.fetch.name);
      free (node->data.fetch.from);
      free (node->data.fetch.version);
      free (node->data.fetch.ref);
      break;
    case CCWMK_AST_PLUGIN:
      free (node->data.plugin.name);
      free (node->data.plugin.path);
      break;
    case CCWMK_AST_INCLUDE:
      free (node->data.include.path);
      break;
    case CCWMK_AST_CONDITIONAL:
      free (node->data.conditional.raw);
      free (node->data.conditional.lhs);
      free (node->data.conditional.rhs);
      break;
    case CCWMK_AST_LUA:
      free (node->data.lua.body);
      break;
    }
}

static int
ensure_capacity (ccwmk_ast_t *ast, size_t need)
{
  if (ast->node_capacity >= need)
    return 1;
  size_t next = ast->node_capacity ? ast->node_capacity * 2 : 8;
  while (next < need)
    next *= 2;
  void *p = realloc (ast->nodes, next * sizeof (*ast->nodes));
  if (!p)
    return 0;
  ast->nodes = (ccwmk_ast_node_t *)p;
  ast->node_capacity = next;
  return 1;
}

static ccwmk_ast_node_t
make_node (ccwmk_ast_kind_t kind, const char *path, size_t line)
{
  ccwmk_ast_node_t node;
  memset (&node, 0, sizeof (node));
  node.kind = kind;
  node.path = ccwmk_strdup (path);
  node.line = line;
  return node;
}

ccwmk_ast_t *
ccwmk_ast_new (void)
{
  return (ccwmk_ast_t *)calloc (1, sizeof (ccwmk_ast_t));
}

void
ccwmk_ast_free (ccwmk_ast_t *ast)
{
  if (!ast)
    return;
  for (size_t i = 0; i < ast->node_count; i++)
    free_node (&ast->nodes[i]);
  free (ast->nodes);
  free (ast);
}

ccwmk_ast_node_t *
ccwmk_ast_append_node (ccwmk_ast_t *ast, const ccwmk_ast_node_t *node)
{
  if (!ast || !node)
    return NULL;
  if (!ensure_capacity (ast, ast->node_count + 1))
    return NULL;
  ast->nodes[ast->node_count] = *node;
  return &ast->nodes[ast->node_count++];
}

ccwmk_ast_node_t
ccwmk_ast_make_variable (const char *path, size_t line, const char *name,
                         const char *value, int simple, int append)
{
  ccwmk_ast_node_t node = make_node (CCWMK_AST_VARIABLE, path, line);
  node.data.variable.name = ccwmk_strdup (name);
  node.data.variable.value = ccwmk_strdup (value);
  node.data.variable.simple = simple;
  node.data.variable.append = append;
  return node;
}

ccwmk_ast_node_t
ccwmk_ast_make_rule (const char *path, size_t line, const char *target)
{
  ccwmk_ast_node_t node = make_node (CCWMK_AST_RULE, path, line);
  node.data.rule.target = ccwmk_strdup (target);
  return node;
}

ccwmk_ast_node_t
ccwmk_ast_make_target (const char *path, size_t line, const char *kind,
                       const char *name)
{
  ccwmk_ast_node_t node = make_node (CCWMK_AST_TARGET, path, line);
  node.data.target.kind = ccwmk_strdup (kind);
  node.data.target.name = ccwmk_strdup (name);
  return node;
}

ccwmk_ast_node_t
ccwmk_ast_make_fetch (const char *path, size_t line, const char *name)
{
  ccwmk_ast_node_t node = make_node (CCWMK_AST_FETCH, path, line);
  node.data.fetch.name = ccwmk_strdup (name);
  return node;
}

ccwmk_ast_node_t
ccwmk_ast_make_plugin (const char *path, size_t line, const char *name)
{
  ccwmk_ast_node_t node = make_node (CCWMK_AST_PLUGIN, path, line);
  node.data.plugin.name = ccwmk_strdup (name);
  return node;
}

ccwmk_ast_node_t
ccwmk_ast_make_include (const char *path, size_t line, const char *include_path)
{
  ccwmk_ast_node_t node = make_node (CCWMK_AST_INCLUDE, path, line);
  node.data.include.path = ccwmk_strdup (include_path);
  return node;
}

ccwmk_ast_node_t
ccwmk_ast_make_conditional (const char *path, size_t line, const char *raw)
{
  ccwmk_ast_node_t node = make_node (CCWMK_AST_CONDITIONAL, path, line);
  node.data.conditional.raw = ccwmk_strdup (raw);
  return node;
}

ccwmk_ast_node_t
ccwmk_ast_make_lua (const char *path, size_t line, const char *body)
{
  ccwmk_ast_node_t node = make_node (CCWMK_AST_LUA, path, line);
  node.data.lua.body = ccwmk_strdup (body);
  return node;
}

static int
append_kv (ccwmk_ast_kv_t **items, size_t *count, const char *key,
           const char *value)
{
  size_t next = *count + 1;
  ccwmk_ast_kv_t *kv = (ccwmk_ast_kv_t *)realloc (*items, next * sizeof (*kv));
  if (!kv)
    return 0;
  *items = kv;
  kv[*count].key = ccwmk_strdup (key);
  kv[*count].value = ccwmk_strdup (value);
  if ((key && !kv[*count].key) || (value && !kv[*count].value))
    {
      free (kv[*count].key);
      free (kv[*count].value);
      return 0;
    }
  *count = next;
  return 1;
}

static int
append_string (char ***items, size_t *count, const char *value)
{
  size_t next = *count + 1;
  char **list = (char **)realloc (*items, next * sizeof (*list));
  if (!list)
    return 0;
  *items = list;
  list[*count] = ccwmk_strdup (value);
  if (value && !list[*count])
    return 0;
  *count = next;
  return 1;
}

int
ccwmk_ast_add_attr (ccwmk_ast_node_t *node, const char *key, const char *value)
{
  if (!node || node->kind != CCWMK_AST_TARGET)
    return 0;
  return append_kv (&node->data.target.attrs, &node->data.target.attr_count,
                    key, value);
}

int
ccwmk_ast_add_prerequisite (ccwmk_ast_node_t *node, const char *value)
{
  if (!node || node->kind != CCWMK_AST_RULE)
    return 0;
  return append_string (&node->data.rule.prerequisites,
                        &node->data.rule.prerequisite_count, value);
}

int
ccwmk_ast_add_recipe_line (ccwmk_ast_node_t *node, const char *value)
{
  if (!node || node->kind != CCWMK_AST_RULE)
    return 0;
  return append_string (&node->data.rule.recipes,
                        &node->data.rule.recipe_count, value);
}
