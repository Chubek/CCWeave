#include "sched.h"
#include "ccw_test.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool has_edge(const char *text, unsigned from, unsigned to)
{
    char needle[64];
    snprintf(needle, sizeof(needle), "edge %u %u\n", from, to);
    return strstr(text, needle) != NULL;
}

static char *copy_string(const char *s)
{
    size_t n = strlen(s) + 1u;
    char *out = malloc(n);
    if (out != NULL) memcpy(out, s, n);
    return out;
}

static void check_pipeline(const char *script, const char *label)
{
    ccw_sched_error error;
    ccw_plan *first = NULL;
    ccw_plan *second = NULL;
    char first_hash[65];
    char second_hash[65];

    CCW_CHECK(ccw_sched_run_script(script, CCW_MANIFEST_DIR, &first, &error),
              "%s must seal: %s", label, error.message);
    if (first == NULL) return;

    CCW_CHECK(ccw_sched_run_script(script, CCW_MANIFEST_DIR, &second, &error),
              "%s must be reproducible: %s", label, error.message);
    CCW_CHECK(ccw_plan_hash(first, first_hash),
              "%s must produce a hash", label);
    CCW_CHECK(ccw_plan_hash(second, second_hash) &&
                  strcmp(first_hash, second_hash) == 0,
              "%s hash must be stable", label);
    CCW_CHECK(strcmp(ccw_plan_text(first), ccw_plan_text(second)) == 0,
              "%s serialization must be byte-identical", label);

    unsigned barrier = 0;
    unsigned backend[32];
    int backend_count = 0;
    char *copy = copy_string(ccw_plan_text(first));
    for (char *line = strtok(copy, "\n"); line; line = strtok(NULL, "\n")) {
        unsigned id;
        unsigned kind;
        char name[256];
        if (sscanf(line, "node %u %u %255s", &id, &kind, name) != 3)
            continue;
        if (kind == 3 && strcmp(name, "pre-tilly") == 0)
            barrier = id;
        if (kind == 1 &&
            (strncmp(name, "codegen-", 8) == 0 ||
             strncmp(name, "regalloc-", 9) == 0 ||
             strcmp(name, "sched-list") == 0))
            backend[backend_count++] = id;
    }
    free(copy);

    CCW_CHECK(barrier != 0, "%s must contain a pre-tilly barrier", label);
    CCW_CHECK(backend_count > 0, "%s must contain backend nodes", label);
    for (int i = 0; i < backend_count; i++) {
        CCW_CHECK(backend[i] > barrier,
                  "%s backend nodes must be declared after pre-tilly", label);
        CCW_CHECK(has_edge(ccw_plan_text(first), barrier, backend[i]),
                  "%s must order pre-tilly before backend nodes", label);
    }

    if (strcmp(label, "cephyr-o2") == 0) {
        const char *plan = ccw_plan_text(first);
        unsigned affine = 0;
        unsigned dependence = 0;
        unsigned schedule = 0;
        unsigned tiling = 0;
        unsigned vec_width = 0;
        unsigned loops = 0;
        unsigned vec_legality = 0;
        unsigned vec_plan = 0;
        unsigned vec_reduce = 0;
        unsigned vec_lower = 0;
        char *node_copy = copy_string(plan);
        for (char *line = strtok(node_copy, "\n"); line;
             line = strtok(NULL, "\n")) {
            unsigned id;
            unsigned kind;
            char name[256];
            if (sscanf(line, "node %u %u %255s", &id, &kind, name) != 3)
                continue;
            if (strcmp(name, "affine-extract") == 0) affine = id;
            else if (strcmp(name, "dep-poly") == 0) dependence = id;
            else if (strcmp(name, "isl-schedule") == 0) schedule = id;
            else if (strcmp(name, "tile-plan") == 0) tiling = id;
            else if (strcmp(name, "vec-width") == 0) vec_width = id;
            else if (strcmp(name, "loop-detect") == 0) loops = id;
            else if (strcmp(name, "vec-legality") == 0) vec_legality = id;
            else if (strcmp(name, "loop-vectorize") == 0) vec_plan = id;
            else if (strcmp(name, "vec-reduce") == 0) vec_reduce = id;
            else if (strcmp(name, "vec-lower-simde") == 0) vec_lower = id;
        }
        free(node_copy);
        CCW_CHECK(strstr(plan, "affine-extract analysis.affine") != NULL,
                  "%s must include affine extraction", label);
        CCW_CHECK(strstr(plan, "dep-poly analysis.dependence") != NULL,
                  "%s must include dependence analysis", label);
        CCW_CHECK(strstr(plan, "isl-schedule opt.schedule") != NULL,
                  "%s must include ISL scheduling", label);
        CCW_CHECK(strstr(plan, "tile-plan opt.tiling") != NULL,
                  "%s must include tile planning", label);
        CCW_CHECK(affine != 0 && dependence != 0 && schedule != 0 &&
                      tiling != 0 &&
                      has_edge(plan, affine, dependence) &&
                      has_edge(plan, dependence, schedule) &&
                      has_edge(plan, schedule, tiling),
                  "%s must preserve the ISL producer/consumer chain", label);
        CCW_CHECK(vec_width != 0 && loops != 0 && vec_legality != 0 &&
                      vec_plan != 0 && vec_reduce != 0 && vec_lower != 0 &&
                      has_edge(plan, loops, vec_legality) &&
                      has_edge(plan, vec_legality, vec_plan) &&
                      has_edge(plan, vec_plan, vec_reduce) &&
                      has_edge(plan, vec_reduce, vec_lower) &&
                      has_edge(plan, vec_lower, barrier),
                  "%s must include the SIMD producer/consumer chain", label);
    }

    char plan_path[256];
    snprintf(plan_path, sizeof(plan_path), "/tmp/ccw-%s-%ld.plan",
             label, (long)getpid());
    error.code = 0;
    CCW_CHECK(ccw_plan_write(first, plan_path, &error),
              "%s plan must be serializable: %s", label, error.message);
    CCW_CHECK(ccw_plan_check(plan_path, CCW_MANIFEST_DIR, &error),
              "%s serialized plan must pass manifest validation: %s",
              label, error.message);
    unlink(plan_path);

    ccw_plan_free(second);
    ccw_plan_free(first);
}

int main(void)
{
    check_pipeline(CEPHYR_SCHED_DIR "/O0.lua", "cephyr-o0");
    check_pipeline(CEPHYR_SCHED_DIR "/O1.lua", "cephyr-o1");
    check_pipeline(CEPHYR_SCHED_DIR "/O2.lua", "cephyr-o2");
    return ccw_test_report("cephyr-sched");
}
