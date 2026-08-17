/* Moonix frontend — MOONIX §4 and §5.
 *
 * Swaff owns CST parsing and the On1x lowering attempt.  The frozen Moonix
 * bytecode payload uses the repository's pinned Lua 5.5 instruction stream;
 * the Moonix header versions that payload independently for deopt metadata. */

#include "moonix_frontend.h"
#include "../runtime/moonix_internal.h"

#include "ccw_swaff.h"
#include "lauxlib.h"

#include <stdlib.h>
#include <string.h>

static const unsigned char moonix_bytecode_magic[8] = {
    'M', 'O', 'O', 'N', 'I', 'X', 'B', 'C'
};

typedef struct {
    unsigned char *data;
    size_t size;
    size_t capacity;
} byte_buffer;

static int byte_writer(lua_State *lua, const void *data, size_t size, void *user)
{
    byte_buffer *buffer = (byte_buffer *)user;
    unsigned char *grown;
    size_t capacity;
    (void)lua;
    if (size > SIZE_MAX - buffer->size) return 1;
    if (buffer->size + size > buffer->capacity) {
        capacity = buffer->capacity ? buffer->capacity : 256u;
        while (capacity < buffer->size + size) {
            if (capacity > SIZE_MAX / 2u) return 1;
            capacity *= 2u;
        }
        grown = (unsigned char *)realloc(buffer->data, capacity);
        if (grown == NULL) return 1;
        buffer->data = grown;
        buffer->capacity = capacity;
    }
    if (size != 0)
        memcpy(buffer->data + buffer->size, data, size);
    buffer->size += size;
    return 0;
}

static int source_line_count(const char *source, size_t source_len)
{
    int lines = 0;
    for (size_t i = 0; i < source_len; ++i)
        if (source[i] == '\n') ++lines;
    if (source_len != 0 && source[source_len - 1] != '\n') ++lines;
    return lines;
}

static void write_version(unsigned char *out)
{
    uint32_t version = MOONIX_BYTECODE_VERSION;
    out[0] = (unsigned char)(version & 0xffu);
    out[1] = (unsigned char)((version >> 8) & 0xffu);
    out[2] = (unsigned char)((version >> 16) & 0xffu);
    out[3] = (unsigned char)((version >> 24) & 0xffu);
}

moonix_status moonix_frontend_compile(moonix_state *state,
                                      const char *source,
                                      size_t source_len,
                                      const char *chunk_name,
                                      moonix_chunk *chunk)
{
    byte_buffer payload = {0};
    ccw_swaff_report report;
    char *swaff_error = NULL;
    ccw_ir *ir;
    int load_status;
    const size_t header_size = sizeof(moonix_bytecode_magic) + 4u;

    if (state == NULL || source == NULL || chunk == NULL) {
        if (state != NULL) moonix_set_error(state, "invalid frontend arguments");
        return MOONIX_ERR_ARGUMENT;
    }
    memset(chunk, 0, sizeof(*chunk));
    if (moonix_source_has_close_attribute(source, source_len)) {
        moonix_set_error(state,
                         "to-be-closed variables are not supported in Moonix v0.1");
        return MOONIX_ERR_UNSUPPORTED;
    }

    load_status = luaL_loadbufferx(state->lua, source, source_len,
                                   chunk_name ? chunk_name : "=(moonix)", "t");
    if (load_status != LUA_OK) {
        moonix_set_error(state, lua_tostring(state->lua, -1));
        lua_pop(state->lua, 1);
        return MOONIX_ERR_SYNTAX;
    }
    if (lua_dump(state->lua, byte_writer, &payload, 0) != 0) {
        lua_pop(state->lua, 1);
        free(payload.data);
        moonix_set_error(state, "could not serialize Moonix bytecode");
        return MOONIX_ERR_OOM;
    }
    lua_pop(state->lua, 1);

    chunk->data = (unsigned char *)malloc(header_size + payload.size);
    if (chunk->data == NULL) {
        free(payload.data);
        moonix_set_error(state, "out of memory");
        return MOONIX_ERR_OOM;
    }
    memcpy(chunk->data, moonix_bytecode_magic, sizeof(moonix_bytecode_magic));
    write_version(chunk->data + sizeof(moonix_bytecode_magic));
    memcpy(chunk->data + header_size, payload.data, payload.size);
    chunk->size = header_size + payload.size;
    chunk->source_line_count = source_line_count(source, source_len);
    chunk->t0_only = moonix_source_has_goto(source, source_len);
    free(payload.data);

    /* §4: the only CST consumer is Swaff.  Some full-Lua forms do not yet
     * have a representable Kliche mapping; those remain valid T0 chunks and
     * simply cannot be admitted to T1. */
    ir = ccw_swaff_lower(ccw_swaff_frontend_lua(), source, source_len,
                         chunk_name ? chunk_name : "moonix",
                         CCW_PROFILE_ON1X, CCW_SWAFF_REJECT_ON_ERROR,
                         &report, &swaff_error);
    free(swaff_error);
    chunk->on1x_ir = ir;
    return MOONIX_OK;
}

void moonix_chunk_clear(moonix_chunk *chunk)
{
    if (chunk == NULL) return;
    free(chunk->data);
    ccw_ir_module_destroy(chunk->on1x_ir);
    memset(chunk, 0, sizeof(*chunk));
}
