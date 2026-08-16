/* Functional coverage for every kernel that remained a 0.0.0 placeholder. */

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
    ccw_status status =
        ccw_kernel_apply(executor, kernel, capability, ir, NULL, &error);
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

static ccw_ir *module_with_block(const char *name, ccw_profile profile,
                                 ccw_node *function_out, ccw_node *block_out)
{
    ccw_ir *ir = ccw_ir_module_create(name, profile);
    ccw_node function = ccw_ir_function_add(ir, "f", CCW_TY_I64);
    ccw_node block = ccw_ir_block_add(ir, function, "entry");
    if (function_out != NULL) *function_out = function;
    *block_out = block;
    return ir;
}

static void test_opcode_mapping(ccw_executor *executor,
                                const char *file, const char *capability,
                                const char *input, const char *expected)
{
    ccw_node block = 0;
    ccw_ir *ir = module_with_block(file, CCW_PROFILE_TILLY, NULL, &block);
    ccw_node instruction = ccw_ir_instr_build(ir, input, CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, instruction, "result");
    ccw_ir_instr_add_operand(ir, instruction, ccw_ir_operand_reg(ir, "x"));
    ccw_ir_instr_add_operand(ir, instruction, ccw_ir_operand_reg(ir, "y"));
    ccw_ir_block_append_instr(ir, block, instruction);
    if (apply_kernel(executor, file, capability, ir)) {
        ccw_node lowered = ccw_ir_block_instr_ref(ir, block, 0);
        CCW_CHECK_STREQ(ccw_ir_instr_opcode(ir, lowered), expected);
        CCW_CHECK_STREQ(ccw_ir_instr_dest(ir, lowered), "result");
    }
    ccw_ir_module_destroy(ir);
}

static void test_mappings(ccw_executor *executor)
{
    test_opcode_mapping(executor, "closure-convert.scm",
                        "lower.closure-conversion", "closure.make",
                        "runtime.closure.make");
    test_opcode_mapping(executor, "complex-lower.scm", "lower.complex",
                        "complex.add", "complex-libcall.add");
    test_opcode_mapping(executor, "codegen-aarch64.scm", "codegen.aarch64",
                        "iadd", "aarch64.add");
    test_opcode_mapping(executor, "codegen-riscv64.scm", "codegen.riscv64",
                        "iadd", "rv64.add");
    test_opcode_mapping(executor, "codegen-wasm32.scm", "codegen.wasm32",
                        "iadd", "wasm32.add");
    test_opcode_mapping(executor, "codegen-x86-64.scm", "codegen.x86-64",
                        "iadd", "x86-64.add");
    test_opcode_mapping(executor, "riscv64-codegen.scm", "codegen.riscv64",
                        "iadd", "rv64gc.add");
}

static void test_cps(ccw_executor *executor)
{
    ccw_node block = 0;
    ccw_ir *ir = module_with_block("cps", CCW_PROFILE_TILLY, NULL, &block);
    ccw_node call = ccw_ir_instr_build(ir, "call", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, call, "result");
    ccw_ir_instr_add_operand(ir, call, ccw_ir_operand_func(ir, "callee"));
    ccw_ir_block_append_instr(ir, block, call);
    ccw_node ret = ccw_ir_instr_build(ir, "ret", CCW_TY_VOID);
    ccw_ir_instr_add_operand(ir, ret, ccw_ir_operand_reg(ir, "result"));
    ccw_ir_block_append_instr(ir, block, ret);
    if (apply_kernel(executor, "cps-convert.scm",
                     "lower.cps-conversion", ir)) {
        CCW_CHECK(ccw_ir_block_instr_count(ir, block) == 1,
                  "CPS conversion must remove the direct return");
        CCW_CHECK_STREQ(ccw_ir_instr_opcode(
                            ir, ccw_ir_block_instr_ref(ir, block, 0)),
                        "cps.tail-call");
    }
    ccw_ir_module_destroy(ir);
}

static void test_inline_cache(ccw_executor *executor)
{
    ccw_node block = 0;
    ccw_ir *ir = module_with_block("inline-cache", CCW_PROFILE_ON1X,
                                   NULL, &block);
    ccw_node call = ccw_ir_instr_build(ir, "call.dynamic", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, call, "result");
    ccw_ir_instr_add_operand(ir, call, ccw_ir_operand_reg(ir, "receiver"));
    ccw_ir_block_append_instr(ir, block, call);
    if (apply_kernel(executor, "ic-install.scm", "vm.inline-cache", ir))
        CCW_CHECK_STREQ(ccw_ir_attr_lookup(
                            ir, call, "analysis.vm.inline-cache.slot"),
                        "0");
    ccw_ir_module_destroy(ir);
}

static void test_safepoint(ccw_executor *executor)
{
    ccw_node block = 0;
    ccw_ir *ir = module_with_block("safepoint", CCW_PROFILE_ON1X,
                                   NULL, &block);
    ccw_node call = ccw_ir_instr_build(ir, "call", CCW_TY_VOID);
    ccw_ir_instr_add_operand(ir, call, ccw_ir_operand_func(ir, "callee"));
    ccw_ir_block_append_instr(ir, block, call);
    if (apply_kernel(executor, "safepoint-insert.scm",
                     "vm.safepoint-insertion", ir)) {
        CCW_CHECK(ccw_ir_block_instr_count(ir, block) == 2,
                  "safepoint must be inserted before a call");
        CCW_CHECK_STREQ(ccw_ir_instr_opcode(
                            ir, ccw_ir_block_instr_ref(ir, block, 0)),
                        "safepoint");
        apply_kernel(executor, "safepoint-insert.scm",
                     "vm.safepoint-insertion", ir);
        CCW_CHECK(ccw_ir_block_instr_count(ir, block) == 2,
                  "safepoint insertion must be idempotent");
    }
    ccw_ir_module_destroy(ir);
}

static void test_switch(ccw_executor *executor)
{
    ccw_node function = 0, entry = 0;
    ccw_ir *ir = module_with_block("switch", CCW_PROFILE_TILLY,
                                   &function, &entry);
    ccw_ir_block_add(ir, function, "default");
    ccw_ir_block_add(ir, function, "hit");
    ccw_node instruction = ccw_ir_instr_build(ir, "switch", CCW_TY_VOID);
    ccw_ir_instr_add_operand(
        ir, instruction, ccw_ir_operand_const_int(ir, CCW_TY_I64, 7));
    ccw_ir_instr_add_operand(
        ir, instruction, ccw_ir_operand_block(ir, "default"));
    ccw_ir_instr_add_operand(
        ir, instruction, ccw_ir_operand_const_int(ir, CCW_TY_I64, 7));
    ccw_ir_instr_add_operand(
        ir, instruction, ccw_ir_operand_block(ir, "hit"));
    ccw_ir_block_append_instr(ir, entry, instruction);
    if (apply_kernel(executor, "switch-lower.scm", "lower.switch", ir)) {
        ccw_node branch = ccw_ir_block_instr_ref(ir, entry, 0);
        CCW_CHECK_STREQ(ccw_ir_instr_opcode(ir, branch), "br");
        CCW_CHECK_STREQ(ccw_ir_operand_name(
                            ir, ccw_ir_instr_operand(ir, branch, 0)),
                        "hit");
    }
    ccw_ir_module_destroy(ir);
}

static void test_code_sink(ccw_executor *executor)
{
    ccw_node block = 0;
    ccw_ir *ir = module_with_block("sink", CCW_PROFILE_TILLY, NULL, &block);
    ccw_node producer = ccw_ir_instr_build(ir, "iadd", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, producer, "temporary");
    ccw_ir_instr_add_operand(ir, producer, ccw_ir_operand_reg(ir, "x"));
    ccw_ir_instr_add_operand(
        ir, producer, ccw_ir_operand_const_int(ir, CCW_TY_I64, 1));
    ccw_ir_block_append_instr(ir, block, producer);
    ccw_node unrelated = ccw_ir_instr_build(ir, "imov", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, unrelated, "other");
    ccw_ir_instr_add_operand(ir, unrelated, ccw_ir_operand_reg(ir, "y"));
    ccw_ir_block_append_instr(ir, block, unrelated);
    ccw_node use = ccw_ir_instr_build(ir, "imul", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, use, "result");
    ccw_ir_instr_add_operand(ir, use, ccw_ir_operand_reg(ir, "temporary"));
    ccw_ir_instr_add_operand(
        ir, use, ccw_ir_operand_const_int(ir, CCW_TY_I64, 2));
    ccw_ir_block_append_instr(ir, block, use);
    if (apply_kernel(executor, "code-sink.scm", "opt.sink", ir))
        CCW_CHECK_STREQ(ccw_ir_attr_lookup(
                            ir, producer, "analysis.opt.sink.sink-before-index"),
                        "2");
    ccw_ir_module_destroy(ir);
}

static ccw_ir *linear_cfg(ccw_node *first_out)
{
    ccw_ir *ir = ccw_ir_module_create("linear-cfg", CCW_PROFILE_TILLY);
    ccw_node function = ccw_ir_function_add(ir, "f", CCW_TY_VOID);
    ccw_node first = ccw_ir_block_add(ir, function, "first");
    ccw_node middle = ccw_ir_block_add(ir, function, "middle");
    ccw_ir_block_add(ir, function, "last");
    ccw_node branch = ccw_ir_instr_build(ir, "br", CCW_TY_VOID);
    ccw_ir_instr_add_operand(ir, branch, ccw_ir_operand_block(ir, "middle"));
    ccw_ir_block_append_instr(ir, first, branch);
    branch = ccw_ir_instr_build(ir, "br", CCW_TY_VOID);
    ccw_ir_instr_add_operand(ir, branch, ccw_ir_operand_block(ir, "last"));
    ccw_ir_block_append_instr(ir, middle, branch);
    *first_out = first;
    return ir;
}

static void test_jump_advice(ccw_executor *executor)
{
    char expected[32];
    ccw_node first = 0;
    ccw_ir *ir = linear_cfg(&first);
    ccw_node target = ccw_ir_function_block_ref(
        ir, ccw_ir_function_ref(ir, 0), 2);
    snprintf(expected, sizeof(expected), "%llu",
             (unsigned long long)target);
    if (apply_kernel(executor, "jump-thread.scm",
                     "opt.jump-threading", ir)) {
        const char *expected_text = expected;
        CCW_CHECK_STREQ(ccw_ir_attr_lookup(
                            ir, first,
                            "analysis.opt.jump-threading.thread-target"),
                        expected_text);
    }
    ccw_ir_module_destroy(ir);

    ir = linear_cfg(&first);
    target = ccw_ir_function_block_ref(ir, ccw_ir_function_ref(ir, 0), 2);
    snprintf(expected, sizeof(expected), "%llu",
             (unsigned long long)target);
    if (apply_kernel(executor, "jump-threading.scm",
                     "opt.jump-threading", ir)) {
        const char *expected_text = expected;
        CCW_CHECK_STREQ(ccw_ir_attr_lookup(
                            ir, first,
                            "analysis.opt.jump-threading.exclusive-thread-target"),
                        expected_text);
    }
    ccw_ir_module_destroy(ir);
}

static void test_simple_facts(ccw_executor *executor)
{
    ccw_node function = 0, block = 0;
    ccw_ir *ir = module_with_block("facts", CCW_PROFILE_TILLY,
                                   &function, &block);
    ccw_node first = ccw_ir_instr_build(ir, "iadd", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, first, "a");
    ccw_ir_instr_add_operand(ir, first, ccw_ir_operand_reg(ir, "x"));
    ccw_ir_instr_add_operand(ir, first, ccw_ir_operand_reg(ir, "y"));
    ccw_ir_block_append_instr(ir, block, first);
    ccw_node second = ccw_ir_instr_build(ir, "iadd", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, second, "b");
    ccw_ir_instr_add_operand(ir, second, ccw_ir_operand_reg(ir, "u"));
    ccw_ir_instr_add_operand(ir, second, ccw_ir_operand_reg(ir, "v"));
    ccw_ir_block_append_instr(ir, block, second);

    if (apply_kernel(executor, "dwarf-linetable.scm",
                     "debug.dwarf-linetable", ir))
        CCW_CHECK_STREQ(ccw_ir_attr_lookup(
                            ir, second,
                            "analysis.debug.dwarf-linetable.line-ordinal"),
                        "2");
    if (apply_kernel(executor, "lambda-lift.scm",
                     "lower.lambda-lifting", ir))
        CCW_CHECK_STREQ(ccw_ir_attr_lookup(
                            ir, function,
                            "analysis.lower.lambda-lifting.top-level?"),
                        "true");
    if (apply_kernel(executor, "regalloc-graph.scm",
                     "codegen.regalloc-graph", ir))
        CCW_CHECK_STREQ(ccw_ir_attr_lookup(
                            ir, second,
                            "analysis.codegen.regalloc-graph.allocator-color"),
                        "1");
    if (apply_kernel(executor, "regalloc-linear.scm",
                     "codegen.regalloc-linear", ir))
        CCW_CHECK_STREQ(ccw_ir_attr_lookup(
                            ir, second,
                            "analysis.codegen.regalloc-linear.allocator-slot"),
                        "1");
    if (apply_kernel(executor, "sched-list.scm",
                     "codegen.sched-list", ir))
        CCW_CHECK_STREQ(ccw_ir_attr_lookup(
                            ir, second,
                            "analysis.codegen.sched-list.schedule-order"),
                        "1");
    if (apply_kernel(executor, "slp-vectorize.scm",
                     "opt.slp-vectorize", ir)) {
        CCW_CHECK_STREQ(ccw_ir_attr_lookup(
                            ir, first,
                            "analysis.opt.slp-vectorize.pack-id"),
                        "0");
        CCW_CHECK_STREQ(ccw_ir_attr_lookup(
                            ir, second,
                            "analysis.opt.slp-vectorize.pack-id"),
                        "0");
    }
    ccw_ir_module_destroy(ir);
}

static ccw_ir *self_loop_module(bool two, ccw_node *first_out)
{
    ccw_ir *ir = ccw_ir_module_create("loops", CCW_PROFILE_TILLY);
    ccw_node function = ccw_ir_function_add(ir, "f", CCW_TY_VOID);
    ccw_node first = ccw_ir_block_add(ir, function, "loop-a");
    ccw_node branch = ccw_ir_instr_build(ir, "br", CCW_TY_VOID);
    ccw_ir_instr_add_operand(ir, branch, ccw_ir_operand_block(ir, "loop-a"));
    ccw_ir_block_append_instr(ir, first, branch);
    if (two) {
        ccw_node second = ccw_ir_block_add(ir, function, "loop-b");
        branch = ccw_ir_instr_build(ir, "br", CCW_TY_VOID);
        ccw_ir_instr_add_operand(ir, branch, ccw_ir_operand_block(ir, "loop-b"));
        ccw_ir_block_append_instr(ir, second, branch);
    }
    *first_out = first;
    return ir;
}

static void test_loop_advice(ccw_executor *executor)
{
    ccw_node first = 0;
    ccw_ir *ir = self_loop_module(false, &first);
    if (apply_kernel(executor, "loop-unroll.scm", "opt.loop-unroll", ir))
        CCW_CHECK_STREQ(ccw_ir_attr_lookup(
                            ir, first, "analysis.opt.loop-unroll.factor"),
                        "2");
    ccw_ir_module_destroy(ir);

    ir = self_loop_module(true, &first);
    if (apply_kernel(executor, "loop-fusion.scm", "opt.loop-fusion", ir)) {
        ccw_node second = ccw_ir_function_block_ref(
            ir, ccw_ir_function_ref(ir, 0), 1);
        char expected[32];
        snprintf(expected, sizeof(expected), "%llu",
                 (unsigned long long)second);
        const char *expected_text = expected;
        CCW_CHECK_STREQ(ccw_ir_attr_lookup(
                            ir, first,
                            "analysis.opt.loop-fusion.candidate-next"),
                        expected_text);
    }
    ccw_ir_module_destroy(ir);
}

static void test_inline(ccw_executor *executor)
{
    ccw_ir *ir = ccw_ir_module_create("inline", CCW_PROFILE_TILLY);
    ccw_node caller = ccw_ir_function_add(ir, "caller", CCW_TY_I64);
    ccw_node caller_block = ccw_ir_block_add(ir, caller, "entry");
    ccw_node call = ccw_ir_instr_build(ir, "call", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, call, "answer");
    ccw_ir_instr_add_operand(ir, call, ccw_ir_operand_func(ir, "constant"));
    ccw_ir_block_append_instr(ir, caller_block, call);
    ccw_node callee = ccw_ir_function_add(ir, "constant", CCW_TY_I64);
    ccw_node callee_block = ccw_ir_block_add(ir, callee, "entry");
    ccw_node ret = ccw_ir_instr_build(ir, "ret", CCW_TY_VOID);
    ccw_ir_instr_add_operand(
        ir, ret, ccw_ir_operand_const_int(ir, CCW_TY_I64, 42));
    ccw_ir_block_append_instr(ir, callee_block, ret);
    if (apply_kernel(executor, "inline.scm", "opt.inline", ir)) {
        ccw_node replacement =
            ccw_ir_block_instr_ref(ir, caller_block, 0);
        int64_t value = 0;
        CCW_CHECK_STREQ(ccw_ir_instr_opcode(ir, replacement), "imov");
        CCW_CHECK(ccw_ir_const_int_value(
                      ir, ccw_ir_instr_operand(ir, replacement, 0), &value)
                      == CCW_OK && value == 42,
                  "constant-return call must inline value 42");
    }
    ccw_ir_module_destroy(ir);
}

static void test_mem2reg(ccw_executor *executor)
{
    ccw_node block = 0;
    ccw_ir *ir = module_with_block("mem2reg", CCW_PROFILE_TILLY,
                                   NULL, &block);
    ccw_node slot = ccw_ir_instr_build(ir, "stack-slot", CCW_TY_PTR);
    ccw_ir_instr_set_dest(ir, slot, "slot");
    ccw_ir_block_append_instr(ir, block, slot);
    ccw_node store = ccw_ir_instr_build(ir, "store", CCW_TY_VOID);
    ccw_ir_instr_add_operand(ir, store, ccw_ir_operand_reg(ir, "slot"));
    ccw_ir_instr_add_operand(ir, store, ccw_ir_operand_reg(ir, "value"));
    ccw_ir_block_append_instr(ir, block, store);
    ccw_node load = ccw_ir_instr_build(ir, "load", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, load, "result");
    ccw_ir_instr_add_operand(ir, load, ccw_ir_operand_reg(ir, "slot"));
    ccw_ir_block_append_instr(ir, block, load);
    if (apply_kernel(executor, "mem2reg.scm", "opt.mem2reg", ir)) {
        CCW_CHECK(ccw_ir_block_instr_count(ir, block) == 1,
                  "local stack-slot triple must collapse to one copy");
        ccw_node copy = ccw_ir_block_instr_ref(ir, block, 0);
        CCW_CHECK_STREQ(ccw_ir_instr_opcode(ir, copy), "imov");
        CCW_CHECK_STREQ(ccw_ir_operand_name(
                            ir, ccw_ir_instr_operand(ir, copy, 0)),
                        "value");
    }
    ccw_ir_module_destroy(ir);
}

int main(void)
{
    ccw_executor *executor = ccw_executor_create();
    CCW_CHECK(executor != NULL, "executor creation failed");
    if (executor == NULL)
        return ccw_test_report("functional-kernels-final");
    CCW_CHECK(ccw_host_register_core_accessors(executor) == CCW_OK,
              "accessor registration failed");

    test_mappings(executor);
    test_cps(executor);
    test_inline_cache(executor);
    test_safepoint(executor);
    test_switch(executor);
    test_code_sink(executor);
    test_jump_advice(executor);
    test_simple_facts(executor);
    test_loop_advice(executor);
    test_inline(executor);
    test_mem2reg(executor);

    ccw_executor_destroy(executor);
    return ccw_test_report("functional-kernels-final");
}
