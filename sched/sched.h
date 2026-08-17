#ifndef CCW_SCHED_H
#define CCW_SCHED_H
#include <stddef.h>
#include <stdint.h>

typedef struct ccw_sched ccw_sched;
typedef struct ccw_plan ccw_plan;
typedef struct { int code; char message[256]; } ccw_sched_error;

ccw_sched *ccw_sched_new(const char *name, const char *manifest_dir, ccw_sched_error *err);
void ccw_sched_free(ccw_sched *s);
int ccw_sched_require_kernel(ccw_sched *, const char *name, uint32_t *node, ccw_sched_error *);
int ccw_sched_require_capability(ccw_sched *, const char *cap, const char *prefer, uint32_t *node, ccw_sched_error *);
int ccw_sched_probe_capability(ccw_sched *, const char *cap, const char *prefer, uint32_t *node);
int ccw_sched_rewrite(ccw_sched *, const char *pattern, uint32_t *node, ccw_sched_error *);
int ccw_sched_edge(ccw_sched *, uint32_t a, uint32_t b, ccw_sched_error *);
int ccw_sched_barrier(ccw_sched *, const char *label, uint32_t *node, ccw_sched_error *);
int ccw_sched_seal(ccw_sched *, ccw_plan **out, ccw_sched_error *);
void ccw_plan_free(ccw_plan *);
int ccw_plan_write(const ccw_plan *, const char *path, ccw_sched_error *);
int ccw_plan_hash(const ccw_plan *, char out[65]);
const char *ccw_plan_text(const ccw_plan *);
ccw_plan *ccw_plan_from_text(const char *);
int ccw_plan_check(const char *path, const char *manifest_dir, ccw_sched_error *);
int ccw_sched_run_script(const char *script, const char *manifest_dir, ccw_plan **out, ccw_sched_error *);

#endif
