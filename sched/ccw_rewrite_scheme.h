#ifndef CCW_REWRITE_SCHEME_H
#define CCW_REWRITE_SCHEME_H

#include "ccw_sched.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Central rewrite-scheme entry point.  Consumers provide a sealed plan;
 * manifest validation, rewrite-salvo resolution, Oeuph execution, budgets,
 * and deterministic diagnostics stay in one implementation.
 */
int ccw_rewrite_scheme_apply (const ccw_plan *plan, ccw_ir *ir,
                              const char *manifest_dir,
                              ccw_oeuph_budget budget,
                              ccw_cost_model model,
                              ccw_oeuph_stats *stats,
                              size_t stats_capacity, size_t *stats_count,
                              ccw_sched_error *error);

#ifdef __cplusplus
}
#endif

#endif
