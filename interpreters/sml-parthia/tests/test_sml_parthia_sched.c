#include "sml_parthia.h"
#include "ccw_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CCW_SML_PARTHIA_SCHED_DIR
#define CCW_SML_PARTHIA_SCHED_DIR "interpreters/sml-parthia/sched"
#endif
#ifndef CCW_SML_PARTHIA_MANIFEST_DIR
#define CCW_SML_PARTHIA_MANIFEST_DIR "manifests"
#endif

static int has_edge(const char *text, unsigned from, unsigned to)
{
    char needle[64];
    snprintf(needle, sizeof(needle), "edge %u %u\n", from, to);
    return strstr(text, needle) != NULL;
}

static unsigned find_node(const char *text, const char *name)
{
    char *copy = (char *)malloc(strlen(text) + 1u);
    char *line;
    unsigned found = 0;
    if (copy == NULL) return 0;
    strcpy(copy, text);
    for (line = strtok(copy, "\n"); line; line = strtok(NULL, "\n")) {
        unsigned id, kind;
        char node_name[128];
        if (sscanf(line, "node %u %u %127s", &id, &kind, node_name) == 3 &&
            strcmp(node_name, name) == 0) {
            found = id;
            break;
        }
    }
    free(copy);
    return found;
}

static void check_level(const char *level)
{
    ccw_plan *plan = NULL;
    char *error = NULL;
    char hash[65];
    const char *text;
    unsigned pattern, pipeline, inline_node, closure, lift, tail, exceptions;
    unsigned gc, safepoint, barrier, tree, legalize, schedule, regalloc, codegen;

    CCW_CHECK(ccw_sml_parthia_load_plan(
                  level, CCW_SML_PARTHIA_MANIFEST_DIR, CCW_SML_PARTHIA_SCHED_DIR,
                  &plan, &error),
              "%s plan must load: %s", level, error ? error : "");
    free(error);
    if (plan == NULL) return;
    text = ccw_plan_text(plan);
    CCW_CHECK(ccw_plan_hash(plan, hash) && strlen(hash) == 64,
              "%s plan must have a stable hash", level);

    pattern = find_node(text, "pattern-match-lower");
    pipeline = find_node(text, "functional-pipeline-lower");
    inline_node = find_node(text, "inline");
    closure = find_node(text, "closure-convert");
    lift = find_node(text, "lambda-lift");
    tail = find_node(text, "tail-call");
    exceptions = find_node(text, "exception-lower");
    gc = find_node(text, "gc-barrier-insert");
    safepoint = find_node(text, "safepoint-insert");
    barrier = find_node(text, "parthia-runtime-lowered");
    tree = find_node(text, "isel-tree-match");
    legalize = find_node(text, "isel-legalize");
    schedule = find_node(text, "sched-list");
    regalloc = find_node(text, "regalloc-linear-scan");
    if (regalloc == 0) regalloc = find_node(text, "regalloc-linear");
    if (regalloc == 0) regalloc = find_node(text, "regalloc-graph-color");
    codegen = find_node(text, "codegen-x86-64");

    CCW_CHECK(pattern && closure && lift && tail && exceptions && gc &&
                  safepoint && barrier && tree && legalize && schedule &&
                  regalloc && codegen,
              "%s plan is missing required functional/runtime/backend stages",
              level);
    if (pattern && closure && lift && tail && exceptions && gc && safepoint &&
        barrier && tree && legalize && schedule && regalloc && codegen) {
        CCW_CHECK(has_edge(text, pattern, pipeline) &&
                      ((inline_node != 0 && has_edge(text, pipeline, inline_node) &&
                        has_edge(text, inline_node, closure)) ||
                       (inline_node == 0 && has_edge(text, pipeline, closure))),
                  "%s pattern lowering must precede closure conversion", level);
        CCW_CHECK(has_edge(text, closure, lift) && has_edge(text, lift, tail),
                  "%s closure conversion/lifting/tail-call ordering is invalid",
                  level);
        CCW_CHECK(exceptions < gc && has_edge(text, gc, safepoint) &&
                      has_edge(text, safepoint, barrier),
                  "%s runtime instrumentation ordering is invalid", level);
        CCW_CHECK(has_edge(text, barrier, tree) &&
                      has_edge(text, tree, legalize) &&
                      has_edge(text, legalize, schedule) &&
                      has_edge(text, schedule, regalloc) &&
                      has_edge(text, regalloc, codegen),
                  "%s backend ordering is invalid", level);
    }
    if (strcmp(level, "O0") == 0)
        CCW_CHECK(inline_node == 0, "O0 must not request inlining");
    else
        CCW_CHECK(inline_node != 0 && has_edge(text, inline_node, closure),
                  "%s must inline before closure conversion", level);
    if (strcmp(level, "O2") == 0) {
        CCW_CHECK(strstr(text, "vec-width analysis.vector-width") != NULL &&
                      strstr(text, "vec-legality analysis.vectorizable") != NULL &&
                      strstr(text, "loop-vectorize opt.loop-vectorize") != NULL &&
                      strstr(text, "vec-reduce opt.vector-reduction") != NULL &&
                      strstr(text, "vec-lower-simde lower.vector-simde") != NULL,
                  "O2 must include the SIMD tier");
        CCW_CHECK(strstr(text, "vm.inline-cache") == NULL &&
                      strstr(text, "vm.deopt-") == NULL,
                  "Parthia must not request JIT/deoptimization capabilities");
    }
    ccw_plan_free(plan);
}

int main(void)
{
    char *error = NULL;
    ccw_plan *default_plan = NULL;
    CCW_CHECK(ccw_sml_parthia_load_plan(NULL, CCW_SML_PARTHIA_MANIFEST_DIR,
                                        CCW_SML_PARTHIA_SCHED_DIR,
                                        &default_plan, &error),
              "NULL level must select O2: %s", error ? error : "");
    free(error);
    ccw_plan_free(default_plan);
    check_level("O0");
    check_level("O1");
    check_level("O2");
    return ccw_test_report("sml-parthia-sched");
}
