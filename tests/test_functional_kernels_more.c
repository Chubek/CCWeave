/* Functional coverage for the third batch of promoted kernel placeholders. */

#include "GlueSTD.h"
#include "ccw_host_accessors.h"
#include "ccw_ir.h"
#include "ccw_test.h"

#include <stdlib.h>

#ifndef CCW_KERNEL_DIR
#define CCW_KERNEL_DIR "kernels"
#endif

static char *kernel_path(const char *file)
{
    size_t size = strlen(CCW_KERNEL_DIR) + strlen(file) + 2u;
    char *path = (char *)malloc(size);
    if (path != NULL) snprintf(path, size, "%s/%s", CCW_KERNEL_DIR, file);
    return path;
}

static bool apply_kernel(ccw_executor *executor, const char *file,
                         const char *capability, ccw_ir *ir)
{
    char *path = kernel_path(file);
    char *error = NULL;
    int kernel = ccw_kernel_load(executor, path, &error);
    CCW_CHECK(kernel >= 0, "%s failed to load: %s", file,
              error ? error : "");
    free(error);
    free(path);
    if (kernel < 0) return false;

    error = NULL;
    ccw_status status = ccw_kernel_apply(
        executor, kernel, capability, ir, NULL, &error);
    CCW_CHECK(status == CCW_OK, "%s failed to apply: %s", file,
              error ? error : "");
    free(error);
    if (status != CCW_OK) return false;

    error = NULL;
    CCW_CHECK(ccw_ir_validate(ir, &error) == CCW_OK,
              "%s produced invalid IR: %s", file, error ? error : "");
    free(error);

    char *text = ccw_ir_print(ir);
    error = NULL;
    ccw_ir *roundtrip = ccw_ir_parse(text, &error);
    CCW_CHECK(roundtrip != NULL && ccw_ir_equal(ir, roundtrip),
              "%s output did not round-trip: %s", file,
              error ? error : "");
    free(error);
    free(text);
    ccw_ir_module_destroy(roundtrip);
    return true;
}

static ccw_ir *single_block_module(const char *name, ccw_profile profile,
                                   ccw_node *block_out)
{
    ccw_ir *ir = ccw_ir_module_create(name, profile);
    ccw_node function = ccw_ir_function_add(ir, "f", CCW_TY_I64);
    *block_out = ccw_ir_block_add(ir, function, "entry");
    return ir;
}

static void test_atomics(ccw_executor *executor)
{
    ccw_node block = 0;
    ccw_ir *ir = single_block_module("atomics", CCW_PROFILE_TILLY, &block);
    ccw_node load = ccw_ir_instr_build(ir, "atomic.load", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, load, "value");
    ccw_ir_instr_add_operand(ir, load, ccw_ir_operand_reg(ir, "address"));
    ccw_ir_block_append_instr(ir, block, load);

    if (apply_kernel(executor, "atomics-lower.scm", "lower.atomics", ir)) {
        ccw_node lowered = ccw_ir_block_instr_ref(ir, block, 0);
        CCW_CHECK_STREQ(ccw_ir_instr_opcode(ir, lowered),
                        "atomic-libcall.load.seq-cst");
        CCW_CHECK_STREQ(ccw_ir_instr_dest(ir, lowered), "value");
    }
    ccw_ir_module_destroy(ir);
}

static void test_bitfield(ccw_executor *executor)
{
    ccw_node block = 0;
    ccw_ir *ir = single_block_module("bitfield", CCW_PROFILE_TILLY, &block);
    ccw_node extract =
        ccw_ir_instr_build(ir, "bitfield-extract", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, extract, "field");
    ccw_ir_instr_add_operand(ir, extract, ccw_ir_operand_reg(ir, "storage"));
    ccw_ir_instr_add_operand(
        ir, extract, ccw_ir_operand_const_int(ir, CCW_TY_I64, 8));
    ccw_ir_instr_add_operand(
        ir, extract, ccw_ir_operand_const_int(ir, CCW_TY_I64, 4));
    ccw_ir_block_append_instr(ir, block, extract);

    if (apply_kernel(executor, "bitfield-lower.scm", "lower.bitfield", ir)) {
        CCW_CHECK(ccw_ir_block_instr_count(ir, block) == 2,
                  "bitfield extraction must become two instructions");
        CCW_CHECK_STREQ(ccw_ir_instr_opcode(
                            ir, ccw_ir_block_instr_ref(ir, block, 0)),
                        "lshr");
        ccw_node mask = ccw_ir_block_instr_ref(ir, block, 1);
        int64_t mask_value = 0;
        CCW_CHECK_STREQ(ccw_ir_instr_opcode(ir, mask), "iand");
        CCW_CHECK_STREQ(ccw_ir_instr_dest(ir, mask), "field");
        CCW_CHECK(ccw_ir_const_int_value(
                      ir, ccw_ir_instr_operand(ir, mask, 1), &mask_value) == CCW_OK
                      && mask_value == 15,
                  "four-bit extraction must use mask 15");
    }
    ccw_ir_module_destroy(ir);
}

static void test_exceptions(ccw_executor *executor)
{
    ccw_node block = 0;
    ccw_ir *ir = single_block_module("exceptions", CCW_PROFILE_TILLY, &block);
    ccw_node instruction = ccw_ir_instr_build(ir, "throw", CCW_TY_VOID);
    ccw_ir_instr_add_operand(ir, instruction, ccw_ir_operand_reg(ir, "error"));
    ccw_ir_block_append_instr(ir, block, instruction);

    if (apply_kernel(executor, "exception-lower.scm",
                     "lower.exceptions", ir))
        CCW_CHECK_STREQ(ccw_ir_instr_opcode(
                            ir, ccw_ir_block_instr_ref(ir, block, 0)),
                        "exception.raise");
    ccw_ir_module_destroy(ir);
}

static void test_tree_match(ccw_executor *executor)
{
    ccw_node block = 0;
    ccw_ir *ir = single_block_module("tree-match", CCW_PROFILE_TILLY, &block);
    ccw_node add = ccw_ir_instr_build(ir, "iadd", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, add, "sum");
    ccw_ir_instr_add_operand(ir, add, ccw_ir_operand_reg(ir, "x"));
    ccw_ir_instr_add_operand(ir, add, ccw_ir_operand_reg(ir, "y"));
    ccw_ir_block_append_instr(ir, block, add);

    if (apply_kernel(executor, "isel-tree-match.scm",
                     "codegen.isel-tree-match", ir))
        CCW_CHECK_STREQ(ccw_ir_attr_lookup(
                            ir, add,
                            "analysis.codegen.isel-tree-match.pattern-class"),
                        "alu-binary");
    ccw_ir_module_destroy(ir);
}

static void test_overflow(ccw_executor *executor)
{
    ccw_node block = 0;
    ccw_ir *ir = single_block_module("overflow", CCW_PROFILE_TILLY, &block);
    ccw_node add = ccw_ir_instr_build(ir, "iadd.checked", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, add, "sum");
    ccw_ir_instr_add_operand(ir, add, ccw_ir_operand_reg(ir, "x"));
    ccw_ir_instr_add_operand(ir, add, ccw_ir_operand_reg(ir, "y"));
    ccw_ir_block_append_instr(ir, block, add);

    if (apply_kernel(executor, "overflow-lower.scm", "lower.overflow", ir)) {
        ccw_node lowered = ccw_ir_block_instr_ref(ir, block, 0);
        CCW_CHECK_STREQ(ccw_ir_instr_opcode(ir, lowered),
                        "iadd.with-overflow");
        CCW_CHECK_STREQ(ccw_ir_instr_dest(ir, lowered), "sum");
    }
    ccw_ir_module_destroy(ir);
}

static ccw_ir *two_definition_module(ccw_node *first_out,
                                     ccw_node *second_out)
{
    ccw_node block = 0;
    ccw_ir *ir =
        single_block_module("definitions", CCW_PROFILE_TILLY, &block);
    ccw_node first = ccw_ir_instr_build(ir, "iadd", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, first, "a");
    ccw_ir_instr_add_operand(ir, first, ccw_ir_operand_reg(ir, "x"));
    ccw_ir_instr_add_operand(
        ir, first, ccw_ir_operand_const_int(ir, CCW_TY_I64, 1));
    ccw_ir_block_append_instr(ir, block, first);
    ccw_node second = ccw_ir_instr_build(ir, "imul", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, second, "b");
    ccw_ir_instr_add_operand(ir, second, ccw_ir_operand_reg(ir, "a"));
    ccw_ir_instr_add_operand(
        ir, second, ccw_ir_operand_const_int(ir, CCW_TY_I64, 2));
    ccw_ir_block_append_instr(ir, block, second);
    *first_out = first;
    *second_out = second;
    return ir;
}

static void test_graph_color(ccw_executor *executor)
{
    ccw_node first = 0, second = 0;
    ccw_ir *ir = two_definition_module(&first, &second);
    if (apply_kernel(executor, "regalloc-graph-color.scm",
                     "codegen.regalloc-graph", ir)) {
        CCW_CHECK_STREQ(ccw_ir_attr_lookup(
                            ir, first,
                            "analysis.codegen.regalloc-graph.virtual-color"),
                        "0");
        CCW_CHECK_STREQ(ccw_ir_attr_lookup(
                            ir, second,
                            "analysis.codegen.regalloc-graph.virtual-color"),
                        "1");
    }
    ccw_ir_module_destroy(ir);
}

static void test_linear_scan(ccw_executor *executor)
{
    ccw_node first = 0, second = 0;
    ccw_ir *ir = two_definition_module(&first, &second);
    if (apply_kernel(executor, "regalloc-linear-scan.scm",
                     "codegen.regalloc-linear", ir)) {
        CCW_CHECK_STREQ(ccw_ir_attr_lookup(
                            ir, first,
                            "analysis.codegen.regalloc-linear.linear-position"),
                        "0");
        CCW_CHECK_STREQ(ccw_ir_attr_lookup(
                            ir, second,
                            "analysis.codegen.regalloc-linear.virtual-register"),
                        "1");
    }
    ccw_ir_module_destroy(ir);
}

static void test_critical_path(ccw_executor *executor)
{
    ccw_node first = 0, second = 0;
    ccw_ir *ir = two_definition_module(&first, &second);
    if (apply_kernel(executor, "sched-critical-path.scm",
                     "codegen.sched-critical-path", ir)) {
        CCW_CHECK_STREQ(ccw_ir_attr_lookup(
                            ir, first,
                            "analysis.codegen.sched-critical-path.priority"),
                        "2");
        CCW_CHECK_STREQ(ccw_ir_attr_lookup(
                            ir, second,
                            "analysis.codegen.sched-critical-path.priority"),
                        "1");
    }
    ccw_ir_module_destroy(ir);
}

static void test_spill_slots(ccw_executor *executor)
{
    ccw_node block = 0;
    ccw_ir *ir = single_block_module("spill-slots", CCW_PROFILE_TILLY, &block);
    ccw_node first = ccw_ir_instr_build(ir, "spill-slot", CCW_TY_PTR);
    ccw_ir_instr_add_operand(
        ir, first, ccw_ir_operand_const_int(ir, CCW_TY_I64, 8));
    ccw_ir_block_append_instr(ir, block, first);
    ccw_node second = ccw_ir_instr_build(ir, "spill-slot", CCW_TY_PTR);
    ccw_ir_instr_add_operand(
        ir, second, ccw_ir_operand_const_int(ir, CCW_TY_I64, 4));
    ccw_ir_block_append_instr(ir, block, second);

    if (apply_kernel(executor, "spill-slot-pack.scm",
                     "codegen.spill-slot-pack", ir)) {
        CCW_CHECK_STREQ(ccw_ir_attr_lookup(
                            ir, first,
                            "analysis.codegen.spill-slot-pack.offset"),
                        "0");
        CCW_CHECK_STREQ(ccw_ir_attr_lookup(
                            ir, second,
                            "analysis.codegen.spill-slot-pack.offset"),
                        "8");
        CCW_CHECK_STREQ(ccw_ir_attr_lookup(
                            ir, second,
                            "analysis.codegen.spill-slot-pack.size"),
                        "4");
    }
    ccw_ir_module_destroy(ir);
}

static void test_ssa_construct(ccw_executor *executor)
{
    ccw_node block = 0;
    ccw_ir *ir = single_block_module("ssa", CCW_PROFILE_TILLY, &block);
    ccw_node first = ccw_ir_instr_build(ir, "imov", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, first, "x");
    ccw_ir_instr_add_operand(ir, first, ccw_ir_operand_reg(ir, "input"));
    ccw_ir_block_append_instr(ir, block, first);
    ccw_node second = ccw_ir_instr_build(ir, "iadd", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, second, "x");
    ccw_ir_instr_add_operand(ir, second, ccw_ir_operand_reg(ir, "x"));
    ccw_ir_instr_add_operand(
        ir, second, ccw_ir_operand_const_int(ir, CCW_TY_I64, 1));
    ccw_ir_block_append_instr(ir, block, second);
    ccw_node use = ccw_ir_instr_build(ir, "imul", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, use, "result");
    ccw_ir_instr_add_operand(ir, use, ccw_ir_operand_reg(ir, "x"));
    ccw_ir_instr_add_operand(
        ir, use, ccw_ir_operand_const_int(ir, CCW_TY_I64, 2));
    ccw_ir_block_append_instr(ir, block, use);

    if (apply_kernel(executor, "ssa-construct.scm",
                     "transform.ssa-construct", ir)) {
        CCW_CHECK_STREQ(ccw_ir_instr_dest(ir, second), "x.ssa1");
        CCW_CHECK_STREQ(ccw_ir_operand_name(
                            ir, ccw_ir_instr_operand(ir, second, 0)),
                        "x");
        CCW_CHECK_STREQ(ccw_ir_operand_name(
                            ir, ccw_ir_instr_operand(ir, use, 0)),
                        "x.ssa1");
    }
    ccw_ir_module_destroy(ir);
}

static void test_varargs(ccw_executor *executor)
{
    ccw_node block = 0;
    ccw_ir *ir = single_block_module("varargs", CCW_PROFILE_TILLY, &block);
    ccw_node argument = ccw_ir_instr_build(ir, "va-arg", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, argument, "next");
    ccw_ir_instr_add_operand(ir, argument, ccw_ir_operand_reg(ir, "cursor"));
    ccw_ir_block_append_instr(ir, block, argument);

    if (apply_kernel(executor, "varargs-lower.scm", "lower.varargs", ir)) {
        ccw_node lowered = ccw_ir_block_instr_ref(ir, block, 0);
        CCW_CHECK_STREQ(ccw_ir_instr_opcode(ir, lowered), "varargs.arg");
        CCW_CHECK_STREQ(ccw_ir_instr_dest(ir, lowered), "next");
    }
    ccw_ir_module_destroy(ir);
}

int main(void)
{
    ccw_executor *executor = ccw_executor_create();
    CCW_CHECK(executor != NULL, "executor creation failed");
    if (executor == NULL)
        return ccw_test_report("functional-kernels-more");
    CCW_CHECK(ccw_host_register_core_accessors(executor) == CCW_OK,
              "accessor registration failed");

    test_atomics(executor);
    test_bitfield(executor);
    test_exceptions(executor);
    test_tree_match(executor);
    test_overflow(executor);
    test_graph_color(executor);
    test_linear_scan(executor);
    test_critical_path(executor);
    test_spill_slots(executor);
    test_ssa_construct(executor);
    test_varargs(executor);

    ccw_executor_destroy(executor);
    return ccw_test_report("functional-kernels-more");
}
