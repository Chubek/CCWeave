/* Moonix embedding API — MOONIX §1.5.
 *
 * The v0.1 specification names a lua.h-compatible subset without listing a
 * second set of signatures.  This header therefore exposes the vendored Lua
 * scalar/stack API through lua.h and adds Moonix lifecycle, tier, frontend,
 * and bytecode operations. */

#ifndef MOONIX_H
#define MOONIX_H

#include "lua.h"
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

/* Runtime hook queried by vm.gc-barrier-insertion integrations. */
int moonix_gc_barrier_check(const moonix_state *state,
                            uint64_t owner_handle, uint64_t value_handle);

#ifdef __cplusplus
}
#endif
#endif
