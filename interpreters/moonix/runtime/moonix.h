/* Moonix embedding API — MOONIX §1.5.
 *
 * The v0.1 specification names a lua.h-compatible subset without listing a
 * second set of signatures.  This header therefore exposes the vendored Lua
 * scalar/stack API through lua.h and adds Moonix lifecycle, tier, frontend,
 * and bytecode operations. */

#ifndef MOONIX_H
#define MOONIX_H

#include "ccw_ir.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOONIX_VERSION_MAJOR 0
#define MOONIX_VERSION_MINOR 1
#define MOONIX_BYTECODE_VERSION 1

typedef struct moonix_state moonix_state;
typedef struct moonix_lua_state lua_State;

#define LUA_MULTRET (-1)
#define LUA_VERSION_RELEASE "5.5-compatible"

int moonix_lua_gettop(const lua_State *);
void moonix_lua_settop(lua_State *, int);
void moonix_lua_pop(lua_State *, int);
void moonix_lua_getglobal(lua_State *, const char *);
int moonix_lua_isinteger(const lua_State *, int);
long long moonix_lua_tointeger(const lua_State *, int);
int moonix_lua_toboolean(const lua_State *, int);
const char *moonix_lua_tostring(const lua_State *, int);
void moonix_lua_insert(lua_State *, int);
void moonix_lua_pushinteger(lua_State *, long long);
void moonix_lua_pushboolean(lua_State *, int);
void moonix_lua_pushstring(lua_State *, const char *);
void moonix_lua_pushnil(lua_State *);
void moonix_lua_setglobal(lua_State *, const char *);
#define lua_gettop moonix_lua_gettop
#define lua_settop moonix_lua_settop
#define lua_pop moonix_lua_pop
#define lua_getglobal moonix_lua_getglobal
#define lua_isinteger moonix_lua_isinteger
#define lua_tointeger moonix_lua_tointeger
#define lua_toboolean moonix_lua_toboolean
#define lua_tostring moonix_lua_tostring
#define lua_insert moonix_lua_insert

typedef enum {
    MOONIX_TIER_T0 = 0,
    MOONIX_TIER_T1 = 1,
    MOONIX_TIER_T2 = 2
} moonix_tier;

typedef enum {
    MOONIX_OK = 0,
    MOONIX_ERR_ARGUMENT = -1,
    MOONIX_ERR_OOM = -2,
    MOONIX_ERR_SYNTAX = -3,
    MOONIX_ERR_RUNTIME = -4,
    MOONIX_ERR_FRONTEND = -5,
    MOONIX_ERR_SCHED = -6,
    MOONIX_ERR_BYTECODE = -7,
    MOONIX_ERR_UNSUPPORTED = -8
} moonix_status;

typedef struct {
    moonix_tier tier;
    const char *manifest_dir;
    const char *sched_dir;
} moonix_options;

typedef struct {
    unsigned char *data;
    size_t size;
    ccw_ir *on1x_ir;
    int source_line_count;
    int t0_only;
} moonix_chunk;

void moonix_options_init(moonix_options *options);
moonix_state *moonix_newstate(const moonix_options *options);
void moonix_close(moonix_state *state);

lua_State *moonix_lua_state(moonix_state *state);
moonix_status moonix_compile(moonix_state *, const char *, size_t,
                             const char *, moonix_chunk *);
const char *moonix_last_error(const moonix_state *state);
const char *moonix_status_string(moonix_status status);

moonix_status moonix_set_tier(moonix_state *state, moonix_tier tier);
moonix_tier moonix_requested_tier(const moonix_state *state);
moonix_tier moonix_active_tier(const moonix_state *state);
const char *moonix_plan_hash(const moonix_state *state, moonix_tier tier);

moonix_status moonix_compile(moonix_state *state, const char *source,
                             size_t source_len, const char *chunk_name,
                             moonix_chunk *chunk);
void moonix_chunk_clear(moonix_chunk *chunk);
moonix_status moonix_load_chunk(moonix_state *state,
                                const moonix_chunk *chunk);
moonix_status moonix_load_buffer(moonix_state *state, const char *source,
                                 size_t source_len, const char *chunk_name);
moonix_status moonix_load_file(moonix_state *state, const char *path);
moonix_status moonix_pcall(moonix_state *state, int nargs, int nresults);
moonix_status moonix_dostring(moonix_state *state, const char *source,
                              const char *chunk_name);
moonix_status moonix_dofile(moonix_state *state, const char *path);

/* Native extension and C interop ABI.  Extensions may be authored in C or
 * C++; loading is backed by dynalo and all values crossing this boundary are
 * scalar stack values.  The scalar FFI below is dispatched through dyncall. */
typedef int (*moonix_cfunction)(lua_State *state);
typedef int (*moonix_extension_open)(moonix_state *state);
typedef struct {
    const char *name;
    moonix_extension_open open;
    void *userdata;
} moonix_extension;
/* Shared objects export:
 *   const moonix_extension *moonix_extension_init(void); */
moonix_status moonix_register_extension(moonix_state *, const moonix_extension *);
moonix_status moonix_load_extension(moonix_state *, const char *path);
moonix_status moonix_call_cfunction(moonix_state *, moonix_cfunction,
                                    int nargs, int nresults);

typedef void *moonix_ffi_library;
moonix_ffi_library moonix_ffi_open(const char *path);
void *moonix_ffi_symbol(moonix_ffi_library, const char *name);
void moonix_ffi_close(moonix_ffi_library);
moonix_status moonix_ffi_call_i64(void *symbol, const long long *args,
                                  size_t nargs, long long *result);

/* Runtime hook queried by vm.gc-barrier-insertion integrations. */
int moonix_gc_barrier_check(const moonix_state *state,
                            uint64_t owner_handle, uint64_t value_handle);

#ifdef __cplusplus
}
#endif
#endif
