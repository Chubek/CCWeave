#include "vars.h"

#include <stdlib.h>
#include <string.h>

typedef struct
{
  char *name;
  char *value;
  int simple;
} ccwmk_var_entry_t;

struct ccwmk_var_context
{
  ccwmk_var_entry_t *entries;
  size_t count;
  size_t capacity;
};

typedef struct
{
  char *data;
  size_t len;
  size_t cap;
} ccwmk_buf_t;

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

static int
buf_reserve (ccwmk_buf_t *buf, size_t need)
{
  if (buf->cap >= need)
    return 1;
  size_t next = buf->cap ? buf->cap * 2 : 64;
  while (next < need)
    next *= 2;
  char *data = (char *)realloc (buf->data, next);
  if (!data)
    return 0;
  buf->data = data;
  buf->cap = next;
  return 1;
}

static int
buf_append_n (ccwmk_buf_t *buf, const char *s, size_t n)
{
  if (!buf_reserve (buf, buf->len + n + 1))
    return 0;
  memcpy (buf->data + buf->len, s, n);
  buf->len += n;
  buf->data[buf->len] = '\0';
  return 1;
}

static int
buf_append (ccwmk_buf_t *buf, const char *s)
{
  return s ? buf_append_n (buf, s, strlen (s)) : 1;
}

static ccwmk_var_entry_t *
find_entry (ccwmk_var_context *ctx, const char *name)
{
  if (!ctx || !name)
    return NULL;
  for (size_t i = 0; i < ctx->count; i++)
    if (!strcmp (ctx->entries[i].name, name))
      return &ctx->entries[i];
  return NULL;
}

static const ccwmk_var_entry_t *
find_entry_const (const ccwmk_var_context *ctx, const char *name)
{
  return find_entry ((ccwmk_var_context *)ctx, name);
}

static int
ensure_capacity (ccwmk_var_context *ctx, size_t need)
{
  if (ctx->capacity >= need)
    return 1;
  size_t next = ctx->capacity ? ctx->capacity * 2 : 8;
  while (next < need)
    next *= 2;
  void *p = realloc (ctx->entries, next * sizeof (*ctx->entries));
  if (!p)
    return 0;
  ctx->entries = (ccwmk_var_entry_t *)p;
  ctx->capacity = next;
  return 1;
}

static int
set_entry (ccwmk_var_context *ctx, const char *name, const char *value,
           int simple)
{
  if (!ctx || !name)
    return 0;
  ccwmk_var_entry_t *entry = find_entry (ctx, name);
  if (!entry)
    {
      if (!ensure_capacity (ctx, ctx->count + 1))
        return 0;
      entry = &ctx->entries[ctx->count++];
      memset (entry, 0, sizeof (*entry));
      char *name_copy = ccwmk_strdup (name);
      char *value_copy = ccwmk_strdup (value ? value : "");
      if (!name_copy || !value_copy)
        {
          free (name_copy);
          free (value_copy);
          ctx->count--;
          return 0;
        }
      entry->name = name_copy;
      entry->value = value_copy;
      entry->simple = simple;
      return 1;
    }
  char *value_copy = ccwmk_strdup (value ? value : "");
  if (!value_copy)
    return 0;
  free (entry->value);
  entry->value = value_copy;
  entry->simple = simple;
  return 1;
}

ccwmk_var_context *
ccwmk_var_context_new (void)
{
  return (ccwmk_var_context *)calloc (1, sizeof (ccwmk_var_context));
}

void
ccwmk_var_context_free (ccwmk_var_context *ctx)
{
  if (!ctx)
    return;
  for (size_t i = 0; i < ctx->count; i++)
    {
      free (ctx->entries[i].name);
      free (ctx->entries[i].value);
    }
  free (ctx->entries);
  free (ctx);
}

int
ccwmk_var_context_set_recursive (ccwmk_var_context *ctx, const char *name,
                                 const char *value)
{
  return set_entry (ctx, name, value, 0);
}

int
ccwmk_var_context_set_simple (ccwmk_var_context *ctx, const char *name,
                              const char *value)
{
  char *expanded = NULL;
  char *error_message = NULL;
  int ok = ccwmk_expand (ctx, value, NULL, &expanded, &error_message);
  free (error_message);
  if (!ok)
    {
      free (expanded);
      return 0;
    }
  ok = set_entry (ctx, name, expanded ? expanded : "", 1);
  free (expanded);
  return ok;
}

const char *
ccwmk_var_context_get (const ccwmk_var_context *ctx, const char *name)
{
  const ccwmk_var_entry_t *entry = find_entry_const (ctx, name);
  return entry ? entry->value : NULL;
}

static int expand_impl (const ccwmk_var_context *ctx, const char *input,
                        const ccwmk_auto_vars_t *auto_vars, ccwmk_buf_t *out,
                        char **error_message, const char **stack,
                        size_t depth);

static int
expand_var_ref (const ccwmk_var_context *ctx, const char *name,
                const ccwmk_auto_vars_t *auto_vars, ccwmk_buf_t *out,
                char **error_message, const char **stack, size_t depth)
{
  if (!name || !*name)
    return 1;
  if (strcmp (name, "@") == 0)
    return buf_append (out, auto_vars && auto_vars->target ? auto_vars->target : "");
  if (strcmp (name, "<") == 0)
    return buf_append (out, auto_vars && auto_vars->first_prereq ? auto_vars->first_prereq : "");
  if (strcmp (name, "^") == 0)
    return buf_append (out, auto_vars && auto_vars->all_prereqs ? auto_vars->all_prereqs : "");

  const ccwmk_var_entry_t *entry = find_entry_const (ctx, name);
  if (!entry)
    return 1;
  for (size_t i = 0; i < depth; i++)
    if (stack[i] && !strcmp (stack[i], name))
      {
        set_error (error_message, "ccwmk: recursive variable expansion");
        return 0;
      }
  if (!entry->simple)
    {
      const char *next_stack[32];
      if (depth >= sizeof (next_stack) / sizeof (next_stack[0]))
        {
          set_error (error_message, "ccwmk: variable expansion too deep");
          return 0;
        }
      for (size_t i = 0; i < depth; i++)
        next_stack[i] = stack[i];
      next_stack[depth] = name;
      return expand_impl (ctx, entry->value, auto_vars, out, error_message,
                          next_stack, depth + 1);
    }
  return buf_append (out, entry->value);
}

static int
expand_impl (const ccwmk_var_context *ctx, const char *input,
             const ccwmk_auto_vars_t *auto_vars, ccwmk_buf_t *out,
             char **error_message, const char **stack, size_t depth)
{
  if (!input)
    return 1;
  for (size_t i = 0; input[i]; i++)
    {
      if (input[i] != '$')
        {
          if (!buf_append_n (out, input + i, 1))
            {
              set_error (error_message, "ccwmk: out of memory");
              return 0;
            }
          continue;
        }
      i++;
      if (!input[i])
        return buf_append_n (out, "$", 1);
      if (input[i] == '$')
        {
          if (!buf_append_n (out, "$", 1))
            {
              set_error (error_message, "ccwmk: out of memory");
              return 0;
            }
          continue;
        }
      if (input[i] == '(' || input[i] == '{')
        {
          char close = input[i] == '(' ? ')' : '}';
          size_t start = ++i;
          while (input[i] && input[i] != close)
            i++;
          if (!input[i])
            {
              set_error (error_message, "ccwmk: unterminated variable reference");
              return 0;
            }
          size_t len = i - start;
          char name[256];
          if (len >= sizeof (name))
            {
              set_error (error_message, "ccwmk: variable name too long");
              return 0;
            }
          memcpy (name, input + start, len);
          name[len] = '\0';
          if (!expand_var_ref (ctx, name, auto_vars, out, error_message, stack,
                               depth))
            return 0;
          continue;
        }
      if (!expand_var_ref (ctx, (char[]){input[i], '\0'}, auto_vars, out,
                           error_message, stack, depth))
        return 0;
    }
  return 1;
}

int
ccwmk_expand (const ccwmk_var_context *ctx, const char *input,
              const ccwmk_auto_vars_t *auto_vars, char **out,
              char **error_message)
{
  if (!out)
    {
      set_error (error_message, "ccwmk: invalid expansion request");
      return 0;
    }
  *out = NULL;
  ccwmk_buf_t buf = { 0, 0, 0 };
  if (!expand_impl (ctx, input, auto_vars, &buf, error_message, NULL, 0))
    {
      free (buf.data);
      return 0;
    }
  if (!buf_reserve (&buf, buf.len + 1))
    {
      free (buf.data);
      set_error (error_message, "ccwmk: out of memory");
      return 0;
    }
  buf.data[buf.len] = '\0';
  *out = buf.data ? buf.data : ccwmk_strdup ("");
  if (!*out)
    {
      free (buf.data);
      set_error (error_message, "ccwmk: out of memory");
      return 0;
    }
  return 1;
}
