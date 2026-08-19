/* Oeuph — equality-saturation rewrite engine over the canonical
 * in-memory Weave IR (§7). Every rule asserts an equivalence;
 * optimization and normalization differ only in the cost model. */

#ifndef CCW_OEUPH_H
#define CCW_OEUPH_H

#include "ccw_ir.h"

#ifdef __cplusplus
extern "C"
{
#endif

  /* §7.4: budgets. Hitting any budget stops saturation and proceeds to
   * extraction — saturation is best-effort, not guaranteed. */
  typedef struct
  {
    int max_nodes;
    int max_classes;
    int max_iterations;
    double max_seconds;
    uint64_t seed; /* fixed seed + budget => reproducible output */
  } ccw_oeuph_budget;

  ccw_oeuph_budget ccw_oeuph_default_budget (void);

  /* Extraction cost models (§7.2). */
  typedef enum
  {
    CCW_COST_PERFORMANCE = 0, /* optimization */
    CCW_COST_CANONICAL        /* normalization */
  } ccw_cost_model;

  /* Pattern language: a small s-expression over opcodes, variables, and
   * integer constants, e.g. (imul ?x (iconst 8)). */
  typedef struct ccw_oeuph_ruleset ccw_oeuph_ruleset;

  /* Every ruleset declares a name; rules are unordered within it. */
  ccw_oeuph_ruleset *ccw_oeuph_ruleset_create (const char *name);
  void ccw_oeuph_ruleset_destroy (ccw_oeuph_ruleset *rs);
  const char *ccw_oeuph_ruleset_name (const ccw_oeuph_ruleset *rs);
  int ccw_oeuph_ruleset_size (const ccw_oeuph_ruleset *rs);

  /* Adds an equivalence. bidirectional rules are also applied right-to-left.
   * `side_condition` may be NULL. Returns CCW_OK or an error status. */
  typedef bool (*ccw_oeuph_side_condition) (int64_t const_value);

  ccw_status ccw_oeuph_rule_add (ccw_oeuph_ruleset *rs, const char *rule_name,
                                 const char *lhs_pattern,
                                 const char *rhs_pattern, bool bidirectional,
                                 ccw_oeuph_side_condition side_condition,
                                 char **error_message);

  /* Loads a .scm ruleset file from stdrewrite/. */
  ccw_oeuph_ruleset *ccw_oeuph_ruleset_load (const char *path,
                                             char **error_message);

  /* §7.4: per-ruleset diagnostics, so rule explosion is observable. */
  typedef struct
  {
    char ruleset[64];
    int iterations;
    int nodes;
    int classes;
    int matches;         /* patterns matched  */
    int applications;    /* unions performed  */
    bool saturated;      /* false => a budget stopped us */
    char budget_hit[32]; /* "" when saturated */
  } ccw_oeuph_stats;

  /* Runs saturation over `ir` and rewrites it in place under `model`. */
  ccw_status ccw_oeuph_run (ccw_ir *ir, const ccw_oeuph_ruleset *rs,
                            ccw_oeuph_budget budget, ccw_cost_model model,
                            ccw_oeuph_stats *stats, char **error_message);

#ifdef __cplusplus
}
#endif
#endif /* CCW_OEUPH_H */
