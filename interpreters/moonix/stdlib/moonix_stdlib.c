/* Moonix v0.1 standard-library surface — MOONIX §3. */

#include "../runtime/moonix_internal.h"

#include "lauxlib.h"
#include "lualib.h"

#include <string.h>

static int unsupported(lua_State *lua)
{
    const char *feature = lua_tostring(lua, lua_upvalueindex(1));
    return luaL_error(lua, "%s is not supported in Moonix v0.1",
                      feature != NULL ? feature : "feature");
}

static void set_unsupported_global(lua_State *lua, const char *name,
                                   const char *feature)
{
    lua_pushstring(lua, feature);
    lua_pushcclosure(lua, unsupported, 1);
    lua_setglobal(lua, name);
}

static void set_unsupported_field(lua_State *lua, const char *table,
                                  const char *field, const char *feature)
{
    lua_getglobal(lua, table);
    if (lua_istable(lua, -1)) {
        lua_pushstring(lua, feature);
        lua_pushcclosure(lua, unsupported, 1);
        lua_setfield(lua, -2, field);
    }
    lua_pop(lua, 1);
}

static int moonix_setmetatable(lua_State *lua)
{
    luaL_checkany(lua, 1);
    if (!lua_isnil(lua, 2)) {
        luaL_checktype(lua, 2, LUA_TTABLE);
        lua_getfield(lua, 2, "__mode");
        if (!lua_isnil(lua, -1))
            return luaL_error(lua,
                              "weak tables are not supported in Moonix v0.1");
        lua_pop(lua, 1);
        lua_getfield(lua, 2, "__gc");
        if (!lua_isnil(lua, -1))
            return luaL_error(lua,
                              "__gc is not supported in Moonix v0.1");
        lua_pop(lua, 1);
    }
    lua_settop(lua, 2);
    if (!lua_setmetatable(lua, 1))
        return luaL_error(lua, "cannot change a protected metatable");
    lua_settop(lua, 1);
    return 1;
}

int moonix_install_stdlib(moonix_state *state)
{
    lua_State *lua;
    static const char *coroutine_functions[] = {
        "create", "resume", "running", "status", "wrap", "yield", "isyieldable"
    };
    if (state == NULL || state->lua == NULL) return 0;
    lua = state->lua;

    luaL_requiref(lua, LUA_GNAME, luaopen_base, 1);
    lua_pop(lua, 1);
    luaL_requiref(lua, LUA_STRLIBNAME, luaopen_string, 1);
    lua_pop(lua, 1);
    luaL_requiref(lua, LUA_TABLIBNAME, luaopen_table, 1);
    lua_pop(lua, 1);
    luaL_requiref(lua, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_pop(lua, 1);

    lua_pushcfunction(lua, moonix_setmetatable);
    lua_setglobal(lua, "setmetatable");
    set_unsupported_global(lua, "require", "package loading");

    lua_newtable(lua);
    for (size_t i = 0;
         i < sizeof(coroutine_functions) / sizeof(coroutine_functions[0]);
         ++i) {
        lua_pushstring(lua, "coroutines");
        lua_pushcclosure(lua, unsupported, 1);
        lua_setfield(lua, -2, coroutine_functions[i]);
    }
    lua_setglobal(lua, "coroutine");

    set_unsupported_field(lua, "string", "find", "string patterns");
    set_unsupported_field(lua, "string", "match", "string patterns");
    set_unsupported_field(lua, "string", "gmatch", "string patterns");
    set_unsupported_field(lua, "string", "gsub", "string patterns");
    return 1;
}

static int is_ident(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

static size_t skip_quoted(const char *source, size_t source_len, size_t at)
{
    char quote = source[at++];
    while (at < source_len) {
        if (source[at] == '\\' && at + 1u < source_len) {
            at += 2u;
        } else if (source[at++] == quote) {
            break;
        }
    }
    return at;
}

static size_t skip_long(const char *source, size_t source_len, size_t at)
{
    size_t equals = 0;
    size_t cursor = at + 1u;
    while (cursor < source_len && source[cursor] == '=') {
        ++equals;
        ++cursor;
    }
    if (cursor >= source_len || source[cursor] != '[') return at;
    ++cursor;
    while (cursor < source_len) {
        if (source[cursor] == ']') {
            size_t end = cursor + 1u;
            size_t seen = 0;
            while (end < source_len && source[end] == '=' && seen < equals) {
                ++seen;
                ++end;
            }
            if (seen == equals && end < source_len && source[end] == ']')
                return end + 1u;
        }
        ++cursor;
    }
    return source_len;
}

static int source_has_token(const char *source, size_t source_len,
                            const char *token, int identifier)
{
    size_t token_len = strlen(token);
    for (size_t i = 0; i < source_len;) {
        size_t skipped;
        if (source[i] == '\'' || source[i] == '"') {
            i = skip_quoted(source, source_len, i);
            continue;
        }
        if (source[i] == '-' && i + 1u < source_len &&
            source[i + 1u] == '-') {
            i += 2u;
            if (i < source_len && source[i] == '[' &&
                (skipped = skip_long(source, source_len, i)) != i) {
                i = skipped;
            } else {
                while (i < source_len && source[i] != '\n') ++i;
            }
            continue;
        }
        if (source[i] == '[' &&
            (skipped = skip_long(source, source_len, i)) != i) {
            i = skipped;
            continue;
        }
        if (i + token_len <= source_len &&
            memcmp(source + i, token, token_len) == 0 &&
            (!identifier ||
             ((i == 0 || !is_ident((unsigned char)source[i - 1u])) &&
              (i + token_len == source_len ||
               !is_ident((unsigned char)source[i + token_len])))))
            return 1;
        ++i;
    }
    return 0;
}

int moonix_source_has_close_attribute(const char *source, size_t source_len)
{
    return source != NULL &&
           source_has_token(source, source_len, "<close>", 0);
}

int moonix_source_has_goto(const char *source, size_t source_len)
{
    return source != NULL && source_has_token(source, source_len, "goto", 1);
}
