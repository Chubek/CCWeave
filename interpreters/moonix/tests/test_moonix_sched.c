#include "sched.h"
#include "ccw_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int has_edge(const char *text, unsigned from, unsigned to)
{
    char needle[64];
    snprintf(needle, sizeof(needle), "edge %u %u\n", from, to);
    return strstr(text, needle) != NULL;
}

static void check_plan(const char *name)
{
    const char *expected_hash =
        strcmp(name, "T1") == 0
            ? "a967076f76b9148412e9f5068088e4ce65fb848ccd0709dddf2521cd5ab3503d"
            : "edf26d12ec435997e160f0a252fecc3e90832588127c95ca0b6426cdfe01b435";
    char path[512];
    ccw_sched_error error = {0};
    ccw_plan *first = NULL;
    ccw_plan *second = NULL;
    char hash1[65], hash2[65];
    char *copy;
    unsigned barrier = 0;
    unsigned vm[32], backend[16];
    size_t vm_count = 0, backend_count = 0;
    int has_ic = 0, has_gc = 0, has_safepoint = 0;

    snprintf(path, sizeof(path), "%s/%s.lua", MOONIX_SCHED_DIR, name);
    CCW_CHECK(ccw_sched_run_script(path, CCW_MANIFEST_DIR, &first, &error),
              "%s must seal: %s", name, error.message);
    CCW_CHECK(ccw_sched_run_script(path, CCW_MANIFEST_DIR, &second, &error),
              "%s second seal failed: %s", name, error.message);
    if (first == NULL || second == NULL) goto done;
    CCW_CHECK(ccw_plan_hash(first, hash1) && ccw_plan_hash(second, hash2) &&
                  strcmp(hash1, hash2) == 0,
              "%s plan hash must be stable", name);
    CCW_CHECK(strcmp(hash1, expected_hash) == 0,
              "%s plan hash drifted: expected %s, got %s",
              name, expected_hash, hash1);

    copy = malloc(strlen(ccw_plan_text(first)) + 1u);
    strcpy(copy, ccw_plan_text(first));
    for (char *line = strtok(copy, "\n"); line; line = strtok(NULL, "\n")) {
        unsigned id, kind;
        char node_name[128], rest[512] = "";
        if (sscanf(line, "node %u %u %127s %511[^\n]",
                   &id, &kind, node_name, rest) < 3)
            continue;
        if (kind == 3 && strcmp(node_name, "on1x-complete") == 0)
            barrier = id;
        if (kind == 1 && strstr(rest, "vm.") != NULL) {
            vm[vm_count++] = id;
            has_ic |= strstr(rest, "vm.inline-cache") != NULL;
            has_gc |= strstr(rest, "vm.gc-barrier-insertion") != NULL;
            has_safepoint |= strstr(rest, "vm.safepoint-insertion") != NULL;
        }
        if (kind == 1 && strstr(rest, "codegen.x86-64") != NULL)
            backend[backend_count++] = id;
    }
    free(copy);
    CCW_CHECK(barrier != 0, "%s lacks on1x-complete", name);
    CCW_CHECK(has_ic && has_gc && has_safepoint,
              "%s lacks mandatory VM capabilities", name);
    for (size_t i = 0; i < vm_count; ++i)
        CCW_CHECK(vm[i] < barrier &&
                      has_edge(ccw_plan_text(first), vm[i], barrier),
                  "%s vm node must precede barrier", name);
    for (size_t i = 0; i < backend_count; ++i)
        CCW_CHECK(backend[i] > barrier &&
                      has_edge(ccw_plan_text(first), barrier, backend[i]),
                  "%s backend must follow barrier", name);
done:
    ccw_plan_free(second);
    ccw_plan_free(first);
}

int main(void)
{
    check_plan("T1");
    check_plan("T2");
    return ccw_test_report("moonix-sched");
}
