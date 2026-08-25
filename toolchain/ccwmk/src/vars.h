#ifndef CCW_CCMWK_VARS_H
#define CCW_CCMWK_VARS_H

#include <stddef.h>

typedef struct ccwmk_var_context ccwmk_var_context;

typedef struct ccwmk_auto_vars_t
{
  const char *target;
  const char *first_prereq;
  const char *all_prereqs;
} ccwmk_auto_vars_t;

ccwmk_var_context *ccwmk_var_context_new (void);
void ccwmk_var_context_free (ccwmk_var_context *ctx);
int ccwmk_var_context_set_recursive (ccwmk_var_context *ctx,
                                     const char *name, const char *value);
int ccwmk_var_context_set_simple (ccwmk_var_context *ctx, const char *name,
                                  const char *value);
const char *ccwmk_var_context_get (const ccwmk_var_context *ctx,
                                   const char *name);
int ccwmk_expand (const ccwmk_var_context *ctx, const char *input,
                  const ccwmk_auto_vars_t *auto_vars, char **out,
                  char **error_message);

#endif
