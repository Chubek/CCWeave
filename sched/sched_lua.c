#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#include "sched.h"
#include <stdio.h>

typedef struct
{
  ccw_sched *sched;
} sched_ud;
static sched_ud *
self (lua_State *L)
{
  return luaL_checkudata (L, 1, "ccw.sched");
}
static int
lua_error_from (lua_State *L, ccw_sched_error *e)
{
  return luaL_error (L, "%s", e->message);
}
static int
method_require (lua_State *L)
{
  sched_ud *u = self (L);
  ccw_sched_error e;
  uint32_t id;
  const char *kernel, *cap, *prefer;
  luaL_checktype (L, 2, LUA_TTABLE);
  lua_getfield (L, 2, "kernel");
  kernel = lua_tostring (L, -1);
  lua_pop (L, 1);
  lua_getfield (L, 2, "capability");
  cap = lua_tostring (L, -1);
  lua_pop (L, 1);
  lua_getfield (L, 2, "prefer");
  prefer = lua_tostring (L, -1);
  lua_pop (L, 1);
  if ((kernel == NULL) == (cap == NULL))
    return luaL_error (L,
                       "require expects exactly one of kernel or capability");
  if (!(kernel
            ? ccw_sched_require_kernel (u->sched, kernel, &id, &e)
            : ccw_sched_require_capability (u->sched, cap, prefer, &id, &e)))
    return lua_error_from (L, &e);
  lua_pushinteger (L, (lua_Integer)id);
  return 1;
}
static int
method_probe (lua_State *L)
{
  sched_ud *u = self (L);
  uint32_t id;
  const char *cap, *prefer;
  luaL_checktype (L, 2, LUA_TTABLE);
  lua_getfield (L, 2, "capability");
  cap = lua_tostring (L, -1);
  lua_pop (L, 1);
  lua_getfield (L, 2, "prefer");
  prefer = lua_tostring (L, -1);
  lua_pop (L, 1);
  if (!cap)
    return luaL_error (L, "probe currently accepts capability");
  if (!ccw_sched_probe_capability (u->sched, cap, prefer, &id))
    {
      lua_pushnil (L);
      return 1;
    }
  lua_pushinteger (L, (lua_Integer)id);
  return 1;
}
static int
method_rewrite (lua_State *L)
{
  sched_ud *u = self (L);
  ccw_sched_error e;
  uint32_t id;
  if (!ccw_sched_rewrite (u->sched, luaL_checkstring (L, 2), &id, &e))
    return lua_error_from (L, &e);
  lua_pushinteger (L, (lua_Integer)id);
  return 1;
}
static int
method_edge (lua_State *L)
{
  sched_ud *u = self (L);
  ccw_sched_error e;
  if (!ccw_sched_edge (u->sched, (uint32_t)luaL_checkinteger (L, 2),
                       (uint32_t)luaL_checkinteger (L, 3), &e))
    return lua_error_from (L, &e);
  return 0;
}
static int
method_barrier (lua_State *L)
{
  sched_ud *u = self (L);
  ccw_sched_error e;
  uint32_t id;
  if (!ccw_sched_barrier (u->sched, luaL_optstring (L, 2, "barrier"), &id, &e))
    return lua_error_from (L, &e);
  lua_pushinteger (L, (lua_Integer)id);
  return 1;
}
static int
method_seal (lua_State *L)
{
  sched_ud *u = self (L);
  ccw_sched_error e;
  ccw_plan *p;
  if (!ccw_sched_seal (u->sched, &p, &e))
    return lua_error_from (L, &e);
  lua_pushstring (L, ccw_plan_text (p));
  ccw_plan_free (p);
  return 1;
}
static int
method_gc (lua_State *L)
{
  sched_ud *u = self (L);
  ccw_sched_free (u->sched);
  u->sched = NULL;
  return 0;
}
static int
sched_new (lua_State *L)
{
  const char *name = luaL_checkstring (L, 1), *dir;
  ccw_sched_error e;
  sched_ud *u;
  lua_getfield (L, lua_upvalueindex (1), "_manifest_dir");
  dir = lua_tostring (L, -1);
  lua_pop (L, 1);
  u = lua_newuserdatauv (L, sizeof (*u), 0);
  u->sched = ccw_sched_new (name, dir, &e);
  if (!u->sched)
    return luaL_error (L, "%s", e.message);
  luaL_getmetatable (L, "ccw.sched");
  lua_setmetatable (L, -2);
  return 1;
}
static void
set_error (ccw_sched_error *e, const char *msg)
{
  if (e)
    {
      e->code = 9;
      snprintf (e->message, sizeof (e->message), "%s",
                msg ? msg : "Lua execution error");
    }
}
int
ccw_sched_run_script (const char *script, const char *dir, ccw_plan **out,
                      ccw_sched_error *e)
{
  lua_State *L;
  const char *result;
  if (!script || !out)
    {
      set_error (e, "invalid script request");
      return 0;
    }
  *out = NULL;
  L = luaL_newstate ();
  if (!L)
    {
      set_error (e, "cannot create Lua state");
      return 0;
    }
  luaL_requiref (L, LUA_GNAME, luaopen_base, 1);
  lua_pop (L, 1);
  luaL_requiref (L, LUA_STRLIBNAME, luaopen_string, 1);
  lua_pop (L, 1);
  luaL_requiref (L, LUA_TABLIBNAME, luaopen_table, 1);
  lua_pop (L, 1);
  luaL_requiref (L, LUA_MATHLIBNAME, luaopen_math, 1);
  lua_pop (L, 1);
  lua_pushnil (L);
  lua_setglobal (L, "dofile");
  lua_pushnil (L);
  lua_setglobal (L, "load");
  lua_pushnil (L);
  lua_setglobal (L, "loadfile");
  lua_getglobal (L, "math");
  lua_pushnil (L);
  lua_setfield (L, -2, "random");
  lua_pop (L, 1);
  luaL_newmetatable (L, "ccw.sched");
  lua_newtable (L);
  lua_pushcfunction (L, method_require);
  lua_setfield (L, -2, "require");
  lua_pushcfunction (L, method_probe);
  lua_setfield (L, -2, "probe");
  lua_pushcfunction (L, method_rewrite);
  lua_setfield (L, -2, "rewrite");
  lua_pushcfunction (L, method_edge);
  lua_setfield (L, -2, "edge");
  lua_pushcfunction (L, method_barrier);
  lua_setfield (L, -2, "barrier");
  lua_pushcfunction (L, method_seal);
  lua_setfield (L, -2, "seal");
  lua_setfield (L, -2, "__index");
  lua_pushcfunction (L, method_gc);
  lua_setfield (L, -2, "__gc");
  lua_pop (L, 1);
  lua_newtable (L);
  lua_pushstring (L, dir ? dir : "manifests");
  lua_setfield (L, -2, "_manifest_dir");
  lua_pushvalue (L, -1);
  lua_pushcclosure (L, sched_new, 1);
  lua_setfield (L, -2, "new");
  lua_setglobal (L, "sched");
  if (luaL_loadfile (L, script) != LUA_OK || lua_pcall (L, 0, 1, 0) != LUA_OK)
    {
      set_error (e, lua_tostring (L, -1));
      lua_close (L);
      return 0;
    }
  result = lua_tostring (L, -1);
  if (!result)
    {
      set_error (e, "script must return S:seal()");
      lua_close (L);
      return 0;
    }
  *out = ccw_plan_from_text (result);
  lua_close (L);
  if (!*out)
    {
      set_error (e, "out of memory");
      return 0;
    }
  return 1;
}
