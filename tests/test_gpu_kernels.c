/* Behavioral coverage for the seven GPU-accelerated hipSYCL kernels.
 *
 * Each test loads a kernel, applies it to a minimal IR module, and
 * verifies that the expected analysis facts are present.  The test
 * exercises the kernel-info, kernel-capabilities, and kernel-apply
 * exports as required by the CCWeave spec (§2.2). */

#include "GlueSTD.h"
#include "ccw_host_accessors.h"
#include "ccw_ir.h"
#include "ccw_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CCW_KERNEL_DIR
#define CCW_KERNEL_DIR "kernels"
#endif

static bool apply_kernel(ccw_executor *ex, const char *file,
                         const char *cap, ccw_ir *ir)
{
    char path[512];
    char *error = NULL;
    snprintf(path, sizeof(path), "%s/%s", CCW_KERNEL_DIR, file);
    int id = ccw_kernel_load(ex, path, &error);
    CCW_CHECK(id >= 0, "%s failed to load: %s", file, error ? error : "");
    free(error);
    if (id < 0) return false;
    error = NULL;
    ccw_status st = ccw_kernel_apply(ex, id, cap, ir, NULL, &error);
    CCW_CHECK(st == CCW_OK, "%s failed to apply: %s", file,
              error ? error : "");
    free(error);
    return st == CCW_OK;
}

static ccw_ir *make_module(const char *name, ccw_profile profile,
                           ccw_node *block_out)
{
    ccw_ir *ir = ccw_ir_module_create(name, profile);
    ccw_node fn = ccw_ir_function_add(ir, "test_fn", CCW_TY_I64);
    *block_out = ccw_ir_block_add(ir, fn, "entry");
    return ir;
}

/* ==================================================================
 * 1. gpu-parallel-parse — parses a parse instruction and tags it.
 * ================================================================== */
static void test_gpu_parallel_parse(ccw_executor *ex)
{
    ccw_node block = 0;
    ccw_ir *ir = make_module("gpu-parse", CCW_PROFILE_TILLY, &block);

    /* Add a parse instruction. */
    ccw_node parse = ccw_ir_instr_build(ir, "parse", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, parse, "tokens");
    ccw_ir_instr_add_operand(ir, parse,
                             ccw_ir_operand_const_int(ir, CCW_TY_I64, 42));
    ccw_ir_block_append_instr(ir, block, parse);

    if (apply_kernel(ex, "gpu-parallel-parse.scm",
                     "parse.gpu-parallel", ir)) {
        CCW_CHECK_STREQ(
            ccw_ir_attr_lookup(ir, parse,
                               "analysis.parse.gpu-parallel.gpu-dispatched"),
            "true");
        CCW_CHECK_STREQ(
            ccw_ir_attr_lookup(ir, 0,
                               "analysis.parse.gpu-parallel.status"),
            "gpu-parsed");
        CCW_CHECK_STREQ(
            ccw_ir_attr_lookup(ir, 0,
                               "analysis.parse.gpu-parallel.backend"),
            "hipSYCL");
    }
    ccw_ir_module_destroy(ir);
}

/* ==================================================================
 * 2. gpu-pattern-match — annotates pattern-match instructions.
 * ================================================================== */
static void test_gpu_pattern_match(ccw_executor *ex)
{
    ccw_node block = 0;
    ccw_ir *ir = make_module("gpu-pmatch", CCW_PROFILE_TILLY, &block);

    /* Build a pattern-match with 10 arms (deep match → GPU binary tree). */
    ccw_node match = ccw_ir_instr_build(ir, "pattern-match", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, match, "result");
    ccw_ir_instr_add_operand(ir, match,
                             ccw_ir_operand_reg(ir, "scrutinee"));
    ccw_ir_instr_add_operand(ir, match,
                             ccw_ir_operand_const_int(ir, CCW_TY_I64, 1));
    ccw_ir_instr_add_operand(ir, match,
                             ccw_ir_operand_const_int(ir, CCW_TY_I64, 2));
    ccw_ir_instr_add_operand(ir, match,
                             ccw_ir_operand_const_int(ir, CCW_TY_I64, 3));
    ccw_ir_instr_add_operand(ir, match,
                             ccw_ir_operand_const_int(ir, CCW_TY_I64, 4));
    ccw_ir_instr_add_operand(ir, match,
                             ccw_ir_operand_const_int(ir, CCW_TY_I64, 5));
    ccw_ir_instr_add_operand(ir, match,
                             ccw_ir_operand_const_int(ir, CCW_TY_I64, 6));
    ccw_ir_instr_add_operand(ir, match,
                             ccw_ir_operand_const_int(ir, CCW_TY_I64, 7));
    ccw_ir_instr_add_operand(ir, match,
                             ccw_ir_operand_const_int(ir, CCW_TY_I64, 8));
    ccw_ir_instr_add_operand(ir, match,
                             ccw_ir_operand_const_int(ir, CCW_TY_I64, 9));
    ccw_ir_block_append_instr(ir, block, match);

    if (apply_kernel(ex, "gpu-pattern-match.scm",
                     "lower.gpu-pattern-match", ir)) {
        CCW_CHECK_STREQ(
            ccw_ir_attr_lookup(ir, match,
                               "analysis.lower.gpu-pattern-match.gpu-lowered"),
            "true");
        CCW_CHECK_STREQ(
            ccw_ir_attr_lookup(ir, match,
                               "analysis.lower.gpu-pattern-match.backend"),
            "hipSYCL");
        /* > 8 arms should get a binary decision tree. */
        CCW_CHECK_STREQ(
            ccw_ir_attr_lookup(ir, match,
                               "analysis.lower.gpu-pattern-match.decision-tree"),
            "gpu-binary");
    }
    ccw_ir_module_destroy(ir);
}

/* ==================================================================
 * 3. gpu-dataflow — annotates blocks with dataflow analysis facts.
 * ================================================================== */
static void test_gpu_dataflow(ccw_executor *ex)
{
    ccw_node block = 0;
    ccw_ir *ir = make_module("gpu-dflow", CCW_PROFILE_TILLY, &block);

    /* Add an instruction with a destination (def) and operands (uses). */
    ccw_node add = ccw_ir_instr_build(ir, "iadd", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, add, "x");
    ccw_ir_instr_add_operand(ir, add, ccw_ir_operand_reg(ir, "y"));
    ccw_ir_instr_add_operand(ir, add, ccw_ir_operand_reg(ir, "z"));
    ccw_ir_block_append_instr(ir, block, add);

    if (apply_kernel(ex, "gpu-dataflow.scm",
                     "analysis.gpu-dataflow", ir)) {
        CCW_CHECK_STREQ(
            ccw_ir_attr_lookup(ir, block,
                               "analysis.analysis.gpu-dataflow.gpu-analyzed"),
            "true");
        CCW_CHECK_STREQ(
            ccw_ir_attr_lookup(ir, 0,
                               "analysis.analysis.gpu-dataflow.status"),
            "gpu-analyzed");
    }
    ccw_ir_module_destroy(ir);
}

/* ==================================================================
 * 4. gpu-batch-inline — computes inlining scores for call sites.
 * ================================================================== */
static void test_gpu_batch_inline(ccw_executor *ex)
{
    ccw_node block = 0;
    ccw_ir *ir = make_module("gpu-inline", CCW_PROFILE_TILLY, &block);

    ccw_node call1 = ccw_ir_instr_build(ir, "call", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, call1, "r1");
    ccw_ir_instr_add_operand(ir, call1,
                             ccw_ir_operand_func(ir, "callee1"));
    ccw_ir_instr_add_operand(ir, call1,
                             ccw_ir_operand_reg(ir, "a1"));
    ccw_ir_instr_add_operand(ir, call1,
                             ccw_ir_operand_reg(ir, "a2"));
    ccw_ir_block_append_instr(ir, block, call1);

    ccw_node call2 = ccw_ir_instr_build(ir, "call", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, call2, "r2");
    ccw_ir_instr_add_operand(ir, call2,
                             ccw_ir_operand_func(ir, "callee2"));
    ccw_ir_instr_add_operand(ir, call2,
                             ccw_ir_operand_reg(ir, "b1"));
    ccw_ir_block_append_instr(ir, block, call2);

    if (apply_kernel(ex, "gpu-batch-inline.scm",
                     "opt.gpu-batch-inline", ir)) {
        CCW_CHECK_STREQ(
            ccw_ir_attr_lookup(ir, call1,
                               "analysis.opt.gpu-batch-inline.gpu-evaluated"),
            "true");
        CCW_CHECK_STREQ(
            ccw_ir_attr_lookup(ir, call1,
                               "analysis.opt.gpu-batch-inline.backend"),
            "hipSYCL");
        CCW_CHECK_STREQ(
            ccw_ir_attr_lookup(ir, 0,
                               "analysis.opt.gpu-batch-inline.status"),
            "gpu-evaluated");
    }
    ccw_ir_module_destroy(ir);
}

/* ==================================================================
 * 5. gpu-const-fold — identifies foldable constant expressions.
 * ================================================================== */
static void test_gpu_const_fold(ccw_executor *ex)
{
    ccw_node block = 0;
    ccw_ir *ir = make_module("gpu-cfold", CCW_PROFILE_TILLY, &block);

    /* Add a binary instruction with two constant operands (foldable). */
    ccw_node add = ccw_ir_instr_build(ir, "iadd", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, add, "sum");
    ccw_ir_instr_add_operand(ir, add,
                             ccw_ir_operand_const_int(ir, CCW_TY_I64, 3));
    ccw_ir_instr_add_operand(ir, add,
                             ccw_ir_operand_const_int(ir, CCW_TY_I64, 5));
    ccw_ir_block_append_instr(ir, block, add);

    if (apply_kernel(ex, "gpu-const-fold.scm",
                     "opt.gpu-const-fold", ir)) {
        CCW_CHECK_STREQ(
            ccw_ir_attr_lookup(ir, add,
                               "analysis.opt.gpu-const-fold.gpu-foldable"),
            "true");
        CCW_CHECK_STREQ(
            ccw_ir_attr_lookup(ir, 0,
                               "analysis.opt.gpu-const-fold.status"),
            "gpu-analyzed");
    }
    ccw_ir_module_destroy(ir);
}

/* ==================================================================
 * 6. gpu-regalloc — annotates blocks with register allocation facts.
 * ================================================================== */
static void test_gpu_regalloc(ccw_executor *ex)
{
    ccw_node block = 0;
    ccw_ir *ir = make_module("gpu-regalloc", CCW_PROFILE_TILLY, &block);

    /* Add an instruction that defines a virtual register. */
    ccw_node mov = ccw_ir_instr_build(ir, "mov", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, mov, "vreg1");
    ccw_ir_instr_add_operand(ir, mov,
                             ccw_ir_operand_const_int(ir, CCW_TY_I64, 42));
    ccw_ir_block_append_instr(ir, block, mov);

    ccw_node add = ccw_ir_instr_build(ir, "iadd", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, add, "vreg2");
    ccw_ir_instr_add_operand(ir, add, ccw_ir_operand_reg(ir, "vreg1"));
    ccw_ir_instr_add_operand(ir, add,
                             ccw_ir_operand_const_int(ir, CCW_TY_I64, 1));
    ccw_ir_block_append_instr(ir, block, add);

    if (apply_kernel(ex, "gpu-regalloc.scm",
                     "opt.gpu-regalloc", ir)) {
        CCW_CHECK_STREQ(
            ccw_ir_attr_lookup(ir, block,
                               "analysis.opt.gpu-regalloc.gpu-processed"),
            "true");
        CCW_CHECK_STREQ(
            ccw_ir_attr_lookup(ir, 0,
                               "analysis.opt.gpu-regalloc.status"),
            "gpu-allocated");
    }
    ccw_ir_module_destroy(ir);
}

/* ==================================================================
 * 7. gpu-codegen — annotates functions for parallel code generation.
 * ================================================================== */
static void test_gpu_codegen(ccw_executor *ex)
{
    ccw_node block = 0;
    ccw_ir *ir = make_module("gpu-codegen", CCW_PROFILE_TILLY, &block);

    ccw_node ret = ccw_ir_instr_build(ir, "ret", CCW_TY_VOID);
    ccw_ir_instr_add_operand(ir, ret,
                             ccw_ir_operand_const_int(ir, CCW_TY_I64, 0));
    ccw_ir_block_append_instr(ir, block, ret);

    if (apply_kernel(ex, "gpu-codegen.scm",
                     "lower.gpu-codegen", ir)) {
        ccw_node fn = ccw_ir_function_ref(ir, 0);
        CCW_CHECK_STREQ(
            ccw_ir_attr_lookup(ir, fn,
                               "analysis.lower.gpu-codegen.gpu-codegen"),
            "true");
        CCW_CHECK_STREQ(
            ccw_ir_attr_lookup(ir, fn,
                               "analysis.lower.gpu-codegen.target-arch"),
            "x86-64");
        CCW_CHECK_STREQ(
            ccw_ir_attr_lookup(ir, 0,
                               "analysis.lower.gpu-codegen.status"),
            "gpu-codegen-ready");
    }
    ccw_ir_module_destroy(ir);
}

/* ==================================================================
 * 8. Capability and error-path checks.
 * ================================================================== */
static void test_capability_checks(ccw_executor *ex)
{
    /* Verify that each kernel reports its expected capabilities. */
    static const struct {
        const char *file;
        const char *cap;
    } checks[] = {
        { "gpu-parallel-parse.scm", "parse.gpu-parallel" },
        { "gpu-pattern-match.scm",  "lower.gpu-pattern-match" },
        { "gpu-dataflow.scm",       "analysis.gpu-dataflow" },
        { "gpu-batch-inline.scm",   "opt.gpu-batch-inline" },
        { "gpu-const-fold.scm",     "opt.gpu-const-fold" },
        { "gpu-regalloc.scm",       "opt.gpu-regalloc" },
        { "gpu-codegen.scm",        "lower.gpu-codegen" },
    };

    for (size_t i = 0; i < sizeof(checks) / sizeof(checks[0]); i++) {
        char path[512], *error = NULL;
        snprintf(path, sizeof(path), "%s/%s",
                 CCW_KERNEL_DIR, checks[i].file);
        int id = ccw_kernel_load(ex, path, &error);
        CCW_CHECK(id >= 0, "GPU kernel %s failed to load: %s",
                  checks[i].file, error ? error : "");
        free(error);
        if (id < 0) continue;

        /* Verify the capability is present. */
        int ncap = ccw_kernel_capability_count(ex, id);
        CCW_CHECK(ncap >= 1, "%s reports no capabilities",
                  checks[i].file);
        bool found = false;
        for (int j = 0; j < ncap; j++) {
            const char *cap = ccw_kernel_capability(ex, id, j);
            if (cap && strcmp(cap, checks[i].cap) == 0) {
                found = true;
                break;
            }
        }
        CCW_CHECK(found, "%s does not report capability %s",
                  checks[i].file, checks[i].cap);

        /* Verify that applying an unsupported capability raises an error. */
        ccw_ir *ir = ccw_ir_module_create("capcheck", CCW_PROFILE_TILLY);
        ccw_node fn = ccw_ir_function_add(ir, "f", CCW_TY_VOID);
        ccw_node blk = ccw_ir_block_add(ir, fn, "entry");
        ccw_node br = ccw_ir_instr_build(ir, "br", CCW_TY_VOID);
        ccw_ir_instr_add_operand(ir, br, ccw_ir_operand_block(ir, "entry"));
        ccw_ir_block_append_instr(ir, blk, br);

        error = NULL;
        ccw_status st = ccw_kernel_apply(ex, id, "nonexistent.capability",
                                         ir, NULL, &error);
        CCW_CHECK(st != CCW_OK,
                  "%s should reject unsupported capability", checks[i].file);
        free(error);
        ccw_ir_module_destroy(ir);
    }
}

int main(void)
{
    ccw_executor *ex = ccw_executor_create();
    CCW_CHECK(ex != NULL, "executor creation failed");
    CCW_CHECK(ccw_host_register_core_accessors(ex) == CCW_OK,
              "core accessor registration failed");

    /* Run the behavioural tests. */
    test_gpu_parallel_parse(ex);
    test_gpu_pattern_match(ex);
    test_gpu_dataflow(ex);
    test_gpu_batch_inline(ex);
    test_gpu_const_fold(ex);
    test_gpu_regalloc(ex);
    test_gpu_codegen(ex);
    test_capability_checks(ex);

    ccw_executor_destroy(ex);
    return ccw_test_report("GPU hipSYCL kernels");
}
