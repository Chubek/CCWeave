#include "../ccwld.h"
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
  ccwld_plan *p;
  const char *file;
} lua_ctx;
static lua_ctx *
ctx (lua_State *L)
{
  lua_getglobal (L, "ccwld");
  lua_getfield (L, -1, "_ctx");
  lua_ctx *c = (lua_ctx *)lua_touserdata (L, -1);
  lua_pop (L, 2);
  return c;
}
static int
fail (lua_State *L, ccwld_error *e)
{
  return luaL_error (L, "%s", e->message);
}
static int
output (lua_State *L)
{
  lua_ctx *c = ctx (L);
  ccwld_output o;
  ccwld_error e;
  memset (&o, 0, sizeof (o));
  luaL_checktype (L, 1, LUA_TTABLE);
  lua_getfield (L, 1, "kind");
  o.kind = strdup (luaL_checkstring (L, -1));
  lua_pop (L, 1);
  lua_getfield (L, 1, "format");
  o.format = strdup (luaL_optstring (L, -1, NULL));
  lua_pop (L, 1);
  lua_getfield (L, 1, "entry");
  o.entry = strdup (luaL_optstring (L, -1, NULL));
  lua_pop (L, 1);
  lua_getfield (L, 1, "soname");
  o.soname = strdup (luaL_optstring (L, -1, NULL));
  lua_pop (L, 1);
  int r = ccwld_plan_output (c->p, &o, &e);
  free (o.kind);
  free (o.format);
  free (o.entry);
  free (o.soname);
  if (!r)
    return fail (L, &e);
  lua_pushvalue (L, 1);
  return 1;
}
static int
input (lua_State *L)
{
  lua_ctx *c = ctx (L);
  ccwld_error e;
  int n = lua_gettop (L);
  for (int i = 1; i <= n; i++)
    if (!ccwld_plan_input (c->p, luaL_checkstring (L, i), 0, 0, &e))
      return fail (L, &e);
  return 0;
}
static int
startup (lua_State *L)
{
  lua_ctx *c = ctx (L);
  ccwld_error e;
  if (!ccwld_plan_input (c->p, luaL_checkstring (L, 1), 0, 1, &e))
    return fail (L, &e);
  return 0;
}
static int
search_path (lua_State *L)
{
  lua_ctx *c = ctx (L);
  ccwld_error e;
  int n = lua_gettop (L);
  for (int i = 1; i <= n; i++)
    if (!ccwld_plan_search_path (c->p, luaL_checkstring (L, i), &e))
      return fail (L, &e);
  return 0;
}
static int
memory (lua_State *L)
{
  lua_ctx *c = ctx (L);
  ccwld_error e;
  luaL_checktype (L, 1, LUA_TTABLE);
  lua_Integer n = luaL_len (L, 1);
  for (lua_Integer i = 1; i <= n; i++)
    {
      lua_geti (L, 1, i);
      lua_getfield (L, -1, "name");
      const char *name = luaL_checkstring (L, -1);
      lua_pop (L, 1);
      lua_getfield (L, -1, "attrs");
      const char *attrs = luaL_optstring (L, -1, "");
      lua_pop (L, 1);
      lua_getfield (L, -1, "origin");
      uint64_t o = (uint64_t)luaL_checkinteger (L, -1);
      lua_pop (L, 1);
      lua_getfield (L, -1, "length");
      uint64_t z = (uint64_t)luaL_checkinteger (L, -1);
      lua_pop (L, 2);
      if (!ccwld_plan_memory (c->p, name, attrs, o, z, &e))
        return fail (L, &e);
    }
  lua_pushvalue (L, 1);
  return 1;
}
static int
out_section (lua_State *L)
{
  lua_ctx *c = ctx (L);
  ccwld_error e;
  const char *n = luaL_checkstring (L, 1);
  luaL_checktype (L, 2, LUA_TTABLE);
  lua_getfield (L, 2, "region");
  const char *r = luaL_optstring (L, -1, NULL);
  lua_pop (L, 1);
  lua_getfield (L, 2, "align");
  uint64_t a = (uint64_t)luaL_optinteger (L, -1, 1);
  lua_pop (L, 1);
  lua_getfield (L, 2, "at_region");
  const char *at = luaL_optstring (L, -1, NULL);
  lua_pop (L, 1);
  if (!ccwld_plan_section (c->p, n, r, a, NULL, at, &e))
    return fail (L, &e);
  lua_pushvalue (L, 1);
  return 1;
}
static int
sections (lua_State *L)
{
  luaL_checktype (L, 1, LUA_TTABLE);
  return 0;
}
static int
expr_int (lua_State *L)
{
  lua_pushlightuserdata (L,
                         ccwld_expr_int ((uint64_t)luaL_checkinteger (L, 1)));
  return 1;
}
static int
expr_symbol (lua_State *L)
{
  lua_pushlightuserdata (L, ccwld_expr_symbol (luaL_checkstring (L, 1)));
  return 1;
}
static int
expr_dot (lua_State *L)
{
  lua_pushlightuserdata (L, ccwld_expr_dot ());
  return 1;
}
static int
assign (lua_State *L)
{
  lua_ctx *c = ctx (L);
  ccwld_error e;
  const char *n = luaL_checkstring (L, 1);
  ccwld_expr *x = (ccwld_expr *)lua_touserdata (L, 2);
  if (!x)
    return luaL_error (L, "assign expects expression");
  if (!ccwld_plan_symbol (c->p, n, x, 0, 0, &e))
    return fail (L, &e);
  return 0;
}
static int
provide (lua_State *L)
{
  lua_ctx *c = ctx (L);
  ccwld_error e;
  const char *n = luaL_checkstring (L, 1);
  ccwld_expr *x = (ccwld_expr *)lua_touserdata (L, 2);
  if (!x)
    return luaL_error (L, "provide expects expression");
  if (!ccwld_plan_symbol (c->p, n, x, 1, 0, &e))
    return fail (L, &e);
  return 0;
}
static int
gensym (lua_State *L)
{
  static unsigned n;
  const char *p = luaL_optstring (L, 1, "__ccwld");
  char b[256];
  snprintf (b, sizeof (b), "%s%u", p, ++n);
  lua_pushstring (L, b);
  return 1;
}
static int
include (lua_State *L)
{
  lua_ctx *c = ctx (L);
  const char *p = luaL_checkstring (L, 1);
  if (luaL_loadfile (L, p) != LUA_OK || lua_pcall (L, 0, 0, 0) != LUA_OK)
    return lua_error (L);
  (void)c;
  return 0;
}
static void
reg (lua_State *L, int (*fn) (lua_State *), const char *n)
{
  lua_pushcfunction (L, fn);
  lua_setfield (L, -2, n);
}
int
ccwld_run_lua (const char *file, const char *target, ccwld_plan **out,
               ccwld_error *e)
{
  lua_State *L = luaL_newstate ();
  if (!L)
    {
      if (e)
        snprintf (e->message, sizeof (e->message), "cannot create Lua state");
      return 0;
    }
  luaL_openlibs (L);
  lua_getglobal (L, "math");
  lua_pushnil (L);
  lua_setfield (L, -2, "random");
  lua_pop (L, 1);
  lua_pushnil (L);
  lua_setglobal (L, "io");
  lua_pushnil (L);
  lua_setglobal (L, "os");
  lua_pushnil (L);
  lua_setglobal (L, "package");
  lua_pushnil (L);
  lua_setglobal (L, "debug");
  lua_pushnil (L);
  lua_setglobal (L, "require");
  lua_pushnil (L);
  lua_setglobal (L, "dofile");
  lua_pushnil (L);
  lua_setglobal (L, "loadfile");
  lua_ctx c = { ccwld_plan_new (target), file };
  lua_newtable (L);
  int t = lua_gettop (L);
  lua_pushlightuserdata (L, &c);
  lua_setfield (L, t, "_ctx");
  reg (L, output, "output");
  reg (L, input, "input");
  reg (L, startup, "startup");
  reg (L, search_path, "search_path");
  reg (L, memory, "memory");
  reg (L, sections, "sections");
  reg (L, out_section, "out");
  reg (L, expr_int, "int");
  reg (L, expr_symbol, "symbol");
  reg (L, expr_dot, "dot");
  reg (L, assign, "assign");
  reg (L, provide, "provide");
  reg (L, gensym, "gensym");
  reg (L, include, "include");
  lua_setglobal (L, "ccwld");
  if (luaL_loadfile (L, file) != LUA_OK || lua_pcall (L, 0, 0, 0) != LUA_OK)
    {
      if (e)
        snprintf (e->message, sizeof (e->message), "%s", lua_tostring (L, -1));
      ccwld_plan_free (c.p);
      lua_close (L);
      return 0;
    }
  if (!ccwld_plan_seal (c.p, e))
    {
      ccwld_plan_free (c.p);
      lua_close (L);
      return 0;
    }
  *out = c.p;
  lua_close (L);
  return 1;
}
int
ccwld_run_script (const char *script, const char *target,
                  const char *output_path, ccwld_error *e)
{
  ccwld_plan *p = NULL;
  const char *dot = strrchr (script, '.');
  int ok;
  if (dot && (!strcmp (dot, ".lua")))
    {
      ok = ccwld_run_lua (script, target, &p, e);
    }
  else
    {
      if (e)
        snprintf (e->message, sizeof (e->message),
                  "ld-script frontend is unavailable for '%s'", script);
      return 0;
    }
  if (!ok)
    return 0;
  ok = ccwld_link_run (p, output_path, e);
  ccwld_plan_free (p);
  return ok;
}
