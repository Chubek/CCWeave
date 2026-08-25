#include "../src/vars.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
check (int ok)
{
  assert (ok);
}

static void
expect_expand (ccwmk_var_context *ctx, const char *input,
               const ccwmk_auto_vars_t *auto_vars, const char *expected)
{
  char *out = NULL;
  char *error_message = NULL;
  check (ccwmk_expand (ctx, input, auto_vars, &out, &error_message));
  assert (!error_message);
  assert (out);
  assert (!strcmp (out, expected));
  free (out);
}

int
main (void)
{
  ccwmk_var_context *ctx = ccwmk_var_context_new ();
  assert (ctx);

  check (ccwmk_var_context_set_recursive (ctx, "A", "x"));
  check (ccwmk_var_context_set_recursive (ctx, "B", "$(A) y"));
  expect_expand (ctx, "$(B)", NULL, "x y");
  check (ccwmk_var_context_set_recursive (ctx, "A", "z"));
  expect_expand (ctx, "$(B)", NULL, "z y");

  check (ccwmk_var_context_set_simple (ctx, "C", "$(A) q"));
  check (ccwmk_var_context_set_recursive (ctx, "A", "k"));
  expect_expand (ctx, "$(C)", NULL, "z q");

  ccwmk_auto_vars_t autos = { "out.o", "in.c", "in.c dep.h" };
  expect_expand (ctx, "$@:$<:$^:$$", &autos, "out.o:in.c:in.c dep.h:$");

  char *out = NULL;
  char *error_message = NULL;
  check (ccwmk_var_context_set_recursive (ctx, "R", "$(R)"));
  assert (!ccwmk_expand (ctx, "$(R)", NULL, &out, &error_message));
  assert (error_message);
  free (error_message);

  ccwmk_var_context_free (ctx);
  return 0;
}
