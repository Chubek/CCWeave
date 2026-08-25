#ifndef CCW_CCMWK_AST_H
#define CCW_CCMWK_AST_H

#include <stddef.h>

typedef enum ccwmk_ast_kind_t
{
  CCWMK_AST_VARIABLE,
  CCWMK_AST_RULE,
  CCWMK_AST_TARGET,
  CCWMK_AST_FETCH,
  CCWMK_AST_PLUGIN,
  CCWMK_AST_INCLUDE,
  CCWMK_AST_CONDITIONAL,
  CCWMK_AST_LUA
} ccwmk_ast_kind_t;

typedef struct ccwmk_ast_kv_t
{
  char *key;
  char *value;
} ccwmk_ast_kv_t;

typedef struct ccwmk_ast_node_t
{
  ccwmk_ast_kind_t kind;
  char *path;
  size_t line;
  union
  {
    struct
    {
      char *name;
      char *value;
      int simple;
      int append;
    } variable;
    struct
    {
      char *target;
      char **prerequisites;
      size_t prerequisite_count;
      char **recipes;
      size_t recipe_count;
      int phony;
      int pattern;
    } rule;
    struct
    {
      char *kind;
      char *name;
      ccwmk_ast_kv_t *attrs;
      size_t attr_count;
    } target;
    struct
    {
      char *name;
      char *from;
      char *version;
      char *ref;
    } fetch;
    struct
    {
      char *name;
      char *path;
    } plugin;
    struct
    {
      char *path;
    } include;
    struct
    {
      char *raw;
      char *lhs;
      char *rhs;
    } conditional;
    struct
    {
      char *body;
    } lua;
  } data;
} ccwmk_ast_node_t;

typedef struct ccwmk_ast_t
{
  ccwmk_ast_node_t *nodes;
  size_t node_count;
  size_t node_capacity;
} ccwmk_ast_t;

ccwmk_ast_t *ccwmk_ast_new (void);
void ccwmk_ast_free (ccwmk_ast_t *ast);
ccwmk_ast_node_t *ccwmk_ast_append_node (ccwmk_ast_t *ast,
                                        const ccwmk_ast_node_t *node);
ccwmk_ast_node_t ccwmk_ast_make_variable (const char *path, size_t line,
                                          const char *name,
                                          const char *value, int simple,
                                          int append);
ccwmk_ast_node_t ccwmk_ast_make_rule (const char *path, size_t line,
                                     const char *target);
ccwmk_ast_node_t ccwmk_ast_make_target (const char *path, size_t line,
                                        const char *kind,
                                        const char *name);
ccwmk_ast_node_t ccwmk_ast_make_fetch (const char *path, size_t line,
                                      const char *name);
ccwmk_ast_node_t ccwmk_ast_make_plugin (const char *path, size_t line,
                                        const char *name);
ccwmk_ast_node_t ccwmk_ast_make_include (const char *path, size_t line,
                                         const char *include_path);
ccwmk_ast_node_t ccwmk_ast_make_conditional (const char *path, size_t line,
                                             const char *raw);
ccwmk_ast_node_t ccwmk_ast_make_lua (const char *path, size_t line,
                                     const char *body);
int ccwmk_ast_add_attr (ccwmk_ast_node_t *node, const char *key,
                        const char *value);
int ccwmk_ast_add_prerequisite (ccwmk_ast_node_t *node, const char *value);
int ccwmk_ast_add_recipe_line (ccwmk_ast_node_t *node, const char *value);

#endif
