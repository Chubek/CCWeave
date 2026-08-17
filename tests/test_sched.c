#include "../sched/sched.h"
#include <stdio.h>
#include <string.h>

int main(void) {
  ccw_sched_error error;
  ccw_plan *plan = NULL;
  char hash[65];
  if (!ccw_sched_run_script(CCW_SCHED_FIXTURE, CCW_MANIFEST_DIR, &plan, &error)) {
    fprintf(stderr, "%s\n", error.message);
    return 1;
  }
  if (!strstr(ccw_plan_text(plan), "node 1 1 escape-analysis") ||
      !strstr(ccw_plan_text(plan), "node 3 2 arith.*")) {
    fprintf(stderr, "unexpected plan\n");
    ccw_plan_free(plan);
    return 1;
  }
  if (!ccw_plan_hash(plan, hash) || strlen(hash) != 64) {
    fprintf(stderr, "unstable hash\n");
    ccw_plan_free(plan);
    return 1;
  }
  ccw_plan_free(plan);
  return 0;
}
