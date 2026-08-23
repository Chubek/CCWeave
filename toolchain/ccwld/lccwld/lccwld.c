/* lccwld — the Lua linker-script frontend (LCCWLD.md §1–§7).
 *
 * Builds the same link-plan IR as the mpc ld-script frontend (D-0034)
 * through the plan builders, with deferred expression objects (§4.6,
 * D-0037), phase hooks whose Lua state lives as long as the plan
 * (§4.9), the lccwas-regime sandbox (§5), and deterministic ordering
 * everywhere (§6).  Bound with the raw Lua C API (see DECISIONS.md:
 * sol2's Lua 5.5 support is unverifiable here). */
#include "../ccwld.h"
#include "../phases/ccwld_phases.h"

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LCCWLD_MAX_INCLUDE 32
#define LCCWLD_MAX_HOOKS 64

/* ================================================================
 * context
 * ================================================================ */

typedef struct
{
  char *name;          /* symbol name (or "." for a dotstep)   */
  ccwld_expr *expr;    /* owned clone of the value expression  */
  int provide;
  int hidden;
  int consumed;        /* applied by an out()/sections() call  */
  int id;              /* handle identity                      */
} lccwld_stmt;

typedef struct lccwld_rt lccwld_rt;

typedef struct
{
  ccwld_plan *p;
  ccwld_error *e;
  lccwld_rt *rt;
  const char *stack[LCCWLD_MAX_INCLUDE + 1]; /* include/-T stack */
  size_t nstack;
  lccwld_stmt *sink;   /* deferred symbol statements (§4.7)    */
  size_t nsink, csink;
  int next_stmt_id;
  int as_needed_depth;
  int output_set;
  int unsafe;
} lua_ctx;

/* hook registration (the Lua state outlives phase 0) */
typedef struct
{
  lua_State *L;
  int ref; /* registry reference to the Lua function */
} lccwld_hook;

struct lccwld_rt
{
  lua_State *L;
  lua_ctx ctx;
  lccwld_hook hooks[LCCWLD_MAX_HOOKS];
  size_t nhooks;
};

static lua_ctx *
ctx (lua_State *L)
{
  lua_getfield (L, LUA_REGISTRYINDEX, "ccwld.lccwld.ctx");
  lua_ctx *c = (lua_ctx *)lua_touserdata (L, -1);
  lua_pop (L, 1);
  return c;
}

/* strdup that tolerates NULL (absent optional strings) */
static char *
xstrdup (const char *s)
{
  return s ? strdup (s) : NULL;
}

static void
lccwld_error (lua_State *L, ccwld_error *e)
{
  luaL_error (L, "%s", e ? e->message : "link-plan error");
}

/* current script for provenance */
static const char *
cur_file (lua_ctx *c)
{
  return c->nstack ? c->stack[c->nstack - 1] : "<script>";
}

static char *
site_here (lua_State *L, lua_ctx *c)
{
  lua_Debug ar;
  int line = 0;
  if (lua_getstack (L, 1, &ar) && lua_getinfo (L, "l", &ar))
    line = ar.currentline;
  char buf[320];
  snprintf (buf, sizeof (buf), "%s:%d", cur_file (c), line);
  return strdup (buf);
}

/* ================================================================
 * deferred expression objects (§4.6, D-0037)
 * ================================================================ */

static const char EXPR_MT[] = "ccwld.expr";

typedef struct
{
  ccwld_expr *e; /* owned */
} expr_box;

static ccwld_expr *
new_expr_ud (lua_State *L, ccwld_expr *e)
{
  expr_box *b = (expr_box *)lua_newuserdatauv (L, sizeof (expr_box), 0);
  if (!b)
    {
      ccwld_expr_free (e);
      luaL_error (L, "out of memory");
      return NULL;
    }
  b->e = e;
  luaL_getmetatable (L, EXPR_MT);
  lua_setmetatable (L, -2);
  return e;
}

static int
is_expr (lua_State *L, int idx)
{
  if (lua_type (L, idx) != LUA_TUSERDATA)
    return 0;
  if (!lua_getmetatable (L, idx))
    return 0;
  luaL_getmetatable (L, EXPR_MT);
  int ok = lua_rawequal (L, -1, -2);
  lua_pop (L, 2);
  return ok;
}

static expr_box *
get_expr (lua_State *L, int idx)
{
  return (expr_box *)luaL_checkudata (L, idx, EXPR_MT);
}

/* auto-lift: expression | number | numeric-string | name-string → owned node */
static ccwld_expr *
lift_operand (lua_State *L, int idx)
{
  if (is_expr (L, idx))
    return ccwld_expr_clone (get_expr (L, idx)->e);
  int t = lua_type (L, idx);
  if (t == LUA_TNUMBER)
    return ccwld_expr_int ((uint64_t)lua_tointeger (L, idx));
  if (t == LUA_TSTRING)
    return ccwld_expr_symbol (lua_tostring (L, idx));
  return NULL;
}

static int
push_binary_mm (lua_State *L, ccwld_op_tag op)
{
  ccwld_expr *a = lift_operand (L, 1);
  ccwld_expr *b = lift_operand (L, 2);
  if (!a || !b)
    {
      ccwld_expr_free (a);
      ccwld_expr_free (b);
      luaL_error (L, "arithmetic needs expression or number operands");
      return 0;
    }
  new_expr_ud (L, ccwld_expr_binary (op, a, b));
  return 1;
}

static int
mm_add (lua_State *L) { return push_binary_mm (L, CCWLD_OP_ADD); }
static int
mm_sub (lua_State *L) { return push_binary_mm (L, CCWLD_OP_SUB); }
static int
mm_mul (lua_State *L) { return push_binary_mm (L, CCWLD_OP_MUL); }
static int
mm_div (lua_State *L) { return push_binary_mm (L, CCWLD_OP_DIV); }
static int
mm_mod (lua_State *L) { return push_binary_mm (L, CCWLD_OP_MOD); }
static int
mm_band (lua_State *L) { return push_binary_mm (L, CCWLD_OP_AND); }
static int
mm_bor (lua_State *L) { return push_binary_mm (L, CCWLD_OP_OR); }
static int
mm_bxor (lua_State *L) { return push_binary_mm (L, CCWLD_OP_XOR); }
static int
mm_shl (lua_State *L) { return push_binary_mm (L, CCWLD_OP_SHL); }
static int
mm_shr (lua_State *L) { return push_binary_mm (L, CCWLD_OP_SHR); }
static int
mm_eq (lua_State *L) { return push_binary_mm (L, CCWLD_OP_EQ); }
static int
mm_lt (lua_State *L) { return push_binary_mm (L, CCWLD_OP_LT); }
static int
mm_le (lua_State *L) { return push_binary_mm (L, CCWLD_OP_LE); }

static int
mm_unm (lua_State *L)
{
  ccwld_expr *a = lift_operand (L, 1);
  if (!a)
    return luaL_error (L, "unary minus needs an expression operand"), 0;
  new_expr_ud (L, ccwld_expr_unary (CCWLD_OP_NEG, a));
  return 1;
}

static int
mm_bnot (lua_State *L)
{
  ccwld_expr *a = lift_operand (L, 1);
  if (!a)
    return luaL_error (L, "~ needs an expression operand"), 0;
  new_expr_ud (L, ccwld_expr_binary (CCWLD_OP_XOR, a, ccwld_expr_int (~(uint64_t)0)));
  return 1;
}

/* §4.6: reading a deferred value at phase 0 is fatal */
static int
mm_tostring (lua_State *L)
{
  (void)get_expr (L, 1);
  return luaL_error (L, "deferred expression: value not available until "
                        "layout"),
         0;
}

static int
mm_gc (lua_State *L)
{
  expr_box *b = get_expr (L, 1);
  ccwld_expr_free (b->e);
  b->e = NULL;
  return 0;
}

/* ================================================================
 * statement sink (§4.7): assign/provide defer until their consumer
 * ================================================================ */

static lccwld_stmt *
sink_add (lua_ctx *c, const char *name, ccwld_expr *owned, int provide,
          int hidden)
{
  if (c->nsink == c->csink)
    {
      size_t nc = c->csink ? c->csink * 2 : 8;
      lccwld_stmt *s = realloc (c->sink, nc * sizeof (*s));
      if (!s)
        return NULL;
      c->sink = s;
      c->csink = nc;
    }
  lccwld_stmt *s = &c->sink[c->nsink];
  s->name = strdup (name);
  s->expr = owned;
  s->provide = provide;
  s->hidden = hidden;
  s->consumed = 0;
  s->id = c->next_stmt_id++;
  if (!s->name)
    return NULL;
  c->nsink++;
  return s;
}

/* apply a statement into a section context */
static int
sink_apply (lua_ctx *c, lccwld_stmt *s, int sec_idx, const char *site)
{
  ccwld_error *e = c->e;
  if (!strcmp (s->name, "."))
    {
      if (!ccwld_plan_dotstep (c->p, s->expr, sec_idx, site, e))
        return 0;
      s->expr = NULL; /* ownership passed to the plan */
      return 1;
    }
  if (!ccwld_plan_symbol_at (c->p, s->name, s->expr, s->provide, s->hidden,
                             sec_idx, site, e))
    return 0;
  s->expr = NULL;
  return 1;
}

static void
sink_free_entry (lccwld_stmt *s)
{
  free (s->name);
  ccwld_expr_free (s->expr);
  s->expr = NULL;
}

/* the Lua handle returned by assign/provide */
static void
push_stmt_handle (lua_State *L, int id)
{
  lua_createtable (L, 0, 2);
  lua_pushboolean (L, 1);
  lua_setfield (L, -2, "__lccwld_stmt");
  lua_pushinteger (L, id);
  lua_setfield (L, -2, "__lccwld_id");
}

static lccwld_stmt *
stmt_from_handle (lua_ctx *c, lua_State *L, int idx)
{
  if (lua_type (L, idx) != LUA_TTABLE)
    return NULL;
  lua_getfield (L, idx, "__lccwld_stmt");
  int ok = lua_toboolean (L, -1);
  lua_pop (L, 1);
  if (!ok)
    return NULL;
  lua_getfield (L, idx, "__lccwld_id");
  int id = (int)lua_tointeger (L, -1);
  lua_pop (L, 1);
  for (size_t i = 0; i < c->nsink; i++)
    if (c->sink[i].id == id)
      return &c->sink[i];
  return NULL;
}

/* ================================================================
 * builder functions
 * ================================================================ */

static int
l_output (lua_State *L)
{
  lua_ctx *c = ctx (L);
  luaL_checktype (L, 1, LUA_TTABLE);
  ccwld_output o;
  memset (&o, 0, sizeof (o));

  lua_getfield (L, 1, "kind");
  o.kind = strdup (luaL_optstring (L, -1, "exe"));
  lua_pop (L, 1);
  lua_getfield (L, 1, "format");
  o.format = strdup (luaL_optstring (L, -1, "elf"));
  lua_pop (L, 1);
  lua_getfield (L, 1, "entry");
  if (is_expr (L, -1))
    {
      ccwld_expr *e = get_expr (L, -1)->e;
      if (e->kind != CCWLD_EXPR_SYMBOL || !e->name)
        {
          lua_pop (L, 1);
          free (o.kind);
          free (o.format);
          return luaL_error (L, "output.entry must be a symbol name"), 0;
        }
      o.entry = strdup (e->name);
    }
  else
    o.entry = xstrdup (luaL_optstring (L, -1, NULL));
  lua_pop (L, 1);
  lua_getfield (L, 1, "soname");
  o.soname = xstrdup (luaL_optstring (L, -1, NULL));
  lua_pop (L, 1);
  lua_getfield (L, 1, "osabi");
  o.osabi = xstrdup (luaL_optstring (L, -1, NULL));
  lua_pop (L, 1);

  /* §4.1: soname is dso-only — fatal, never silently dropped */
  if (o.soname && o.kind && strcmp (o.kind, "dso") != 0)
    {
      free (o.kind);
      free (o.format);
      free (o.entry);
      free (o.soname);
      free (o.osabi);
      return luaL_error (L, "output.soname is only valid for kind=\"dso\""),
             0;
    }

  if (!ccwld_plan_output (c->p, &o, c->e))
    {
      free (o.kind);
      free (o.format);
      free (o.entry);
      free (o.soname);
      free (o.osabi);
      return lccwld_error (L, c->e), 0;
    }
  c->output_set = 1;
  free (o.kind);
  free (o.format);
  free (o.entry);
  free (o.soname);
  free (o.osabi);
  lua_pushvalue (L, 1);
  return 1;
}

static int
l_input (lua_State *L)
{
  lua_ctx *c = ctx (L);
  int n = lua_gettop (L);
  for (int i = 1; i <= n; i++)
    if (!ccwld_plan_input (c->p, luaL_checkstring (L, i),
                           c->as_needed_depth > 0, 0, c->e))
      return lccwld_error (L, c->e), 0;
  return 0;
}

static int
l_group (lua_State *L)
{
  lua_ctx *c = ctx (L);
  luaL_checktype (L, 1, LUA_TTABLE);
  lua_Integer n = luaL_len (L, 1);
  const char *paths[256];
  if (n > 256)
    return luaL_error (L, "group: too many members"), 0;
  /* push every member so the borrowed strings stay pinned across the
   * plan call (it allocates, which can trigger GC) */
  for (lua_Integer i = 1; i <= n; i++)
    {
      lua_geti (L, 1, i);
      paths[i - 1] = luaL_checkstring (L, -1);
    }
  int ok = n
           && ccwld_plan_group (c->p, (const char **)paths, (size_t)n, c->e);
  if (n)
    lua_pop (L, (int)n);
  if (n && !ok)
    return lccwld_error (L, c->e), 0;
  lua_pushvalue (L, 1);
  return 1;
}

static int
l_as_needed (lua_State *L)
{
  lua_ctx *c = ctx (L);
  luaL_checktype (L, 1, LUA_TFUNCTION);
  c->as_needed_depth++;
  lua_pushvalue (L, 1);
  if (lua_pcall (L, 0, 0, 0) != LUA_OK)
    {
      c->as_needed_depth--;
      return lua_error (L);
    }
  c->as_needed_depth--;
  return 0;
}

static int
l_search_path (lua_State *L)
{
  lua_ctx *c = ctx (L);
  int n = lua_gettop (L);
  for (int i = 1; i <= n; i++)
    if (!ccwld_plan_search_path (c->p, luaL_checkstring (L, i), c->e))
      return lccwld_error (L, c->e), 0;
  return 0;
}

static int
l_startup (lua_State *L)
{
  lua_ctx *c = ctx (L);
  if (!ccwld_plan_input (c->p, luaL_checkstring (L, 1), 0, 1, c->e))
    return lccwld_error (L, c->e), 0;
  return 0;
}

static int
l_memory (lua_State *L)
{
  lua_ctx *c = ctx (L);
  luaL_checktype (L, 1, LUA_TTABLE);
  lua_Integer n = luaL_len (L, 1);
  for (lua_Integer i = 1; i <= n; i++)
    {
      lua_geti (L, 1, i);
      lua_getfield (L, -1, "name");
      const char *name = luaL_checkstring (L, -1);
      lua_pop (L, 1);
      lua_getfield (L, -1, "attrs");
      const char *attrs = luaL_optstring (L, -1, NULL);
      lua_pop (L, 1);
      lua_getfield (L, -1, "origin");
      uint64_t origin = (uint64_t)luaL_checkinteger (L, -1);
      lua_pop (L, 1);
      lua_getfield (L, -1, "length");
      uint64_t length = (uint64_t)luaL_checkinteger (L, -1);
      lua_pop (L, 2);
      if (!ccwld_plan_memory (c->p, name, attrs, origin, length, c->e))
        return lccwld_error (L, c->e), 0;
    }
  lua_pushvalue (L, 1);
  return 1;
}

/* selector handle: {__lccwld_sel={file=…, globs={…}, keep=…}} */
static void
push_sel_handle (lua_State *L, const char *file, int keep)
{
  int n = lua_gettop (L); /* globs table on top */
  lua_createtable (L, 0, 3);
  lua_createtable (L, 0, 3);
  lua_pushstring (L, file);
  lua_setfield (L, -2, "file");
  lua_pushboolean (L, keep);
  lua_setfield (L, -2, "keep");
  lua_pushvalue (L, n);
  lua_setfield (L, -2, "globs");
  lua_setfield (L, -2, "__lccwld_sel");
}

static int
sel_from_handle (lua_State *L, int idx, const char **file, int *keep)
{
  if (lua_type (L, idx) != LUA_TTABLE)
    return 0;
  lua_getfield (L, idx, "__lccwld_sel");
  if (!lua_istable (L, -1))
    {
      lua_pop (L, 1);
      return 0;
    }
  lua_getfield (L, -1, "file");
  *file = lua_tostring (L, -1);
  lua_pop (L, 1);
  lua_getfield (L, -1, "keep");
  *keep = lua_toboolean (L, -1);
  lua_pop (L, 1);
  lua_pop (L, 1);
  return *file != NULL;
}

/* whitespace-separated glob string → array table on the stack */
static void
push_glob_table (lua_State *L, const char *s)
{
  lua_createtable (L, 8, 0);
  char buf[128];
  size_t k = 0;
  lua_Integer idx = 1;
  for (const char *p = s;; p++)
    {
      if (*p && *p != ' ' && *p != '\t' && k + 1 < sizeof (buf))
        {
          buf[k++] = *p;
          continue;
        }
      if (k)
        {
          buf[k] = 0;
          lua_pushstring (L, buf);
          lua_seti (L, -2, idx++);
          k = 0;
        }
      if (!*p)
        break;
    }
}

static int
l_match (lua_State *L)
{
  const char *file = luaL_checkstring (L, 1);
  /* section globs: a whitespace-separated string or an array table */
  if (lua_type (L, 2) == LUA_TSTRING)
    push_glob_table (L, lua_tostring (L, 2));
  else
    {
      luaL_checktype (L, 2, LUA_TTABLE);
      lua_pushvalue (L, 2);
    }
  push_sel_handle (L, file, 0);
  return 1;
}

static int
l_keep (lua_State *L)
{
  const char *file = luaL_checkstring (L, 1);
  if (lua_type (L, 2) == LUA_TSTRING)
    push_glob_table (L, lua_tostring (L, 2));
  else
    {
      luaL_checktype (L, 2, LUA_TTABLE);
      lua_pushvalue (L, 2);
    }
  push_sel_handle (L, file, 1);
  return 1;
}

/* apply a selector handle to the named output section.  The handle's
 * tables stay on the Lua stack across the plan call so the borrowed
 * glob strings cannot be collected mid-call. */
static int
apply_selector (lua_ctx *c, lua_State *L, int idx, const char *secname)
{
  const char *file;
  int keep;
  if (!sel_from_handle (L, idx, &file, &keep))
    return -1;
  lua_getfield (L, idx, "__lccwld_sel");
  lua_getfield (L, -1, "globs");
  lua_Integer n = luaL_len (L, -1);
  if (n == 0)
    {
      lua_pop (L, 2);
      return luaL_error (L, "selector for '%s' has no section patterns", file),
             0;
    }
  const char *globs[128];
  if (n > 128)
    {
      lua_pop (L, 2);
      return luaL_error (L, "selector for '%s' has too many patterns", file),
             0;
    }
  for (lua_Integer i = 1; i <= n; i++)
    {
      lua_geti (L, -1, i);
      globs[i - 1] = lua_tostring (L, -1);
    }
  int ok = ccwld_plan_selector (c->p, secname, file,
                                (char *const *)globs, (size_t)n, keep, c->e);
  lua_pop (L, 2 + (int)n);
  if (!ok)
    return lccwld_error (L, c->e), 0;
  return 1;
}

static int
l_out (lua_State *L)
{
  lua_ctx *c = ctx (L);
  const char *name = luaL_checkstring (L, 1);
  luaL_checktype (L, 2, LUA_TTABLE);
  const char *region = NULL, *at_region = NULL;

  lua_getfield (L, 2, "region");
  region = luaL_optstring (L, -1, NULL);
  lua_pop (L, 1);
  lua_getfield (L, 2, "at_region");
  at_region = luaL_optstring (L, -1, NULL);
  lua_pop (L, 1);
  lua_getfield (L, 2, "align");
  uint64_t align = (uint64_t)luaL_optinteger (L, -1, 1);
  lua_pop (L, 1);

  if (!ccwld_plan_section (c->p, name, region, align, NULL, at_region, c->e))
    return lccwld_error (L, c->e), 0;
  int sec_idx = (int)c->p->nsecs - 1;

  lua_getfield (L, 2, "phdr");
  if (!lua_isnil (L, -1))
    {
      if (!ccwld_plan_section_set_phdr (c->p, name, lua_tostring (L, -1), c->e))
        return lccwld_error (L, c->e), 0;
    }
  lua_pop (L, 1);

  lua_getfield (L, 2, "subalign");
  if (!lua_isnil (L, -1))
    {
      if (!ccwld_plan_section_set_subalign (c->p, name,
                                            (uint64_t)lua_tointeger (L, -1),
                                            c->e))
        return lccwld_error (L, c->e), 0;
    }
  lua_pop (L, 1);

  lua_getfield (L, 2, "load");
  if (!lua_isnil (L, -1))
    {
      if (!ccwld_plan_section_set_load (c->p, name, lua_toboolean (L, -1),
                                        c->e))
        return lccwld_error (L, c->e), 0;
    }
  lua_pop (L, 1);

  lua_getfield (L, 2, "vma");
  if (!lua_isnil (L, -1))
    {
      ccwld_expr *v = lift_operand (L, -1);
      if (!v)
        return luaL_error (L, "out: vma must be an expression or number"), 0;
      if (!ccwld_plan_section_set_vma (c->p, name, v, c->e))
        return lccwld_error (L, c->e), 0;
    }
  lua_pop (L, 1);

  lua_getfield (L, 2, "at");
  if (!lua_isnil (L, -1))
    {
      ccwld_expr *v = lift_operand (L, -1);
      if (!v)
        return luaL_error (L, "out: at must be an expression or number"), 0;
      if (!ccwld_plan_section_set_at (c->p, name, v, c->e))
        return lccwld_error (L, c->e), 0;
    }
  lua_pop (L, 1);

  lua_getfield (L, 2, "fill");
  if (!lua_isnil (L, -1))
    {
      ccwld_expr *v = lift_operand (L, -1);
      if (!v)
        return luaL_error (L, "out: fill must be an expression or number"), 0;
      if (!ccwld_plan_section_set_fill (c->p, name, v, c->e))
        return lccwld_error (L, c->e), 0;
    }
  lua_pop (L, 1);

  /* input = selector handles (single or array) */
  lua_getfield (L, 2, "input");
  if (!lua_isnil (L, -1))
    {
      if (lua_istable (L, -1))
        {
          lua_Integer n = luaL_len (L, -1);
          for (lua_Integer i = 1; i <= n; i++)
            {
              lua_geti (L, -1, i);
              int r = apply_selector (c, L, lua_gettop (L), name);
              lua_pop (L, 1);
              if (r != 1)
                {
                  if (r < 0)
                    return luaL_error (L, "out: input entries must come from "
                                          "ccwld.match/ccwld.keep"),
                           0;
                  return 0;
                }
            }
          /* single handle table (not an array)?  try as one selector */
          if (n == 0 && sel_from_handle (L, lua_gettop (L),
                                         &(const char *){ 0 }, &(int){ 0 }))
            {
              int r = apply_selector (c, L, lua_gettop (L), name);
              if (r != 1)
                return r < 0 ? (luaL_error (L, "out: bad input selector"), 0)
                             : 0;
            }
        }
      else
        {
          int r = apply_selector (c, L, lua_gettop (L), name);
          if (r != 1)
            return r < 0
                       ? (luaL_error (L, "out: input must be a selector or "
                                         "list of selectors"),
                          0)
                       : 0;
        }
    }
  lua_pop (L, 1);

  /* array elements: selector handles and section-scoped statements,
   * in construction order (§4.4) */
  lua_Integer n = luaL_len (L, 2);
  for (lua_Integer i = 1; i <= n; i++)
    {
      lua_geti (L, 2, i);
      if (sel_from_handle (L, lua_gettop (L), &(const char *){ 0 },
                           &(int){ 0 }))
        {
          int r = apply_selector (c, L, lua_gettop (L), name);
          lua_pop (L, 1);
          if (r != 1)
            return r < 0 ? (luaL_error (L, "out: bad selector"), 0) : 0;
          continue;
        }
      lccwld_stmt *s = stmt_from_handle (c, L, lua_gettop (L));
      lua_pop (L, 1);
      if (s && !s->consumed)
        {
          char *site = site_here (L, c);
          int ok = sink_apply (c, s, sec_idx, site);
          free (site);
          if (!ok)
            return lccwld_error (L, c->e), 0;
          s->consumed = 1;
        }
      /* else: unknown entry — tolerated (composability) */
    }

  lua_createtable (L, 0, 2);
  lua_pushboolean (L, 1);
  lua_setfield (L, -2, "__lccwld_sec");
  lua_pushstring (L, name);
  lua_setfield (L, -2, "name");
  return 1;
}

static int
l_sections (lua_State *L)
{
  lua_ctx *c = ctx (L);
  luaL_checktype (L, 1, LUA_TTABLE);
  lua_Integer n = luaL_len (L, 1);
  for (lua_Integer i = 1; i <= n; i++)
    {
      lua_geti (L, 1, i);
      lccwld_stmt *s = stmt_from_handle (c, L, lua_gettop (L));
      lua_pop (L, 1);
      if (s && !s->consumed)
        {
          char *site = site_here (L, c);
          int ok = sink_apply (c, s, -1, site);
          free (site);
          if (!ok)
            return lccwld_error (L, c->e), 0;
          s->consumed = 1;
        }
    }
  lua_pushvalue (L, 1);
  return 1;
}

static int
l_phdrs (lua_State *L)
{
  lua_ctx *c = ctx (L);
  luaL_checktype (L, 1, LUA_TTABLE);
  lua_Integer n = luaL_len (L, 1);
  for (lua_Integer i = 1; i <= n; i++)
    {
      lua_geti (L, 1, i);
      lua_getfield (L, -1, "name");
      const char *name = luaL_checkstring (L, -1);
      lua_pop (L, 1);
      lua_getfield (L, -1, "type");
      const char *type = luaL_optstring (L, -1, "LOAD");
      lua_pop (L, 1);
      uint32_t flags = 0;
      lua_getfield (L, -1, "flags");
      if (lua_type (L, -1) == LUA_TSTRING)
        {
          for (const char *f = lua_tostring (L, -1); f && *f; f++)
            flags |= *f == 'r' ? 4 : *f == 'w' ? 2 : *f == 'x' ? 1 : 0;
        }
      else if (lua_type (L, -1) == LUA_TNUMBER)
        flags = (uint32_t)lua_tointeger (L, -1);
      lua_pop (L, 1);
      lua_getfield (L, -1, "align");
      uint64_t align = (uint64_t)luaL_optinteger (L, -1, 0x1000);
      lua_pop (L, 2);
      if (!ccwld_plan_phdr (c->p, name, type, flags, align, c->e))
        return lccwld_error (L, c->e), 0;
    }
  lua_pushvalue (L, 1);
  return 1;
}

static int
l_segments (lua_State *L)
{
  /* Mach-O segments lower onto the same phdr nodes (validated against
   * the format at seal time); Lua-side alias of phdrs (§4.5). */
  return l_phdrs (L);
}

static int
l_version (lua_State *L)
{
  lua_ctx *c = ctx (L);
  luaL_checktype (L, 1, LUA_TTABLE);
  lua_Integer n = luaL_len (L, 1);
  int first = 1;
  for (lua_Integer i = 1; i <= n; i++)
    {
      lua_geti (L, 1, i);
      lua_getfield (L, -1, "name");
      const char *ver = luaL_optstring (L, -1, NULL);
      lua_pop (L, 1);
      lua_getfield (L, -1, "default");
      int def = lua_isnil (L, -1) ? first : lua_toboolean (L, -1);
      lua_pop (L, 1);
      lua_getfield (L, -1, "globals");
      if (lua_istable (L, -1))
        {
          lua_Integer g = luaL_len (L, -1);
          for (lua_Integer k = 1; k <= g; k++)
            {
              lua_geti (L, -1, k);
              const char *sym = luaL_checkstring (L, -1);
              lua_pop (L, 1);
              if (!ccwld_plan_version (c->p, sym, ver, def, c->e))
                return lccwld_error (L, c->e), 0;
            }
        }
      lua_pop (L, 2);
      first = 0;
    }
  lua_pushvalue (L, 1);
  return 1;
}

/* --- symbols (§4.7) --- */

static int
l_symbol (lua_State *L)
{
  new_expr_ud (L, ccwld_expr_symbol (luaL_checkstring (L, 1)));
  return 1;
}

static int
assign_impl (lua_State *L, int provide, int hidden)
{
  lua_ctx *c = ctx (L);
  const char *name = luaL_checkstring (L, 1);
  ccwld_expr *v = lift_operand (L, 2);
  if (!v)
    return luaL_error (L, "assign/provide needs an expression or number"), 0;
  lccwld_stmt *s = sink_add (c, name, v, provide, hidden);
  if (!s)
    return luaL_error (L, "out of memory"), 0;
  push_stmt_handle (L, s->id);
  return 1;
}

static int
l_assign (lua_State *L) { return assign_impl (L, 0, 0); }
static int
l_provide (lua_State *L) { return assign_impl (L, 1, 0); }
static int
l_provide_hidden (lua_State *L) { return assign_impl (L, 1, 1); }

static int
l_hidden (lua_State *L)
{
  lua_ctx *c = ctx (L);
  if (!ccwld_plan_attr (c->p, luaL_checkstring (L, 1), "hidden", NULL, NULL,
                        c->e))
    return lccwld_error (L, c->e), 0;
  return 0;
}

static int
l_weaken (lua_State *L)
{
  lua_ctx *c = ctx (L);
  if (!ccwld_plan_attr (c->p, luaL_checkstring (L, 1), NULL, "weak", NULL,
                        c->e))
    return lccwld_error (L, c->e), 0;
  return 0;
}

static int
l_alias (lua_State *L)
{
  lua_ctx *c = ctx (L);
  if (!ccwld_plan_attr (c->p, luaL_checkstring (L, 1), NULL, NULL,
                        luaL_checkstring (L, 2), c->e))
    return lccwld_error (L, c->e), 0;
  return 0;
}

/* --- expressions (§4.6) --- */

static int
l_dot (lua_State *L)
{
  new_expr_ud (L, ccwld_expr_dot ());
  return 1;
}

static int
l_align (lua_State *L)
{
  ccwld_expr *a = lift_operand (L, 1);
  ccwld_expr *b = lift_operand (L, 2);
  if (!a || !b)
    {
      ccwld_expr_free (a);
      ccwld_expr_free (b);
      return luaL_error (L, "align(expr, boundary) needs two operands"), 0;
    }
  new_expr_ud (L, ccwld_expr_align_to (a, b));
  return 1;
}

static const char *
name_arg (lua_State *L, int idx)
{
  return luaL_checkstring (L, idx);
}

static int
l_addr (lua_State *L)
{
  new_expr_ud (L, ccwld_expr_addr (name_arg (L, 1)));
  return 1;
}

static int
l_loadaddr (lua_State *L)
{
  new_expr_ud (L, ccwld_expr_loadaddr (name_arg (L, 1)));
  return 1;
}

static int
l_sizeof (lua_State *L)
{
  new_expr_ud (L, ccwld_expr_sizeof (name_arg (L, 1)));
  return 1;
}

static int
l_sizeof_headers (lua_State *L)
{
  new_expr_ud (L, ccwld_expr_sizeof_headers ());
  return 1;
}

static int
l_origin (lua_State *L)
{
  new_expr_ud (L, ccwld_expr_region_origin (name_arg (L, 1)));
  return 1;
}

static int
l_length (lua_State *L)
{
  new_expr_ud (L, ccwld_expr_region_length (name_arg (L, 1)));
  return 1;
}

static int
l_max (lua_State *L)
{
  ccwld_expr *a = lift_operand (L, 1);
  ccwld_expr *b = lift_operand (L, 2);
  if (!a || !b)
    {
      ccwld_expr_free (a);
      ccwld_expr_free (b);
      return luaL_error (L, "max(a, b) needs two operands"), 0;
    }
  new_expr_ud (L, ccwld_expr_max (a, b));
  return 1;
}

static int
l_min (lua_State *L)
{
  ccwld_expr *a = lift_operand (L, 1);
  ccwld_expr *b = lift_operand (L, 2);
  if (!a || !b)
    {
      ccwld_expr_free (a);
      ccwld_expr_free (b);
      return luaL_error (L, "min(a, b) needs two operands"), 0;
    }
  new_expr_ud (L, ccwld_expr_min (a, b));
  return 1;
}

static int
l_abs (lua_State *L)
{
  ccwld_expr *a = lift_operand (L, 1);
  if (!a)
    return luaL_error (L, "abs needs an expression operand"), 0;
  new_expr_ud (L, ccwld_expr_unary (CCWLD_OP_ABS, a));
  return 1;
}

static int
l_defined (lua_State *L)
{
  new_expr_ud (L, ccwld_expr_defined (luaL_checkstring (L, 1)));
  return 1;
}

static int
l_cond (lua_State *L)
{
  ccwld_expr *a = lift_operand (L, 1);
  ccwld_expr *b = lift_operand (L, 2);
  ccwld_expr *c = lift_operand (L, 3);
  if (!a || !b || !c)
    {
      ccwld_expr_free (a);
      ccwld_expr_free (b);
      ccwld_expr_free (c);
      return luaL_error (L, "cond(c, a, b) needs three operands"), 0;
    }
  new_expr_ud (L, ccwld_expr_cond (a, b, c));
  return 1;
}

static int
l_segment_start (lua_State *L)
{
  const char *seg = luaL_checkstring (L, 1);
  ccwld_expr *d = lift_operand (L, 2);
  if (!d)
    return luaL_error (L, "segment_start(name, default) needs a default"), 0;
  new_expr_ud (L, ccwld_expr_segment_start2 (seg, d));
  return 1;
}

static int
l_fill_fn (lua_State *L)
{
  /* ccwld.fill(byteval) — usable as out(fill=…) value */
  ccwld_expr *v = lift_operand (L, 1);
  if (!v)
    return luaL_error (L, "fill needs a byte value"), 0;
  new_expr_ud (L, v);
  return 1;
}

/* --- includes / gensym / diagnostics (§4.11) --- */

static char *
read_file (const char *path, size_t *len_out)
{
  FILE *f = fopen (path, "rb");
  if (!f)
    return NULL;
  fseek (f, 0, SEEK_END);
  long n = ftell (f);
  fseek (f, 0, SEEK_SET);
  if (n < 0)
    {
      fclose (f);
      return NULL;
    }
  char *buf = malloc ((size_t)n + 1);
  if (!buf)
    {
      fclose (f);
      return NULL;
    }
  if (fread (buf, 1, (size_t)n, f) != (size_t)n)
    {
      free (buf);
      fclose (f);
      return NULL;
    }
  fclose (f);
  buf[n] = 0;
  if (len_out)
    *len_out = (size_t)n;
  return buf;
}

static int
run_chunk (lua_ctx *c, const char *path);

static int
l_include (lua_State *L)
{
  lua_ctx *c = ctx (L);
  const char *leaf = luaL_checkstring (L, 1);
  const char *ext = strrchr (leaf, '.');
  if (!ext || strcmp (ext, ".lua"))
    return luaL_error (L, "ccwld.include accepts only .lua scripts "
                          "(ld-script includes do not cross)"),
           0;
  if (c->nstack >= LCCWLD_MAX_INCLUDE)
    return luaL_error (L, "include nesting exceeds depth %d",
                       LCCWLD_MAX_INCLUDE),
           0;
  /* resolve: absolute / including-dir / search paths */
  char *cand = NULL;
  if (strchr (leaf, '/'))
    cand = strdup (leaf);
  else
    {
      const char *base = c->nstack ? c->stack[c->nstack - 1] : NULL;
      if (base)
        {
          const char *slash = strrchr (base, '/');
          if (slash)
            {
              size_t d = (size_t)(slash - base) + 1;
              cand = malloc (d + strlen (leaf) + 1);
              if (cand)
                {
                  memcpy (cand, base, d);
                  strcpy (cand + d, leaf);
                }
            }
        }
      if (!cand)
        cand = strdup (leaf);
    }
  size_t len = 0;
  char *data = read_file (cand, &len);
  if (!data)
    {
      for (size_t i = 0; i < c->p->npaths && !data; i++)
        {
          char *p2 = malloc (strlen (c->p->paths[i]) + strlen (leaf) + 2);
          if (!p2)
            continue;
          sprintf (p2, "%s/%s", c->p->paths[i], leaf);
          data = read_file (p2, &len);
          if (data)
            {
              free (cand);
              cand = p2;
            }
          else
            free (p2);
        }
    }
  if (!data)
    {
      char msg[512];
      snprintf (msg, sizeof (msg), "cannot open included script '%s'", leaf);
      free (cand);
      return luaL_error (L, "%s", msg), 0;
    }
  for (size_t i = 0; i < c->nstack; i++)
    if (!strcmp (c->stack[i], cand))
      {
        char msg[512];
        snprintf (msg, sizeof (msg), "include cycle: '%s' is already being "
                                     "processed",
                  cand);
        free (cand);
        free (data);
        return luaL_error (L, "%s", msg), 0;
      }
  c->stack[c->nstack++] = cand;
  int ok = run_chunk (c, cand); /* run_chunk reads the file itself */
  free (c->stack[--c->nstack]);
  free (data);
  if (!ok)
    {
      lua_pushstring (L, c->e->message);
      return lua_error (L);
    }
  return 0;
}

static int
l_gensym (lua_State *L)
{
  lua_ctx *c = ctx (L);
  const char *prefix = luaL_optstring (L, 1, "sym");
  char buf[256];
  ccwld_plan_gensym (c->p, prefix, buf, sizeof (buf));
  lua_pushstring (L, buf);
  return 1;
}

static int
l_error (lua_State *L)
{
  const char *msg = luaL_optstring (L, 1, "ccwld.error");
  return luaL_error (L, "%s", msg), 0;
}

static int
l_warn (lua_State *L)
{
  lua_ctx *c = ctx (L);
  char *site = site_here (L, c);
  fprintf (stderr, "ccwld: warning: %s (%s)\n",
           luaL_optstring (L, 1, ""), site);
  free (site);
  return 0;
}

static int
l_assert (lua_State *L)
{
  if (lua_toboolean (L, 1))
    {
      lua_pushvalue (L, 1);
      return 1;
    }
  if (lua_gettop (L) >= 2 && !lua_isnil (L, 2))
    {
      lua_pushvalue (L, 2);
      return lua_error (L);
    }
  return luaL_error (L, "assertion failed"), 0;
}

/* --- LTO / plugins (§4.12, D-0035: configuration only) --- */

static int
l_lto (lua_State *L)
{
  lua_ctx *c = ctx (L);
  luaL_checktype (L, 1, LUA_TTABLE);
  lua_getfield (L, 1, "enable");
  int enable = lua_isnil (L, -1) ? 1 : lua_toboolean (L, -1);
  lua_pop (L, 1);
  lua_getfield (L, 1, "jobs");
  unsigned jobs = (unsigned)luaL_optinteger (L, -1, 1);
  lua_pop (L, 1);
  lua_getfield (L, 1, "cache_dir");
  const char *cache_dir = luaL_optstring (L, -1, NULL);
  lua_pop (L, 1);
  lua_getfield (L, 1, "pipeline");
  const char *pipeline = luaL_optstring (L, -1, NULL);
  lua_pop (L, 1);
  if (enable && !pipeline)
    return luaL_error (L, "ccwld.lto needs a pipeline backend path"), 0;
  if (!ccwld_plan_lto (c->p, pipeline, jobs, cache_dir, c->e))
    return lccwld_error (L, c->e), 0;
  return 0;
}

/* tiny deterministic JSON writer for plugin options (sorted keys) */
static void
json_append (char **buf, size_t *len, size_t *cap, const char *fmt, ...)
{
  va_list ap;
  va_start (ap, fmt);
  va_list ap2;
  va_copy (ap2, ap);
  int need = vsnprintf (NULL, 0, fmt, ap);
  va_end (ap);
  if (need < 0 || *len + (size_t)need + 1 > *cap)
    {
      size_t nc = *cap * 2 + (size_t)need + 16;
      char *nb = realloc (*buf, nc);
      if (!nb)
        return;
      *buf = nb;
      *cap = nc;
    }
  *len += (size_t)vsnprintf (*buf + *len, *cap - *len, fmt, ap2);
  va_end (ap2);
}

static void
json_escape_into (char **buf, size_t *len, size_t *cap, const char *s)
{
  json_append (buf, len, cap, "\"");
  for (; *s; s++)
    {
      if ((unsigned char)*s < 0x20 || *s == '"' || *s == '\\')
        json_append (buf, len, cap, "\\u%04x", (unsigned char)*s);
      else
        json_append (buf, len, cap, "%c", *s);
    }
  json_append (buf, len, cap, "\"");
}

static int
json_write_value (lua_State *L, int idx, char **buf, size_t *len, size_t *cap,
                  int depth);

static int
json_write_table (lua_State *L, int idx, char **buf, size_t *len, size_t *cap,
                  int depth)
{
  if (depth > 8)
    {
      json_append (buf, len, cap, "null");
      return 1;
    }
  /* array part first (Lua order), then string keys sorted */
  lua_Integer n = luaL_len (L, idx);
  if (n > 0)
    {
      json_append (buf, len, cap, "[");
      for (lua_Integer i = 1; i <= n; i++)
        {
          if (i > 1)
            json_append (buf, len, cap, ",");
          lua_geti (L, idx, i);
          json_write_value (L, lua_gettop (L), buf, len, cap, depth + 1);
          lua_pop (L, 1);
        }
      json_append (buf, len, cap, "]");
      return 1;
    }
  const char *keys[64];
  size_t nkeys = 0;
  lua_pushnil (L);
  while (lua_next (L, idx) != 0)
    {
      if (lua_type (L, -2) == LUA_TSTRING && nkeys < 64)
        keys[nkeys++] = lua_tostring (L, -2);
      lua_pop (L, 1);
    }
  /* strcmp sort (deterministic, §6) */
  for (size_t i = 0; i + 1 < nkeys; i++)
    for (size_t j = i + 1; j < nkeys; j++)
      if (strcmp (keys[i], keys[j]) > 0)
        {
          const char *t = keys[i];
          keys[i] = keys[j];
          keys[j] = t;
        }
  json_append (buf, len, cap, "{");
  for (size_t i = 0; i < nkeys; i++)
    {
      if (i)
        json_append (buf, len, cap, ",");
      json_escape_into (buf, len, cap, keys[i]);
      json_append (buf, len, cap, ":");
      lua_getfield (L, idx, keys[i]);
      json_write_value (L, lua_gettop (L), buf, len, cap, depth + 1);
      lua_pop (L, 1);
    }
  json_append (buf, len, cap, "}");
  return 1;
}

static int
json_write_value (lua_State *L, int idx, char **buf, size_t *len, size_t *cap,
                  int depth)
{
  switch (lua_type (L, idx))
    {
    case LUA_TSTRING:
      json_escape_into (buf, len, cap, lua_tostring (L, idx));
      break;
    case LUA_TNUMBER:
      if (lua_isinteger (L, idx))
        json_append (buf, len, cap, "%lld",
                     (long long)lua_tointeger (L, idx));
      else
        json_append (buf, len, cap, "%g", lua_tonumber (L, idx));
      break;
    case LUA_TBOOLEAN:
      json_append (buf, len, cap, lua_toboolean (L, idx) ? "true" : "false");
      break;
    case LUA_TTABLE:
      json_write_table (L, idx, buf, len, cap, depth);
      break;
    default:
      json_append (buf, len, cap, "null");
      break;
    }
  return 1;
}

static int
l_plugin (lua_State *L)
{
  lua_ctx *c = ctx (L);
  const char *path = luaL_checkstring (L, 1);

  /* merge any pending ccwld.plugin_opt entries (explicit table wins)
   * and clear the pending set — one registration consumes them */
  lua_getfield (L, LUA_REGISTRYINDEX, "ccwld.lccwld.pending_opts");
  int have_pending = lua_istable (L, -1);
  if (!have_pending)
    lua_pop (L, 1);
  int merged_idx = 0;
  if (have_pending)
    {
      int pend = lua_gettop (L);
      if (lua_istable (L, 2))
        lua_pushvalue (L, 2);
      else
        lua_createtable (L, 0, 4);
      merged_idx = lua_gettop (L);
      lua_pushnil (L);
      while (lua_next (L, pend) != 0)
        {
          /* stack: …, pending, merged, key, value */
          lua_pushvalue (L, -2);
          lua_gettable (L, merged_idx);
          if (lua_isnil (L, -1))
            {
              lua_pop (L, 1);
              lua_pushvalue (L, -2);
              lua_pushvalue (L, -2);
              lua_settable (L, merged_idx);
            }
          else
            lua_pop (L, 1);
          lua_pop (L, 1); /* value; key stays for lua_next */
        }
    }

  char *json = NULL;
  size_t len = 0, cap = 0;
  if (merged_idx)
    json_write_table (L, merged_idx, &json, &len, &cap, 0);
  else if (lua_istable (L, 2))
    json_write_table (L, 2, &json, &len, &cap, 0);
  if (merged_idx)
    lua_pop (L, 2); /* merged + pending */

  lua_pushnil (L);
  lua_setfield (L, LUA_REGISTRYINDEX, "ccwld.lccwld.pending_opts");

  if (!ccwld_plan_plugin (c->p, path, json ? json : "{}", c->e))
    {
      free (json);
      return lccwld_error (L, c->e), 0;
    }
  free (json);
  return 0;
}

static int
l_plugin_opt (lua_State *L)
{
  lua_ctx *c = ctx (L);
  /* pending options merge into the next plugin registration */
  lua_getfield (L, LUA_REGISTRYINDEX, "ccwld.lccwld.pending_opts");
  if (!lua_istable (L, -1))
    {
      lua_pop (L, 1);
      lua_createtable (L, 0, 4);
      lua_pushvalue (L, -1);
      lua_setfield (L, LUA_REGISTRYINDEX, "ccwld.lccwld.pending_opts");
    }
  lua_pushvalue (L, 2);
  lua_setfield (L, -2, luaL_checkstring (L, 1));
  lua_pop (L, 1);
  (void)c;
  return 0;
}

/* --- hooks (§4.9) --- */

static int
phase_from_name (const char *s, ccwld_phase *ph)
{
  if (!strcmp (s, "resolved"))
    *ph = CCWLD_PHASE_RESOLVED;
  else if (!strcmp (s, "gc"))
    *ph = CCWLD_PHASE_GC;
  else if (!strcmp (s, "layout"))
    *ph = CCWLD_PHASE_LAYOUT;
  else if (!strcmp (s, "emit"))
    *ph = CCWLD_PHASE_EMIT;
  else
    return 0;
  return 1;
}

/* link-handle methods (§4.10) — closures over a ccwld_link upvalue */

static int
lh_undefined (lua_State *L)
{
  ccwld_link *lk = (ccwld_link *)lua_touserdata (L, lua_upvalueindex (1));
  size_t n = ccwld_link_undefined_count (lk);
  lua_createtable (L, (int)n, 0);
  for (size_t i = 0; i < n; i++)
    {
      lua_pushstring (L, ccwld_link_undefined (lk, i));
      lua_seti (L, -2, (lua_Integer)i + 1);
    }
  return 1;
}

static int
lh_reloc_stats (lua_State *L)
{
  ccwld_link *lk = (ccwld_link *)lua_touserdata (L, lua_upvalueindex (1));
  size_t n = ccwld_link_reloc_stat_count (lk);
  lua_createtable (L, 0, (int)n);
  for (size_t i = 0; i < n; i++)
    {
      const char *type = NULL;
      size_t count = 0;
      if (ccwld_link_reloc_stat (lk, i, &type, &count))
        {
          lua_pushinteger (L, (lua_Integer)count);
          lua_setfield (L, -2, type ? type : "?");
        }
    }
  return 1;
}

static int
lh_set_symbol (lua_State *L)
{
  ccwld_link *lk = (ccwld_link *)lua_touserdata (L, lua_upvalueindex (1));
  if (!ccwld_link_set_symbol (lk, luaL_checkstring (L, 1),
                              (uint64_t)luaL_checkinteger (L, 2),
                              CCWLD_MUT_HOOK))
    return luaL_error (L, "set_symbol failed (phase scope or unknown "
                          "symbol)"),
           0;
  return 0;
}

static int
lh_keep_section (lua_State *L)
{
  ccwld_link *lk = (ccwld_link *)lua_touserdata (L, lua_upvalueindex (1));
  if (!ccwld_link_keep_section (lk, luaL_checkstring (L, 1), CCWLD_MUT_HOOK))
    return luaL_error (L, "keep_section failed (phase scope or no live "
                          "match)"),
           0;
  return 0;
}

static int
lh_move_section (lua_State *L)
{
  ccwld_link *lk = (ccwld_link *)lua_touserdata (L, lua_upvalueindex (1));
  if (!ccwld_link_move_section (lk, (size_t)luaL_checkinteger (L, 1),
                                (size_t)luaL_checkinteger (L, 2),
                                CCWLD_MUT_HOOK))
    return luaL_error (L, "move_section failed (phase scope or bad "
                          "indexes)"),
           0;
  return 0;
}

static int
lh_add_note (lua_State *L)
{
  ccwld_link *lk = (ccwld_link *)lua_touserdata (L, lua_upvalueindex (1));
  if (!ccwld_link_add_note (lk, luaL_checkstring (L, 1),
                            luaL_checkstring (L, 2), CCWLD_MUT_HOOK))
    return luaL_error (L, "add_note is only allowed at the emit phase"), 0;
  return 0;
}

static void
push_link_handle (lua_State *L, ccwld_link *lk)
{
  lua_createtable (L, 0, 8);

  /* link.objects */
  size_t n = ccwld_link_object_count (lk);
  lua_createtable (L, (int)n, 0);
  for (size_t i = 0; i < n; i++)
    {
      ccwld_obj_view v;
      if (!ccwld_link_object (lk, i, &v))
        continue;
      lua_createtable (L, 0, 5);
      lua_pushstring (L, v.path ? v.path : "");
      lua_setfield (L, -2, "path");
      lua_pushstring (L, v.kind ? v.kind : "");
      lua_setfield (L, -2, "kind");
      lua_pushstring (L, v.format ? v.format : "");
      lua_setfield (L, -2, "format");
      lua_pushinteger (L, (lua_Integer)v.symbol_count);
      lua_setfield (L, -2, "symbols");
      lua_pushinteger (L, (lua_Integer)v.section_count);
      lua_setfield (L, -2, "sections");
      lua_seti (L, -2, (lua_Integer)i + 1);
    }
  lua_setfield (L, -2, "objects");

  /* link.sections */
  size_t sn = ccwld_link_section_count (lk);
  lua_createtable (L, (int)sn, 0);
  for (size_t i = 0; i < sn; i++)
    {
      ccwld_sec_view v;
      if (!ccwld_link_section (lk, i, &v))
        continue;
      lua_createtable (L, 0, 5);
      lua_pushstring (L, v.name ? v.name : "");
      lua_setfield (L, -2, "name");
      lua_pushinteger (L, (lua_Integer)v.addr);
      lua_setfield (L, -2, "addr");
      lua_pushinteger (L, (lua_Integer)v.size);
      lua_setfield (L, -2, "size");
      lua_pushinteger (L, (lua_Integer)v.align);
      lua_setfield (L, -2, "align");
      size_t mn = ccwld_link_section_member_count (lk, i);
      lua_createtable (L, (int)mn, 0);
      for (size_t k = 0; k < mn; k++)
        {
          lua_pushstring (L, ccwld_link_section_member (lk, i, k));
          lua_seti (L, -2, (lua_Integer)k + 1);
        }
      lua_setfield (L, -2, "members");
      lua_seti (L, -2, (lua_Integer)i + 1);
    }
  lua_setfield (L, -2, "sections");

  /* link.symbols */
  size_t yn = ccwld_link_symbol_count (lk);
  lua_createtable (L, (int)yn, 0);
  for (size_t i = 0; i < yn; i++)
    {
      ccwld_sym_view v;
      if (!ccwld_link_symbol (lk, i, &v))
        continue;
      lua_createtable (L, 0, 6);
      lua_pushstring (L, v.name ? v.name : "");
      lua_setfield (L, -2, "name");
      lua_pushinteger (L, (lua_Integer)v.value);
      lua_setfield (L, -2, "value");
      lua_pushstring (L, v.defined_in ? v.defined_in : "");
      lua_setfield (L, -2, "defined_in");
      lua_pushstring (L, v.binding ? v.binding : "global");
      lua_setfield (L, -2, "binding");
      lua_pushstring (L, v.visibility ? v.visibility : "default");
      lua_setfield (L, -2, "visibility");
      lua_pushboolean (L, v.defined);
      lua_setfield (L, -2, "defined");
      lua_seti (L, -2, (lua_Integer)i + 1);
    }
  lua_setfield (L, -2, "symbols");

  /* methods as closures over the handle */
  lua_pushlightuserdata (L, lk);
  lua_pushcclosure (L, lh_undefined, 1);
  lua_setfield (L, -2, "undefined");
  lua_pushlightuserdata (L, lk);
  lua_pushcclosure (L, lh_reloc_stats, 1);
  lua_setfield (L, -2, "reloc_stats");
  lua_pushlightuserdata (L, lk);
  lua_pushcclosure (L, lh_set_symbol, 1);
  lua_setfield (L, -2, "set_symbol");
  lua_pushlightuserdata (L, lk);
  lua_pushcclosure (L, lh_keep_section, 1);
  lua_setfield (L, -2, "keep_section");
  lua_pushlightuserdata (L, lk);
  lua_pushcclosure (L, lh_move_section, 1);
  lua_setfield (L, -2, "move_section");
  lua_pushlightuserdata (L, lk);
  lua_pushcclosure (L, lh_add_note, 1);
  lua_setfield (L, -2, "add_note");
}

/* the C trampoline registered on the plan */
static int
hook_trampoline (ccwld_phase phase, ccwld_link *lk, void *user)
{
  lccwld_hook *h = (lccwld_hook *)user;
  lua_State *L = h->L;
  lua_rawgeti (L, LUA_REGISTRYINDEX, h->ref);
  push_link_handle (L, lk);
  if (lua_pcall (L, 1, 0, 0) != LUA_OK)
    {
      const char *emsg = lua_tostring (L, -1);
      ccwld_state *st = (ccwld_state *)(lk && lk->plan ? lk->plan->state
                                                       : NULL);
      if (st)
        ccwld_diag_error (st, CCWLD_EXIT_LINK, "hook", NULL,
                          "Lua hook failed at phase %d: %s", (int)phase,
                          emsg ? emsg : "?");
      else
        fprintf (stderr, "ccwld: hook failed at phase %d: %s\n", (int)phase,
                 emsg ? emsg : "?");
      lua_pop (L, 1);
      return 1;
    }
  return 0;
}

static int
l_on (lua_State *L)
{
  lua_ctx *c = ctx (L);
  const char *name = luaL_checkstring (L, 1);
  luaL_checktype (L, 2, LUA_TFUNCTION);
  ccwld_phase ph;
  if (!phase_from_name (name, &ph))
    return luaL_error (L, "on: unknown phase '%s' "
                          "(resolved|gc|layout|emit)",
                       name),
           0;
  if (c->rt->nhooks >= LCCWLD_MAX_HOOKS)
    return luaL_error (L, "too many hooks (max %d)", LCCWLD_MAX_HOOKS), 0;
  lccwld_hook *h = &c->rt->hooks[c->rt->nhooks++];
  h->L = c->rt->L;
  lua_pushvalue (L, 2);
  h->ref = luaL_ref (L, LUA_REGISTRYINDEX);
  if (!ccwld_plan_hook (c->p, ph, hook_trampoline, h, c->e))
    return lccwld_error (L, c->e), 0;
  return 0;
}

/* ================================================================
 * sandbox (§5)
 * ================================================================ */

static int
sandbox_getenv (lua_State *L)
{
  const char *k = luaL_checkstring (L, 1);
  if (!strncmp (k, "CCWLD_", 6))
    {
      const char *v = getenv (k);
      if (v)
        {
          lua_pushstring (L, v);
          return 1;
        }
    }
  lua_pushnil (L);
  return 1;
}

static int
deny_write (lua_State *L)
{
  return luaL_error (L, "this table is read-only"), 0;
}

static void
make_readonly (lua_State *L, int idx)
{
  lua_createtable (L, 0, 2);
  lua_pushcfunction (L, deny_write);
  lua_setfield (L, -2, "__newindex");
  lua_pushvalue (L, idx < 0 ? idx - 1 : idx);
  lua_setfield (L, -2, "__index");
  lua_setmetatable (L, idx < 0 ? idx - 1 : idx);
}

static void
build_sandbox (lua_State *L, lua_ctx *c)
{
  luaL_requiref (L, LUA_GNAME, luaopen_base, 1);
  lua_pop (L, 1);
  luaL_requiref (L, LUA_STRLIBNAME, luaopen_string, 1);
  lua_pop (L, 1);
  luaL_requiref (L, LUA_TABLIBNAME, luaopen_table, 1);
  lua_pop (L, 1);
  luaL_requiref (L, LUA_MATHLIBNAME, luaopen_math, 1);
  lua_pop (L, 1);
  luaL_requiref (L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
  lua_pop (L, 1);

  /* math.random family removed (§6) */
  lua_getglobal (L, "math");
  if (lua_istable (L, -1))
    {
      lua_pushnil (L);
      lua_setfield (L, -2, "random");
      lua_pushnil (L);
      lua_setfield (L, -2, "randomseed");
      lua_pop (L, 1);
    }

  if (!c->unsafe)
    {
      lua_pushnil (L);
      lua_setglobal (L, "io");
      lua_pushnil (L);
      lua_setglobal (L, "package");
      lua_pushnil (L);
      lua_setglobal (L, "require");
      lua_pushnil (L);
      lua_setglobal (L, "load");
      lua_pushnil (L);
      lua_setglobal (L, "loadstring");
      lua_pushnil (L);
      lua_setglobal (L, "loadfile");
      lua_pushnil (L);
      lua_setglobal (L, "dofile");
      lua_pushnil (L);
      lua_setglobal (L, "debug");

      /* frozen os: only getenv, only CCWLD_* keys */
      lua_createtable (L, 0, 1);
      lua_pushcfunction (L, sandbox_getenv);
      lua_setfield (L, -2, "getenv");
      lua_setglobal (L, "os");
    }
}

/* ================================================================
 * module registration
 * ================================================================ */

static void
reg (lua_State *L, lua_CFunction fn, const char *name)
{
  lua_pushcfunction (L, fn);
  lua_setfield (L, -2, name);
}

static void
target_descriptor (lua_State *L, const char *target)
{
  const char *t = target ? target : "unknown";
  const char *dash = strchr (t, '-');
  char arch[64] = "unknown";
  if (dash && (size_t)(dash - t) < sizeof (arch))
    {
      memcpy (arch, t, (size_t)(dash - t));
      arch[dash - t] = 0;
    }
  else
    snprintf (arch, sizeof (arch), "%s", t);
  const char *format = "elf";
  if (!strncmp (t, "wasm", 4))
    format = "wasm";
  else if (strstr (t, "-pe") || !strncmp (t, "pe", 2))
    format = "pe";
  const char *endian = "little";
  if (strstr (t, "eb") || strstr (t, "be"))
    endian = "big";
  int width = 64;
  if (strstr (t, "32") || !strncmp (t, "i386", 4) || !strncmp (t, "arm", 3))
    width = 32;

  lua_createtable (L, 0, 4);
  lua_pushstring (L, arch);
  lua_setfield (L, -2, "arch");
  lua_pushstring (L, format);
  lua_setfield (L, -2, "format");
  lua_pushstring (L, endian);
  lua_setfield (L, -2, "endianness");
  lua_pushinteger (L, width);
  lua_setfield (L, -2, "ptr_width");
  make_readonly (L, -1);
}

static void
register_module (lua_ctx *c)
{
  lua_State *L = c->rt->L;

  /* expression metatable */
  luaL_newmetatable (L, EXPR_MT);
  lua_pushcfunction (L, mm_add);
  lua_setfield (L, -2, "__add");
  lua_pushcfunction (L, mm_sub);
  lua_setfield (L, -2, "__sub");
  lua_pushcfunction (L, mm_mul);
  lua_setfield (L, -2, "__mul");
  lua_pushcfunction (L, mm_div);
  lua_setfield (L, -2, "__div");
  lua_pushcfunction (L, mm_mod);
  lua_setfield (L, -2, "__mod");
  lua_pushcfunction (L, mm_band);
  lua_setfield (L, -2, "__band");
  lua_pushcfunction (L, mm_bor);
  lua_setfield (L, -2, "__bor");
  lua_pushcfunction (L, mm_bxor);
  lua_setfield (L, -2, "__bxor");
  lua_pushcfunction (L, mm_shl);
  lua_setfield (L, -2, "__shl");
  lua_pushcfunction (L, mm_shr);
  lua_setfield (L, -2, "__shr");
  lua_pushcfunction (L, mm_eq);
  lua_setfield (L, -2, "__eq");
  lua_pushcfunction (L, mm_lt);
  lua_setfield (L, -2, "__lt");
  lua_pushcfunction (L, mm_le);
  lua_setfield (L, -2, "__le");
  lua_pushcfunction (L, mm_unm);
  lua_setfield (L, -2, "__unm");
  lua_pushcfunction (L, mm_bnot);
  lua_setfield (L, -2, "__bnot");
  lua_pushcfunction (L, mm_tostring);
  lua_setfield (L, -2, "__tostring");
  lua_pushcfunction (L, mm_gc);
  lua_setfield (L, -2, "__gc");
  lua_pop (L, 1);

  /* the ccwld module */
  lua_createtable (L, 0, 48);

  reg (L, l_output, "output");
  reg (L, l_input, "input");
  reg (L, l_group, "group");
  reg (L, l_as_needed, "as_needed");
  reg (L, l_search_path, "search_path");
  reg (L, l_startup, "startup");
  reg (L, l_memory, "memory");
  reg (L, l_sections, "sections");
  reg (L, l_out, "out");
  reg (L, l_match, "match");
  reg (L, l_keep, "keep");
  reg (L, l_phdrs, "phdrs");
  reg (L, l_segments, "segments");
  reg (L, l_version, "version");
  reg (L, l_symbol, "symbol");
  reg (L, l_assign, "assign");
  reg (L, l_provide, "provide");
  reg (L, l_provide_hidden, "provide_hidden");
  reg (L, l_hidden, "hidden");
  reg (L, l_weaken, "weaken");
  reg (L, l_alias, "alias");
  reg (L, l_dot, "dot");
  reg (L, l_align, "align");
  reg (L, l_addr, "addr");
  reg (L, l_loadaddr, "loadaddr");
  reg (L, l_sizeof, "sizeof");
  reg (L, l_sizeof_headers, "sizeof_headers");
  reg (L, l_origin, "origin");
  reg (L, l_length, "length");
  reg (L, l_max, "max");
  reg (L, l_min, "min");
  reg (L, l_abs, "abs");
  reg (L, l_defined, "defined");
  reg (L, l_cond, "cond");
  reg (L, l_segment_start, "segment_start");
  reg (L, l_fill_fn, "fill");
  reg (L, l_include, "include");
  reg (L, l_gensym, "gensym");
  reg (L, l_error, "error");
  reg (L, l_warn, "warn");
  reg (L, l_assert, "assert");
  reg (L, l_lto, "lto");
  reg (L, l_plugin, "plugin");
  reg (L, l_plugin_opt, "plugin_opt");
  reg (L, l_on, "on");

  /* ccwld.env — -D / --defsym bindings (§3) */
  lua_createtable (L, 0, 8);
  for (size_t i = 0; i < c->p->nenv; i++)
    {
      lua_pushstring (L, c->p->env_vals[i]);
      lua_setfield (L, -2, c->p->env_keys[i]);
    }
  make_readonly (L, -1);
  lua_setfield (L, -2, "env");

  /* ccwld.target (§3) */
  target_descriptor (L, c->p->target);
  lua_setfield (L, -2, "target");

  /* ccwld.builtin (§3) */
  lua_createtable (L, 0, 6);
  lua_pushstring (L, CCWLD_VERSION);
  lua_setfield (L, -2, "version");
  lua_pushinteger (L, LCCWLD_MAX_INCLUDE);
  lua_setfield (L, -2, "max_include_depth");
  lua_pushinteger (L, 8);
  lua_setfield (L, -2, "max_hook_depth");
  lua_pushboolean (L, c->unsafe);
  lua_setfield (L, -2, "unsafe_lua");
  lua_pushstring (L, "embedded");
  lua_setfield (L, -2, "emitter");
  make_readonly (L, -1);
  lua_setfield (L, -2, "builtin");

  lua_pushlightuserdata (L, c);
  lua_setfield (L, LUA_REGISTRYINDEX, "ccwld.lccwld.ctx");

  lua_setglobal (L, "ccwld");
}

/* ================================================================
 * runtime lifecycle
 * ================================================================ */

static void
rt_free (void *vp)
{
  lccwld_rt *rt = (lccwld_rt *)vp;
  if (!rt)
    return;
  for (size_t i = 0; i < rt->nhooks; i++)
    if (rt->hooks[i].ref != LUA_NOREF)
      luaL_unref (rt->L, LUA_REGISTRYINDEX, rt->hooks[i].ref);
  for (size_t i = 0; i < rt->ctx.nsink; i++)
    sink_free_entry (&rt->ctx.sink[i]);
  free (rt->ctx.sink);
  for (size_t i = 0; i < rt->ctx.nstack; i++)
    free ((void *)rt->ctx.stack[i]);
  if (rt->L)
    lua_close (rt->L);
  free (rt);
}

static int
run_chunk (lua_ctx *c, const char *path)
{
  size_t len = 0;
  char *data = read_file (path, &len);
  if (!data)
    {
      ccwld_error_set (c->e, CCWLD_EXIT_USAGE, "cannot open script '%s'",
                       path);
      return 0;
    }
  lua_State *L = c->rt->L;
  int ok = luaL_loadbuffer (L, data, len, path) == LUA_OK
           && lua_pcall (L, 0, 0, 0) == LUA_OK;
  free (data);
  if (!ok)
    {
      const char *msg = lua_tostring (L, -1);
      ccwld_error_set (c->e, CCWLD_EXIT_USAGE, "%s", msg ? msg : "script error");
      lua_pop (L, 1);
      return 0;
    }
  return 1;
}

int
ccwld_run_lua (const char *file, const char *target,
               const char *const *defines, const char *const *defsymbols,
               int unsafe_lua, const ccwld_driver_defs *extra,
               ccwld_plan **out, ccwld_error *e)
{
  if (!out || !file)
    {
      ccwld_error_set (e, CCWLD_EXIT_USAGE, "invalid Lua linker script");
      return 0;
    }
  *out = NULL;

  ccwld_plan *p = ccwld_plan_new (target ? target : "unknown");
  if (!p)
    {
      ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
      return 0;
    }
  ccwld_plan_set_frontend (p, "lua");

  lccwld_rt *rt = calloc (1, sizeof (*rt));
  if (!rt)
    {
      ccwld_plan_free (p);
      ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "out of memory");
      return 0;
    }
  rt->ctx.p = p;
  rt->ctx.e = e;
  rt->ctx.rt = rt;
  rt->ctx.unsafe = unsafe_lua;
  rt->ctx.stack[rt->ctx.nstack++] = strdup (file);
  rt->L = luaL_newstate ();
  if (!rt->L)
    {
      rt_free (rt);
      ccwld_plan_free (p);
      ccwld_error_set (e, CCWLD_EXIT_INTERNAL, "cannot create Lua state");
      return 0;
    }

  /* -D / --defsym bindings land in the plan before the script runs */
  if (defines)
    for (size_t i = 0; defines[i]; i++)
      {
        const char *eq = strchr (defines[i], '=');
        char key[128];
        if (!eq)
          {
            ccwld_error_set (e, CCWLD_EXIT_USAGE,
                             "-D expects key=value (got '%s')", defines[i]);
            goto setup_fail;
          }
        size_t kl = (size_t)(eq - defines[i]);
        if (kl >= sizeof (key))
          kl = sizeof (key) - 1;
        memcpy (key, defines[i], kl);
        key[kl] = 0;
        if (!ccwld_plan_env (p, key, eq + 1, 0, e))
          goto setup_fail;
      }
  if (defsymbols)
    for (size_t i = 0; defsymbols[i]; i++)
      {
        const char *eq = strchr (defsymbols[i], '=');
        char key[128];
        if (!eq)
          {
            ccwld_error_set (e, CCWLD_EXIT_USAGE,
                             "--defsym expects name=value (got '%s')",
                             defsymbols[i]);
            goto setup_fail;
          }
        size_t kl = (size_t)(eq - defsymbols[i]);
        if (kl >= sizeof (key))
          kl = sizeof (key) - 1;
        memcpy (key, defsymbols[i], kl);
        key[kl] = 0;
        if (!ccwld_plan_env (p, key, eq + 1, 1, e))
          goto setup_fail;
      }

  p->options.unsafe_lua = unsafe_lua;
  if (unsafe_lua)
    p->reproducible = 0; /* §5: unsafe-lua links are non-reproducible */

  /* driver-level declarations land before the script body (§2.1) */
  if (!ccwld_apply_driver_defs (p, extra, e))
    goto setup_fail;

  build_sandbox (rt->L, &rt->ctx);
  register_module (&rt->ctx);

  if (!run_chunk (&rt->ctx, file))
    goto setup_fail;

  /* bare top-level statements flush at the top level (§4.7) */
  for (size_t i = 0; i < rt->ctx.nsink; i++)
    {
      lccwld_stmt *s = &rt->ctx.sink[i];
      if (s->consumed)
        continue;
      char *site = site_here (rt->L, &rt->ctx);
      int ok = sink_apply (&rt->ctx, s, -1, site);
      free (site);
      if (!ok)
        goto setup_fail;
      s->consumed = 1;
    }

  /* default output declaration keeps script-less-output plans valid */
  if (!rt->ctx.output_set)
    {
      ccwld_output def;
      memset (&def, 0, sizeof (def));
      def.kind = strdup ("exe");
      def.format = strdup ("elf");
      int ok = ccwld_plan_output (p, &def, e);
      free (def.kind);
      free (def.format);
      if (!ok)
        goto setup_fail;
    }

  /* GNU-style: the command-line -e wins over the script's entry */
  if (extra && extra->entry
      && !ccwld_driver_entry_override (p, extra->entry, e))
    goto setup_fail;

  if (!ccwld_plan_seal (p, e))
    goto setup_fail;

  /* the Lua state must outlive phase 0: hooks run during the link */
  p->frontend_ctx = rt;
  p->frontend_ctx_free = rt_free;
  *out = p;
  return 1;

setup_fail:
  rt_free (rt);
  ccwld_plan_free (p);
  return 0;
}
