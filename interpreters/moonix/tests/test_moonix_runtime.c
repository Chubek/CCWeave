#include "ccw_test.h"
#include "moonix.h"

#include <string.h>

static moonix_state *
new_state (moonix_tier tier)
{
  moonix_options options;
  moonix_options_init (&options);
  options.tier = tier;
  return moonix_newstate (&options);
}

static void
check_core_semantics (moonix_tier tier)
{
  static const char source[]
      = "local function make_counter(x)\n"
        "  return function(y) x = x + y; return x end\n"
        "end\n"
        "local c = make_counter(5)\n"
        "local t = {value = c(3)}\n"
        "local ok, message = pcall(function() error('caught') end)\n"
        "result = t.value + (17 // 5) + (17 % 5) + (6 & 3)\n"
        "caught = (not ok) and string.sub(message, -6) == 'caught'\n";
  moonix_state *state = new_state (tier);
  lua_State *lua;
  CCW_CHECK (state != NULL, "could not create tier %d state", (int)tier);
  if (state == NULL)
    return;
  CCW_CHECK (moonix_dostring (state, source, "=runtime") == MOONIX_OK,
             "tier %d failed: %s", (int)tier, moonix_last_error (state));
  lua = moonix_lua_state (state);
  lua_getglobal (lua, "result");
  CCW_CHECK (lua_isinteger (lua, -1) && lua_tointeger (lua, -1) == 15,
             "tier %d arithmetic result differs", (int)tier);
  lua_pop (lua, 1);
  lua_getglobal (lua, "caught");
  CCW_CHECK (lua_toboolean (lua, -1), "tier %d pcall/error differs",
             (int)tier);
  lua_pop (lua, 1);
  if (tier == MOONIX_TIER_T2)
    CCW_CHECK (moonix_active_tier (state) == MOONIX_TIER_T0,
               "v0.1 T2 must degrade to T0");
  moonix_close (state);
}

static void
check_exclusions (void)
{
  moonix_state *state = new_state (MOONIX_TIER_T0);
  moonix_status status;
  CCW_CHECK (state != NULL, "could not create exclusion-test state");
  if (state == NULL)
    return;

  status = moonix_dostring (state, "coroutine.create(function() end)", "=co");
  CCW_CHECK (status == MOONIX_ERR_RUNTIME
                 && strstr (moonix_last_error (state), "not supported")
                        != NULL,
             "coroutine exclusion must be explicit");
  status
      = moonix_dostring (state, "setmetatable({}, {__mode = 'k'})", "=weak");
  CCW_CHECK (status == MOONIX_ERR_RUNTIME
                 && strstr (moonix_last_error (state), "not supported")
                        != NULL,
             "weak-table exclusion must be explicit");
  status = moonix_dostring (
      state, "local x <close> = setmetatable({}, {__close=function() end})",
      "=close");
  CCW_CHECK (status == MOONIX_ERR_UNSUPPORTED
                 && strstr (moonix_last_error (state), "not supported")
                        != NULL,
             "<close> exclusion must be explicit");
  status = moonix_dostring (state, "literal = '<close>'", "=literal");
  CCW_CHECK (status == MOONIX_OK,
             "<close> inside a string must not trigger phase exclusion");
  moonix_close (state);
}

static void
check_goto_fallback (void)
{
  moonix_state *state = new_state (MOONIX_TIER_T1);
  moonix_status status;
  CCW_CHECK (state != NULL, "could not create goto-test state");
  if (state == NULL)
    return;
  status = moonix_dostring (
      state, "local x = 0; goto done; x = 1; ::done:: goto_result = x",
      "=goto");
  CCW_CHECK (status == MOONIX_OK, "T0 goto fallback failed: %s",
             moonix_last_error (state));
  CCW_CHECK (moonix_active_tier (state) == MOONIX_TIER_T0,
             "goto must remain interpreter-only in v0.1");
  moonix_close (state);
}

static int
extension_add (lua_State *lua)
{
  long long a = lua_tointeger (lua, 1), b = lua_tointeger (lua, 2);
  lua_settop (lua, 0);
  moonix_lua_pushinteger (lua, a + b);
  return 1;
}
static int
extension_open (moonix_state *state)
{
  lua_State *lua = moonix_lua_state (state);
  moonix_lua_pushinteger (lua, 7);
  moonix_lua_setglobal (lua, "extension_marker");
  return 0;
}
static void
check_native_apis (void)
{
  moonix_options options;
  moonix_extension ext;
  moonix_state *state;
  moonix_options_init (&options);
  state = moonix_newstate (&options);
  ext.name = "test";
  ext.open = extension_open;
  ext.userdata = NULL;
  CCW_CHECK (state && moonix_register_extension (state, &ext) == MOONIX_OK,
             "Moonix extension registration failed");
  if (state)
    {
      lua_State *lua = moonix_lua_state (state);
      lua_getglobal (lua, "extension_marker");
      CCW_CHECK (lua_tointeger (lua, -1) == 7,
                 "extension initializer did not run");
      lua_settop (lua, 0);
      moonix_lua_pushinteger (lua, 2);
      moonix_lua_pushinteger (lua, 3);
      CCW_CHECK (moonix_call_cfunction (state, extension_add, 2, 1)
                         == MOONIX_OK
                     && lua_tointeger (lua, -1) == 5,
                 "C interop call failed");
      moonix_close (state);
    }
}

int
main (void)
{
  check_core_semantics (MOONIX_TIER_T0);
  check_core_semantics (MOONIX_TIER_T1);
  check_core_semantics (MOONIX_TIER_T2);
  check_exclusions ();
  check_goto_fallback ();
  check_native_apis ();
  return ccw_test_report ("moonix-runtime");
}
