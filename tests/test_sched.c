#include "../sched/ccw_sched.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ccw_ir *rewrite_sample(void)
{
  ccw_ir *ir = ccw_ir_module_create("sched-rewrite", CCW_PROFILE_TILLY);
  ccw_node fn = ccw_ir_function_add(ir, "f", CCW_TY_I64);
  ccw_node block;
  ccw_node ins;
  if (!ir || fn == 0) return ir;
  ccw_ir_function_add_param(ir, fn, CCW_TY_I64, "x");
  block = ccw_ir_block_add(ir, fn, "entry");
  ins = ccw_ir_instr_build(ir, "imul", CCW_TY_I64);
  ccw_ir_instr_set_dest(ir, ins, "t0");
  ccw_ir_instr_add_operand(ir, ins, ccw_ir_operand_reg(ir, "x"));
  ccw_ir_instr_add_operand(ir, ins,
                           ccw_ir_operand_const_int(ir, CCW_TY_I64, 8));
  ccw_ir_block_append_instr(ir, block, ins);
  return ir;
}

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

  /* SCHED §6: a selected Stdrewrite batch is applied by Oeuph, not by
   * scheduler-side IR logic. */
  {
    ccw_sched *scheduler = ccw_sched_new("rewrite", CCW_MANIFEST_DIR, &error);
    ccw_plan *rewrite_plan = NULL;
    ccw_ir *ir = NULL;
    ccw_oeuph_stats stats[1];
    size_t stats_count = 0;
    uint32_t rewrite_node = 0;
    ccw_oeuph_budget budget = ccw_oeuph_default_budget();

    if (!scheduler ||
        !ccw_sched_rewrite(scheduler, "arith.strength-reduction",
                           &rewrite_node, &error) ||
        !ccw_sched_seal(scheduler, &rewrite_plan, &error)) {
      fprintf(stderr, "could not build rewrite plan: %s\n", error.message);
      ccw_sched_free(scheduler);
      return 1;
    }
    (void)rewrite_node;
    ccw_sched_free(scheduler);
    ir = rewrite_sample();
    if (!ir ||
        !ccw_plan_apply_rewrites(rewrite_plan, ir, CCW_MANIFEST_DIR, budget,
                                 CCW_COST_PERFORMANCE, stats, 1,
                                 &stats_count, &error)) {
      fprintf(stderr, "could not apply rewrite plan: %s\n", error.message);
      ccw_ir_module_destroy(ir);
      ccw_plan_free(rewrite_plan);
      return 1;
    }
    if (stats_count != 1 ||
        strcmp(stats[0].ruleset, "arith.strength-reduction") != 0 ||
        strcmp(ccw_ir_instr_opcode(ir, ccw_ir_block_instr_ref(
                                      ir, ccw_ir_function_block_ref(
                                            ir, ccw_ir_function_ref(ir, 0), 0),
                                      0)),
               "shl") != 0) {
      fprintf(stderr, "Oeuph did not apply the selected ruleset\n");
      ccw_ir_module_destroy(ir);
      ccw_plan_free(rewrite_plan);
      return 1;
    }
    ccw_ir_module_destroy(ir);
    ccw_plan_free(rewrite_plan);
  }

  /* Kernel and barrier members are retained in a sealed plan even though
   * this rewrite-only executor skips their host-side work. */
  {
    ccw_sched *scheduler = ccw_sched_new("kernel-plan", CCW_MANIFEST_DIR,
                                         &error);
    ccw_plan *kernel_plan = NULL;
    ccw_ir *ir = rewrite_sample();
    uint32_t kernel_node = 0;
    uint32_t barrier_node = 0;
    if (!scheduler ||
        !ccw_sched_require_kernel(scheduler, "escape-analysis",
                                   &kernel_node, &error) ||
        !ccw_sched_barrier(scheduler, "after-kernel", &barrier_node,
                           &error) ||
        !ccw_sched_edge(scheduler, kernel_node, barrier_node, &error) ||
        !ccw_sched_seal(scheduler, &kernel_plan, &error) ||
        !ccw_plan_apply_rewrites(kernel_plan, ir, CCW_MANIFEST_DIR,
                                 ccw_oeuph_default_budget(),
                                 CCW_COST_PERFORMANCE, NULL, 0, NULL,
                                 &error)) {
      fprintf(stderr, "kernel-only plan execution failed: %s\n",
              error.message);
      ccw_sched_free(scheduler);
      ccw_ir_module_destroy(ir);
      ccw_plan_free(kernel_plan);
      return 1;
    }
    ccw_sched_free(scheduler);
    ccw_ir_module_destroy(ir);
    ccw_plan_free(kernel_plan);
  }

  return 0;
}
