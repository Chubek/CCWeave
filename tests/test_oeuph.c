/* Oeuph conformance (§7.4): determinism under a fixed seed and budget,
 * budgets actually halting saturation, and per-ruleset diagnostics. */

#include "ccw_oeuph.h"
#include "ccw_test.h"

#include <stdlib.h>

#ifndef CCW_REWRITE_SALVO_DIR
#define CCW_REWRITE_SALVO_DIR "rewrite-salvo"
#endif

static ccw_ir *sample(void)
{
    ccw_ir *m = ccw_ir_module_create("oeuph-sample", CCW_PROFILE_TILLY);
    ccw_node fn = ccw_ir_function_add(m, "f", CCW_TY_I64);
    ccw_ir_function_add_param(m, fn, CCW_TY_I64, "x");
    ccw_node blk = ccw_ir_block_add(m, fn, "entry");

    ccw_node mul = ccw_ir_instr_build(m, "imul", CCW_TY_I64);
    ccw_ir_instr_set_dest(m, mul, "t0");
    ccw_ir_instr_add_operand(m, mul, ccw_ir_operand_reg(m, "x"));
    ccw_ir_instr_add_operand(m, mul, ccw_ir_operand_const_int(m, CCW_TY_I64, 8));
    ccw_ir_block_append_instr(m, blk, mul);

    ccw_node add = ccw_ir_instr_build(m, "iadd", CCW_TY_I64);
    ccw_ir_instr_set_dest(m, add, "t1");
    ccw_ir_instr_add_operand(m, add, ccw_ir_operand_reg(m, "x"));
    ccw_ir_instr_add_operand(m, add, ccw_ir_operand_const_int(m, CCW_TY_I64, 0));
    ccw_ir_block_append_instr(m, blk, add);
    return m;
}

static char *run_once(const char *ruleset_dir, ccw_oeuph_budget budget,
                      ccw_cost_model model,
                      ccw_oeuph_stats *stats)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s/rules.scm",
             CCW_REWRITE_SALVO_DIR, ruleset_dir);
    char *err = NULL;
    ccw_oeuph_ruleset *rs = ccw_oeuph_ruleset_load(path, &err);
    if (rs == NULL) {
        fprintf(stderr, "ruleset load failed: %s\n", err ? err : "");
        free(err);
        return NULL;
    }
    ccw_ir *m = sample();
    ccw_oeuph_run(m, rs, budget, model, stats, &err);
    free(err);
    char *text = ccw_ir_print(m);
    ccw_ir_module_destroy(m);
    ccw_oeuph_ruleset_destroy(rs);
    return text;
}

int main(void)
{
    /* --- rulesets declare a name and load their rules --- */
    char path[512];
    snprintf(path, sizeof(path), "%s/arith/identity/rules.scm",
             CCW_REWRITE_SALVO_DIR);
    char *err = NULL;
    ccw_oeuph_ruleset *arith = ccw_oeuph_ruleset_load(path, &err);
    CCW_CHECK(arith != NULL, "arith ruleset failed to load: %s", err ? err : "");
    free(err);
    if (arith != NULL) {
        CCW_CHECK_STREQ(ccw_oeuph_ruleset_name(arith), "arith.identity");
        CCW_CHECK(ccw_oeuph_ruleset_size(arith) == 18,
                  "expected 18 rules, got %d", ccw_oeuph_ruleset_size(arith));
        ccw_oeuph_ruleset_destroy(arith);
    }

    /* --- power-oriented rules are a loadable, named ruleset --- */
    snprintf(path, sizeof(path), "%s/power/rules.scm", CCW_REWRITE_SALVO_DIR);
    err = NULL;
    ccw_oeuph_ruleset *power = ccw_oeuph_ruleset_load(path, &err);
    CCW_CHECK(power != NULL, "power ruleset failed to load: %s", err ? err : "");
    free(err);
    if (power != NULL) {
        CCW_CHECK_STREQ(ccw_oeuph_ruleset_name(power), "power.consumption");
        CCW_CHECK(ccw_oeuph_ruleset_size(power) == 14,
                  "expected 14 power rules, got %d", ccw_oeuph_ruleset_size(power));
        ccw_oeuph_ruleset_destroy(power);
    }

    /* --- determinism: same seed + budget => byte-identical extraction --- */
    ccw_oeuph_budget budget = ccw_oeuph_default_budget();
    ccw_oeuph_stats s1, s2;
    char *a = run_once("arith/strength-reduction", budget,
                       CCW_COST_PERFORMANCE, &s1);
    char *b = run_once("arith/strength-reduction", budget,
                       CCW_COST_PERFORMANCE, &s2);
    CCW_CHECK(a != NULL && b != NULL && strcmp(a, b) == 0,
              "extraction must be reproducible across runs");

    /* --- optimization picks the cheaper equivalent form --- */
    CCW_CHECK(a != NULL && strstr(a, "shl") != NULL,
              "performance cost model must prefer shl over imul:\n%s", a ? a : "");

    /* --- diagnostics are emitted per ruleset --- */
    CCW_CHECK_STREQ(s1.ruleset, "arith.strength-reduction");
    CCW_CHECK(s1.matches > 0, "match statistics must be reported");
    CCW_CHECK(s1.iterations > 0, "iteration count must be reported");
    CCW_CHECK(s1.matches == s2.matches && s1.applications == s2.applications,
              "diagnostics must be reproducible too");
    free(a);
    free(b);

    /* --- power rules perform the same low-work extraction --- */
    ccw_oeuph_stats power_stats;
    char *power_text = run_once("power", budget, CCW_COST_PERFORMANCE,
                                &power_stats);
    CCW_CHECK(power_text != NULL && strstr(power_text, "shl") != NULL,
              "power rules must prefer a shift for imul by eight");
    CCW_CHECK_STREQ(power_stats.ruleset, "power.consumption");
    free(power_text);

    /* --- budgets halt saturation --- */
    ccw_oeuph_budget tiny = ccw_oeuph_default_budget();
    tiny.max_iterations = 1;
    ccw_oeuph_stats s3;
    char *c = run_once("arith/strength-reduction", tiny,
                       CCW_COST_PERFORMANCE, &s3);
    CCW_CHECK(!s3.saturated, "a one-iteration budget must stop saturation");
    CCW_CHECK_STREQ(s3.budget_hit, "iterations");
    free(c);

    ccw_oeuph_budget narrow = ccw_oeuph_default_budget();
    narrow.max_nodes = 2;
    ccw_oeuph_stats s4;
    char *d = run_once("arith/strength-reduction", narrow,
                       CCW_COST_PERFORMANCE, &s4);
    CCW_CHECK(!s4.saturated, "a two-node budget must stop saturation");
    CCW_CHECK_STREQ(s4.budget_hit, "nodes");
    free(d);

    /* --- normalization uses the same rules, a different cost model --- */
    ccw_oeuph_stats s5;
    char *e = run_once("arith/strength-reduction", budget,
                       CCW_COST_CANONICAL, &s5);
    CCW_CHECK(e != NULL, "normalization run failed");
    free(e);

    return ccw_test_report("oeuph");
}
