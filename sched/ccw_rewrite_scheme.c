#include "ccw_rewrite_scheme.h"

int
ccw_rewrite_scheme_apply (const ccw_plan *plan, ccw_ir *ir,
                          const char *manifest_dir,
                          ccw_oeuph_budget budget, ccw_cost_model model,
                          ccw_oeuph_stats *stats, size_t stats_capacity,
                          size_t *stats_count, ccw_sched_error *error)
{
  return ccw_plan_apply_rewrites (plan, ir, manifest_dir, budget, model, stats,
                                  stats_capacity, stats_count, error);
}
