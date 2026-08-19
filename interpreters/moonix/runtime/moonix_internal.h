#ifndef MOONIX_INTERNAL_H
#define MOONIX_INTERNAL_H

#include "moonix.h"
#include "sched.h"

struct moonix_state {
    lua_State *lua;
    moonix_tier requested_tier;
    moonix_tier active_tier;
    char *manifest_dir;
    char *sched_dir;
    ccw_plan *plans[2];
    char plan_hashes[2][65];
    char error[512];
    void *extensions;
};

void moonix_set_error(moonix_state *state, const char *message);
moonix_status moonix_jit_select_tier(moonix_state *state, moonix_tier tier);
int moonix_install_stdlib(moonix_state *state);
int moonix_source_has_close_attribute(const char *source, size_t source_len);
int moonix_source_has_goto(const char *source, size_t source_len);

#endif
