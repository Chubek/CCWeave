/* Behavioral coverage for the eight paradigm-oriented kernels. */

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
    ccw_status status = ccw_kernel_apply(ex, id, cap, ir, NULL, &error);
    CCW_CHECK(status == CCW_OK, "%s failed to apply: %s", file,
              error ? error : "");
    free(error);
    return status == CCW_OK;
}

static ccw_ir *one_block(const char *name, ccw_profile profile,
                         ccw_node *block_out)
{
    ccw_ir *ir = ccw_ir_module_create(name, profile);
    ccw_node fn = ccw_ir_function_add(ir, "f", CCW_TY_I64);
    *block_out = ccw_ir_block_add(ir, fn, "entry");
    return ir;
}

static void test_functional_eta(ccw_executor *ex)
{
    ccw_node block = 0;
    ccw_ir *ir = one_block("eta", CCW_PROFILE_TILLY, &block);
    ccw_node call = ccw_ir_instr_build(ir, "call", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, call, "result");
    ccw_ir_instr_add_operand(ir, call, ccw_ir_operand_func(ir, "g"));
    ccw_ir_block_append_instr(ir, block, call);
    ccw_node ret = ccw_ir_instr_build(ir, "ret", CCW_TY_VOID);
    ccw_ir_instr_add_operand(ir, ret, ccw_ir_operand_reg(ir, "result"));
    ccw_ir_block_append_instr(ir, block, ret);
    if (apply_kernel(ex, "functional-eta-reduce.scm",
                     "opt.functional-eta-reduction", ir))
        CCW_CHECK_STREQ(ccw_ir_attr_lookup(
                            ir, ccw_ir_function_ref(ir, 0),
                            "analysis.opt.functional-eta-reduction.reducible?"),
                        "true");
    ccw_ir_module_destroy(ir);
}

static void test_functional_pipeline(ccw_executor *ex)
{
    ccw_node block = 0;
    ccw_ir *ir = one_block("pipeline", CCW_PROFILE_TILLY, &block);
    ccw_node ins = ccw_ir_instr_build(ir, "compose", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, ins, "p");
    ccw_ir_instr_add_operand(ir, ins, ccw_ir_operand_func(ir, "f"));
    ccw_ir_instr_add_operand(ir, ins, ccw_ir_operand_func(ir, "g"));
    ccw_ir_block_append_instr(ir, block, ins);
    if (apply_kernel(ex, "functional-pipeline-lower.scm",
                     "lower.functional-pipeline", ir))
        CCW_CHECK_STREQ(ccw_ir_instr_opcode(
                            ir, ccw_ir_block_instr_ref(ir, block, 0)),
                        "runtime.compose");
    ccw_ir_module_destroy(ir);
}

static void test_closure_specialize(ccw_executor *ex)
{
    ccw_node block = 0;
    ccw_ir *ir = one_block("closure-specialize", CCW_PROFILE_TILLY, &block);
    ccw_node ins = ccw_ir_instr_build(ir, "closure.call", CCW_TY_I64);
    ccw_ir_instr_add_operand(ir, ins, ccw_ir_operand_reg(ir, "closure"));
    ccw_ir_instr_add_operand(ir, ins, ccw_ir_operand_func(ir, "target"));
    ccw_ir_block_append_instr(ir, block, ins);
    if (apply_kernel(ex, "closure-specialize.scm",
                     "opt.functional-closure-specialization", ir))
        CCW_CHECK_STREQ(ccw_ir_attr_lookup(
                            ir, ins,
                            "analysis.opt.functional-closure-specialization.specializable?"),
                        "true");
    ccw_ir_module_destroy(ir);
}

static void test_devirtualize(ccw_executor *ex)
{
    ccw_node block = 0;
    ccw_ir *ir = one_block("devirtualize", CCW_PROFILE_TILLY, &block);
    ccw_node ins = ccw_ir_instr_build(ir, "call.virtual", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, ins, "result");
    ccw_ir_instr_add_operand(ir, ins, ccw_ir_operand_reg(ir, "receiver"));
    ccw_ir_instr_add_operand(ir, ins, ccw_ir_operand_func(ir, "method"));
    ccw_ir_block_append_instr(ir, block, ins);
    if (apply_kernel(ex, "oop-devirtualize.scm",
                     "opt.oop-devirtualization", ir))
        CCW_CHECK_STREQ(ccw_ir_instr_opcode(
                            ir, ccw_ir_block_instr_ref(ir, block, 0)), "call");
    ccw_ir_module_destroy(ir);
}

static void test_vtable(ccw_executor *ex)
{
    ccw_node block = 0;
    ccw_ir *ir = one_block("vtable", CCW_PROFILE_TILLY, &block);
    ccw_node ins = ccw_ir_instr_build(ir, "vtable.lookup", CCW_TY_PTR);
    ccw_ir_instr_set_dest(ir, ins, "method");
    ccw_ir_instr_add_operand(ir, ins, ccw_ir_operand_reg(ir, "object"));
    ccw_ir_block_append_instr(ir, block, ins);
    if (apply_kernel(ex, "oop-vtable-lower.scm", "lower.oop-vtable", ir))
        CCW_CHECK_STREQ(ccw_ir_instr_opcode(
                            ir, ccw_ir_block_instr_ref(ir, block, 0)),
                        "runtime.vtable.lookup");
    ccw_ir_module_destroy(ir);
}

static void test_null_check(ccw_executor *ex)
{
    ccw_node block = 0;
    ccw_ir *ir = one_block("null-check", CCW_PROFILE_TILLY, &block);
    ccw_node object = ccw_ir_operand_reg(ir, "object");
    for (int i = 0; i < 2; i++) {
        ccw_node check = ccw_ir_instr_build(ir, "null-check", CCW_TY_VOID);
        ccw_ir_instr_add_operand(ir, check, object);
        ccw_ir_block_append_instr(ir, block, check);
    }
    ccw_node second = ccw_ir_block_instr_ref(ir, block, 1);
    if (apply_kernel(ex, "oop-null-check.scm",
                     "opt.oop-null-check-elimination", ir))
        CCW_CHECK_STREQ(ccw_ir_attr_lookup(
                            ir, second,
                            "analysis.opt.oop-null-check-elimination.redundant?"),
                        "true");
    ccw_ir_module_destroy(ir);
}

static void test_branch_fold(ccw_executor *ex)
{
    ccw_node block = 0;
    ccw_ir *ir = one_block("branch-fold", CCW_PROFILE_TILLY, &block);
    ccw_node br = ccw_ir_instr_build(ir, "br", CCW_TY_VOID);
    ccw_ir_instr_add_operand(ir, br,
                             ccw_ir_operand_const_int(ir, CCW_TY_I1, 1));
    ccw_ir_instr_add_operand(ir, br, ccw_ir_operand_block(ir, "taken"));
    ccw_ir_instr_add_operand(ir, br, ccw_ir_operand_block(ir, "fallthrough"));
    ccw_ir_block_append_instr(ir, block, br);
    if (apply_kernel(ex, "imperative-branch-fold.scm",
                     "opt.imperative-branch-folding", ir))
        CCW_CHECK_STREQ(ccw_ir_instr_opcode(
                            ir, ccw_ir_block_instr_ref(ir, block, 0)), "jmp");
    ccw_ir_module_destroy(ir);
}

static void test_store_load(ccw_executor *ex)
{
    ccw_node block = 0;
    ccw_ir *ir = one_block("store-load", CCW_PROFILE_TILLY, &block);
    ccw_node address = ccw_ir_operand_reg(ir, "address");
    ccw_node store = ccw_ir_instr_build(ir, "store", CCW_TY_VOID);
    ccw_ir_instr_add_operand(ir, store, address);
    ccw_ir_instr_add_operand(ir, store, ccw_ir_operand_reg(ir, "value"));
    ccw_ir_block_append_instr(ir, block, store);
    ccw_node load = ccw_ir_instr_build(ir, "load", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, load, "loaded");
    ccw_ir_instr_add_operand(ir, load, address);
    ccw_ir_block_append_instr(ir, block, load);
    if (apply_kernel(ex, "imperative-store-load-forward.scm",
                     "opt.imperative-store-load-forwarding", ir))
        CCW_CHECK_STREQ(ccw_ir_instr_opcode(
                            ir, ccw_ir_block_instr_ref(ir, block, 1)), "imov");
    ccw_ir_module_destroy(ir);
}

int main(void)
{
    ccw_executor *ex = ccw_executor_create();
    CCW_CHECK(ex != NULL, "executor creation failed");
    if (ex == NULL) return ccw_test_report("new-kernels");
    CCW_CHECK(ccw_host_register_core_accessors(ex) == CCW_OK,
              "accessor registration failed");
    test_functional_eta(ex);
    test_functional_pipeline(ex);
    test_closure_specialize(ex);
    test_devirtualize(ex);
    test_vtable(ex);
    test_null_check(ex);
    test_branch_fold(ex);
    test_store_load(ex);
    ccw_executor_destroy(ex);
    return ccw_test_report("new-kernels");
}
