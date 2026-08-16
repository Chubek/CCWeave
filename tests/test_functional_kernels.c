/* Conservative functional coverage for kernels promoted from metadata-only
 * placeholders. Each check exercises observable IR mutation or host facts. */

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
    if (status == CCW_OK) {
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
    }
    return status == CCW_OK;
}

static void test_anf(ccw_executor *executor)
{
    ccw_ir *ir = ccw_ir_module_create("anf", CCW_PROFILE_TILLY);
    ccw_node fn = ccw_ir_function_add(ir, "f", CCW_TY_I64);
    ccw_node block = ccw_ir_block_add(ir, fn, "entry");
    ccw_node add = ccw_ir_instr_build(ir, "iadd", CCW_TY_I64);
    ccw_ir_instr_add_operand(ir, add, ccw_ir_operand_reg(ir, "x"));
    ccw_ir_instr_add_operand(
        ir, add, ccw_ir_operand_const_int(ir, CCW_TY_I64, 1));
    ccw_ir_block_append_instr(ir, block, add);

    if (apply_kernel(executor, "anf-normalize.scm", "normalize.anf", ir))
        CCW_CHECK_STREQ(ccw_ir_instr_dest(ir, add), "anf.0");
    ccw_ir_module_destroy(ir);
}

static void test_deopt_metadata(ccw_executor *executor)
{
    ccw_ir *ir = ccw_ir_module_create("deopt-metadata", CCW_PROFILE_ON1X);
    ccw_node fn = ccw_ir_function_add(ir, "f", CCW_TY_VOID);
    ccw_node entry = ccw_ir_block_add(ir, fn, "entry");
    ccw_ir_block_add(ir, fn, "exit");
    ccw_node deopt = ccw_ir_instr_build(ir, "deopt", CCW_TY_VOID);
    ccw_ir_instr_add_operand(ir, deopt, ccw_ir_operand_block(ir, "exit"));
    ccw_ir_block_append_instr(ir, entry, deopt);

    if (apply_kernel(executor, "deopt-metadata.scm",
                     "vm.deopt-metadata", ir)) {
        CCW_CHECK_STREQ(ccw_ir_attr_lookup(
                            ir, deopt,
                            "analysis.vm.deopt-metadata.target"),
                        "exit");
        CCW_CHECK_STREQ(ccw_ir_attr_lookup(
                            ir, deopt,
                            "analysis.vm.deopt-metadata.state-operand-count"),
                        "1");
    }
    ccw_ir_module_destroy(ir);
}

static void test_gc_barrier(ccw_executor *executor)
{
    ccw_ir *ir = ccw_ir_module_create("gc-barrier", CCW_PROFILE_ON1X);
    ccw_node fn = ccw_ir_function_add(ir, "f", CCW_TY_VOID);
    ccw_node block = ccw_ir_block_add(ir, fn, "entry");
    ccw_node store = ccw_ir_instr_build(ir, "store", CCW_TY_VOID);
    ccw_ir_instr_add_operand(ir, store, ccw_ir_operand_reg(ir, "address"));
    ccw_ir_instr_add_operand(ir, store, ccw_ir_operand_reg(ir, "value"));
    ccw_ir_block_append_instr(ir, block, store);

    if (apply_kernel(executor, "gc-barrier-insert.scm",
                     "vm.gc-barrier-insertion", ir)) {
        CCW_CHECK(ccw_ir_block_instr_count(ir, block) == 2,
                  "gc barrier must be inserted before a store");
        CCW_CHECK_STREQ(ccw_ir_instr_opcode(
                            ir, ccw_ir_block_instr_ref(ir, block, 0)),
                        "gc-write-barrier");
        apply_kernel(executor, "gc-barrier-insert.scm",
                     "vm.gc-barrier-insertion", ir);
        CCW_CHECK(ccw_ir_block_instr_count(ir, block) == 2,
                  "gc barrier insertion must be idempotent");
    }
    ccw_ir_module_destroy(ir);
}

static void test_isel_legalize(ccw_executor *executor)
{
    ccw_ir *ir = ccw_ir_module_create("isel", CCW_PROFILE_TILLY);
    ccw_node fn = ccw_ir_function_add(ir, "f", CCW_TY_I64);
    ccw_node block = ccw_ir_block_add(ir, fn, "entry");
    ccw_node neg = ccw_ir_instr_build(ir, "ineg", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, neg, "negative");
    ccw_ir_instr_add_operand(ir, neg, ccw_ir_operand_reg(ir, "x"));
    ccw_ir_block_append_instr(ir, block, neg);

    if (apply_kernel(executor, "isel-legalize.scm",
                     "codegen.isel-legalize", ir)) {
        ccw_node lowered = ccw_ir_block_instr_ref(ir, block, 0);
        int64_t zero = -1;
        CCW_CHECK_STREQ(ccw_ir_instr_opcode(ir, lowered), "isub");
        CCW_CHECK(ccw_ir_const_int_value(
                      ir, ccw_ir_instr_operand(ir, lowered, 0), &zero) == CCW_OK
                      && zero == 0,
                  "ineg must legalize to subtraction from zero");
        CCW_CHECK_STREQ(ccw_ir_instr_dest(ir, lowered), "negative");
    }
    ccw_ir_module_destroy(ir);
}

static void test_pattern_lower(ccw_executor *executor)
{
    ccw_ir *ir = ccw_ir_module_create("pattern", CCW_PROFILE_TILLY);
    ccw_node fn = ccw_ir_function_add(ir, "f", CCW_TY_I1);
    ccw_node block = ccw_ir_block_add(ir, fn, "entry");
    ccw_node match = ccw_ir_instr_build(ir, "match-eq", CCW_TY_I1);
    ccw_ir_instr_set_dest(ir, match, "matched");
    ccw_ir_instr_add_operand(ir, match, ccw_ir_operand_reg(ir, "x"));
    ccw_ir_instr_add_operand(
        ir, match, ccw_ir_operand_const_int(ir, CCW_TY_I64, 7));
    ccw_ir_block_append_instr(ir, block, match);

    if (apply_kernel(executor, "pattern-match-lower.scm",
                     "lower.pattern-match", ir)) {
        ccw_node lowered = ccw_ir_block_instr_ref(ir, block, 0);
        CCW_CHECK_STREQ(ccw_ir_instr_opcode(ir, lowered), "icmp.eq");
        CCW_CHECK_STREQ(ccw_ir_instr_dest(ir, lowered), "matched");
    }
    ccw_ir_module_destroy(ir);
}

static void test_peephole(ccw_executor *executor)
{
    ccw_ir *ir = ccw_ir_module_create("peephole", CCW_PROFILE_TILLY);
    ccw_node fn = ccw_ir_function_add(ir, "f", CCW_TY_I64);
    ccw_node block = ccw_ir_block_add(ir, fn, "entry");
    ccw_node add = ccw_ir_instr_build(ir, "iadd", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, add, "sum");
    ccw_ir_instr_add_operand(ir, add, ccw_ir_operand_reg(ir, "x"));
    ccw_ir_instr_add_operand(
        ir, add, ccw_ir_operand_const_int(ir, CCW_TY_I64, 0));
    ccw_ir_block_append_instr(ir, block, add);
    ccw_node self = ccw_ir_instr_build(ir, "imov", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, self, "same");
    ccw_ir_instr_add_operand(ir, self, ccw_ir_operand_reg(ir, "same"));
    ccw_ir_block_append_instr(ir, block, self);

    if (apply_kernel(executor, "peephole.scm", "codegen.peephole", ir)) {
        CCW_CHECK(ccw_ir_block_instr_count(ir, block) == 1,
                  "peephole must remove a self-copy");
        CCW_CHECK_STREQ(ccw_ir_instr_opcode(
                            ir, ccw_ir_block_instr_ref(ir, block, 0)),
                        "imov");
    }
    ccw_ir_module_destroy(ir);
}

static void test_regalloc_coalesce(ccw_executor *executor)
{
    ccw_ir *ir = ccw_ir_module_create("coalesce", CCW_PROFILE_TILLY);
    ccw_node fn = ccw_ir_function_add(ir, "f", CCW_TY_I64);
    ccw_node block = ccw_ir_block_add(ir, fn, "entry");
    ccw_node copy = ccw_ir_instr_build(ir, "imov", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, copy, "temporary");
    ccw_ir_instr_add_operand(ir, copy, ccw_ir_operand_reg(ir, "x"));
    ccw_ir_block_append_instr(ir, block, copy);
    ccw_node add = ccw_ir_instr_build(ir, "iadd", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, add, "result");
    ccw_ir_instr_add_operand(ir, add, ccw_ir_operand_reg(ir, "temporary"));
    ccw_ir_instr_add_operand(
        ir, add, ccw_ir_operand_const_int(ir, CCW_TY_I64, 1));
    ccw_ir_block_append_instr(ir, block, add);

    if (apply_kernel(executor, "regalloc-coalesce.scm",
                     "codegen.regalloc-coalesce", ir)) {
        CCW_CHECK(ccw_ir_block_instr_count(ir, block) == 1,
                  "coalescing must remove the local copy");
        ccw_node result = ccw_ir_block_instr_ref(ir, block, 0);
        CCW_CHECK_STREQ(ccw_ir_operand_name(
                            ir, ccw_ir_instr_operand(ir, result, 0)),
                        "x");
    }
    ccw_ir_module_destroy(ir);
}

static void test_ssa_destruct(ccw_executor *executor)
{
    ccw_ir *ir = ccw_ir_module_create("ssa-destruct", CCW_PROFILE_TILLY);
    ccw_node fn = ccw_ir_function_add(ir, "f", CCW_TY_I64);
    ccw_node block = ccw_ir_block_add(ir, fn, "entry");
    ccw_node phi = ccw_ir_instr_build(ir, "phi", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, phi, "joined");
    ccw_ir_instr_add_operand(ir, phi, ccw_ir_operand_reg(ir, "x"));
    ccw_ir_block_append_instr(ir, block, phi);

    if (apply_kernel(executor, "ssa-destruct.scm",
                     "transform.ssa-destruct", ir)) {
        ccw_node copy = ccw_ir_block_instr_ref(ir, block, 0);
        CCW_CHECK_STREQ(ccw_ir_instr_opcode(ir, copy), "imov");
        CCW_CHECK_STREQ(ccw_ir_instr_dest(ir, copy), "joined");
    }
    ccw_ir_module_destroy(ir);
}

static void test_tailcall_mark(ccw_executor *executor)
{
    ccw_ir *ir = ccw_ir_module_create("tailcall", CCW_PROFILE_TILLY);
    ccw_node fn = ccw_ir_function_add(ir, "f", CCW_TY_I64);
    ccw_node block = ccw_ir_block_add(ir, fn, "entry");
    ccw_node call = ccw_ir_instr_build(ir, "call", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, call, "result");
    ccw_ir_instr_add_operand(ir, call, ccw_ir_operand_func(ir, "callee"));
    ccw_ir_block_append_instr(ir, block, call);
    ccw_node ret = ccw_ir_instr_build(ir, "ret", CCW_TY_VOID);
    ccw_ir_instr_add_operand(ir, ret, ccw_ir_operand_reg(ir, "result"));
    ccw_ir_block_append_instr(ir, block, ret);

    if (apply_kernel(executor, "tailcall-mark.scm", "opt.tailcall", ir))
        CCW_CHECK_STREQ(ccw_ir_attr_lookup(
                            ir, call,
                            "analysis.opt.tailcall.tail-position?"),
                        "true");
    ccw_ir_module_destroy(ir);
}

static void test_ubsan(ccw_executor *executor)
{
    ccw_ir *ir = ccw_ir_module_create("ubsan", CCW_PROFILE_TILLY);
    ccw_node fn = ccw_ir_function_add(ir, "f", CCW_TY_I64);
    ccw_node block = ccw_ir_block_add(ir, fn, "entry");
    ccw_node unsafe = ccw_ir_instr_build(ir, "idiv", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, unsafe, "dynamic");
    ccw_ir_instr_add_operand(ir, unsafe, ccw_ir_operand_reg(ir, "x"));
    ccw_ir_instr_add_operand(ir, unsafe, ccw_ir_operand_reg(ir, "divisor"));
    ccw_ir_block_append_instr(ir, block, unsafe);
    ccw_node safe = ccw_ir_instr_build(ir, "idiv", CCW_TY_I64);
    ccw_ir_instr_set_dest(ir, safe, "constant");
    ccw_ir_instr_add_operand(ir, safe, ccw_ir_operand_reg(ir, "x"));
    ccw_ir_instr_add_operand(
        ir, safe, ccw_ir_operand_const_int(ir, CCW_TY_I64, 2));
    ccw_ir_block_append_instr(ir, block, safe);

    if (apply_kernel(executor, "ubsan-checks.scm", "sanitize.ubsan", ir)) {
        CCW_CHECK(ccw_ir_block_instr_count(ir, block) == 3,
                  "ubsan must instrument only the dynamic divisor");
        CCW_CHECK_STREQ(ccw_ir_instr_opcode(
                            ir, ccw_ir_block_instr_ref(ir, block, 0)),
                        "ubsan-check-divisor");
        apply_kernel(executor, "ubsan-checks.scm", "sanitize.ubsan", ir);
        CCW_CHECK(ccw_ir_block_instr_count(ir, block) == 3,
                  "ubsan instrumentation must be idempotent");
    }
    ccw_ir_module_destroy(ir);
}

int main(void)
{
    ccw_executor *executor = ccw_executor_create();
    CCW_CHECK(executor != NULL, "executor creation failed");
    if (executor == NULL) return ccw_test_report("functional-kernels");
    CCW_CHECK(ccw_host_register_core_accessors(executor) == CCW_OK,
              "accessor registration failed");

    test_anf(executor);
    test_deopt_metadata(executor);
    test_gc_barrier(executor);
    test_isel_legalize(executor);
    test_pattern_lower(executor);
    test_peephole(executor);
    test_regalloc_coalesce(executor);
    test_ssa_destruct(executor);
    test_tailcall_mark(executor);
    test_ubsan(executor);

    ccw_executor_destroy(executor);
    return ccw_test_report("functional-kernels");
}
