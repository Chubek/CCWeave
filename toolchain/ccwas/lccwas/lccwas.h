#ifndef CCW_LCCWAS_H
#define CCW_LCCWAS_H

#include <stddef.h>
#include <stdio.h>

#include "lua.h"

typedef struct {
  char *data;
  size_t len, cap;
  char *file;
  const char *arch;
  const char *syntax;
  int unsafe;
  unsigned gensym;
  int in_macro;
  int sealed;
  const char *include_stack[32];
  int include_depth;
  char **env_keys;
  char **env_vals;
  size_t env_count;
  lua_State *L;
} ccw_lccwas;

void ccw_lccwas_init(ccw_lccwas *c, const char *arch, const char *syntax,
                     const char *file, int unsafe);
void ccw_lccwas_destroy(ccw_lccwas *c);
int ccw_lccwas_define(ccw_lccwas *c, const char *key, const char *value);
int ccw_lccwas_expand_file(ccw_lccwas *c, const char *path, char **error);
int ccw_lccwas_expand_buffer(ccw_lccwas *c, const char *src, const char *file,
                             char **error);
char *ccw_lccwas_take_buffer(ccw_lccwas *c);
void ccw_lccwas_seal(ccw_lccwas *c);
lua_State *ccw_lccwas_state(ccw_lccwas *c);

#endif
