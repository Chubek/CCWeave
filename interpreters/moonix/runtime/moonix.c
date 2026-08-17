#include "moonix_internal.h"
#include "../frontend/moonix_frontend.h"

#include "lauxlib.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef MOONIX_DEFAULT_MANIFEST_DIR
#define MOONIX_DEFAULT_MANIFEST_DIR "manifests"
#endif
#ifndef MOONIX_DEFAULT_SCHED_DIR
#define MOONIX_DEFAULT_SCHED_DIR "interpreters/moonix/sched"
#endif

static char *moonix_strdup(const char *value)
{
    size_t size;
    char *copy;
    if (value == NULL) return NULL;
    size = strlen(value) + 1u;
    copy = (char *)malloc(size);
    if (copy != NULL) memcpy(copy, value, size);
    return copy;
}

void moonix_set_error(moonix_state *state, const char *message)
{
    if (state == NULL) return;
    snprintf(state->error, sizeof(state->error), "%s",
             message != NULL ? message : "unknown Moonix error");
}

void moonix_options_init(moonix_options *options)
{
    if (options == NULL) return;
    options->tier = MOONIX_TIER_T0;
    options->manifest_dir = MOONIX_DEFAULT_MANIFEST_DIR;
    options->sched_dir = MOONIX_DEFAULT_SCHED_DIR;
}

moonix_state *moonix_newstate(const moonix_options *options)
{
    moonix_options defaults;
    moonix_state *state;
    if (options == NULL) {
        moonix_options_init(&defaults);
        options = &defaults;
    }
    state = (moonix_state *)calloc(1, sizeof(*state));
    if (state == NULL) return NULL;
    state->lua = luaL_newstate();
    state->manifest_dir = moonix_strdup(options->manifest_dir != NULL
                                            ? options->manifest_dir
                                            : MOONIX_DEFAULT_MANIFEST_DIR);
    state->sched_dir = moonix_strdup(options->sched_dir != NULL
                                         ? options->sched_dir
                                         : MOONIX_DEFAULT_SCHED_DIR);
    if (state->lua == NULL || state->manifest_dir == NULL ||
        state->sched_dir == NULL || !moonix_install_stdlib(state)) {
        moonix_close(state);
        return NULL;
    }
    state->requested_tier = MOONIX_TIER_T0;
    state->active_tier = MOONIX_TIER_T0;
    if (moonix_set_tier(state, options->tier) != MOONIX_OK) {
        moonix_close(state);
        return NULL;
    }
    return state;
}

void moonix_close(moonix_state *state)
{
    if (state == NULL) return;
    ccw_plan_free(state->plans[0]);
    ccw_plan_free(state->plans[1]);
    if (state->lua != NULL) lua_close(state->lua);
    free(state->manifest_dir);
    free(state->sched_dir);
    free(state);
}

lua_State *moonix_lua_state(moonix_state *state)
{
    return state != NULL ? state->lua : NULL;
}

const char *moonix_last_error(const moonix_state *state)
{
    return state != NULL ? state->error : "invalid Moonix state";
}

const char *moonix_status_string(moonix_status status)
{
    switch (status) {
    case MOONIX_OK: return "success";
    case MOONIX_ERR_ARGUMENT: return "invalid argument";
    case MOONIX_ERR_OOM: return "out of memory";
    case MOONIX_ERR_SYNTAX: return "syntax error";
    case MOONIX_ERR_RUNTIME: return "runtime error";
    case MOONIX_ERR_FRONTEND: return "frontend error";
    case MOONIX_ERR_SCHED: return "scheduler error";
    case MOONIX_ERR_BYTECODE: return "invalid bytecode";
    case MOONIX_ERR_UNSUPPORTED: return "not supported in this phase";
    default: return "unknown Moonix status";
    }
}

moonix_status moonix_set_tier(moonix_state *state, moonix_tier tier)
{
    if (state == NULL || tier < MOONIX_TIER_T0 || tier > MOONIX_TIER_T2) {
        if (state != NULL) moonix_set_error(state, "invalid Moonix tier");
        return MOONIX_ERR_ARGUMENT;
    }
    return moonix_jit_select_tier(state, tier);
}

moonix_tier moonix_requested_tier(const moonix_state *state)
{
    return state != NULL ? state->requested_tier : MOONIX_TIER_T0;
}

moonix_tier moonix_active_tier(const moonix_state *state)
{
    return state != NULL ? state->active_tier : MOONIX_TIER_T0;
}

const char *moonix_plan_hash(const moonix_state *state, moonix_tier tier)
{
    int index = (int)tier - (int)MOONIX_TIER_T1;
    if (state == NULL || index < 0 || index > 1 ||
        state->plan_hashes[index][0] == '\0')
        return NULL;
    return state->plan_hashes[index];
}

moonix_status moonix_compile(moonix_state *state, const char *source,
                             size_t source_len, const char *chunk_name,
                             moonix_chunk *chunk)
{
    return moonix_frontend_compile(state, source, source_len, chunk_name, chunk);
}

static uint32_t read_version(const unsigned char *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

moonix_status moonix_load_chunk(moonix_state *state,
                                const moonix_chunk *chunk)
{
    static const unsigned char magic[8] = {
        'M', 'O', 'O', 'N', 'I', 'X', 'B', 'C'
    };
    const size_t header_size = sizeof(magic) + 4u;
    int status;
    if (state == NULL || chunk == NULL || chunk->data == NULL ||
        chunk->size <= header_size) {
        if (state != NULL) moonix_set_error(state, "invalid Moonix bytecode");
        return MOONIX_ERR_ARGUMENT;
    }
    if (memcmp(chunk->data, magic, sizeof(magic)) != 0 ||
        read_version(chunk->data + sizeof(magic)) != MOONIX_BYTECODE_VERSION) {
        moonix_set_error(state, "Moonix bytecode version mismatch");
        return MOONIX_ERR_BYTECODE;
    }
    status = luaL_loadbufferx(state->lua,
                              (const char *)chunk->data + header_size,
                              chunk->size - header_size,
                              "=(moonix-bytecode)", "b");
    if (status != LUA_OK) {
        moonix_set_error(state, lua_tostring(state->lua, -1));
        lua_pop(state->lua, 1);
        return MOONIX_ERR_BYTECODE;
    }
    /* T2 is intentionally a v0.2 feature.  T1 admission requires an On1x
     * lowering and a validated plan; otherwise §3 requires T0 fallback. */
    if (state->requested_tier == MOONIX_TIER_T1 &&
        !chunk->t0_only && chunk->on1x_ir != NULL && state->plans[0] != NULL)
        state->active_tier = MOONIX_TIER_T1;
    else
        state->active_tier = MOONIX_TIER_T0;
    state->error[0] = '\0';
    return MOONIX_OK;
}

moonix_status moonix_load_buffer(moonix_state *state, const char *source,
                                 size_t source_len, const char *chunk_name)
{
    moonix_chunk chunk;
    moonix_status status = moonix_compile(state, source, source_len,
                                          chunk_name, &chunk);
    if (status != MOONIX_OK) return status;
    status = moonix_load_chunk(state, &chunk);
    moonix_chunk_clear(&chunk);
    return status;
}

static char *read_file(const char *path, size_t *size_out)
{
    FILE *file;
    long end;
    char *data;
    size_t size;
    file = fopen(path, "rb");
    if (file == NULL) return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (end = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    size = (size_t)end;
    data = (char *)malloc(size + 1u);
    if (data == NULL || fread(data, 1, size, file) != size) {
        free(data);
        fclose(file);
        return NULL;
    }
    data[size] = '\0';
    fclose(file);
    *size_out = size;
    return data;
}

moonix_status moonix_load_file(moonix_state *state, const char *path)
{
    char *source;
    size_t source_len;
    moonix_status status;
    if (state == NULL || path == NULL) return MOONIX_ERR_ARGUMENT;
    source = read_file(path, &source_len);
    if (source == NULL) {
        snprintf(state->error, sizeof(state->error), "%s: %s",
                 path, strerror(errno));
        return MOONIX_ERR_ARGUMENT;
    }
    status = moonix_load_buffer(state, source, source_len, path);
    free(source);
    return status;
}

moonix_status moonix_pcall(moonix_state *state, int nargs, int nresults)
{
    int status;
    if (state == NULL || nargs < 0) return MOONIX_ERR_ARGUMENT;
    status = lua_pcall(state->lua, nargs, nresults, 0);
    if (status != LUA_OK) {
        moonix_set_error(state, lua_tostring(state->lua, -1));
        lua_pop(state->lua, 1);
        state->active_tier = MOONIX_TIER_T0;
        return MOONIX_ERR_RUNTIME;
    }
    state->error[0] = '\0';
    return MOONIX_OK;
}

moonix_status moonix_dostring(moonix_state *state, const char *source,
                              const char *chunk_name)
{
    moonix_status status;
    if (source == NULL) return MOONIX_ERR_ARGUMENT;
    status = moonix_load_buffer(state, source, strlen(source), chunk_name);
    if (status != MOONIX_OK) return status;
    return moonix_pcall(state, 0, LUA_MULTRET);
}

moonix_status moonix_dofile(moonix_state *state, const char *path)
{
    moonix_status status = moonix_load_file(state, path);
    if (status != MOONIX_OK) return status;
    return moonix_pcall(state, 0, LUA_MULTRET);
}

int moonix_gc_barrier_check(const moonix_state *state,
                            uint64_t owner_handle, uint64_t value_handle)
{
    (void)state;
    /* Null handles are never heap edges.  Non-null handle stores are
     * conservatively barriered, which is safe for an incremental collector. */
    return owner_handle != 0 && value_handle != 0;
}
