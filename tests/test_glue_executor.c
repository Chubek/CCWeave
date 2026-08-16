/* Glue/executor conformance: ABI check, missing-export rejection,
 * capability verification before dispatch, accessor arity violations,
 * accessor errors as Scheme conditions, and string ownership. */

#include "GlueSTD.h"
#include "ccw_host_accessors.h"
#include "ccw_ir.h"
#include "ccw_test.h"

#include <stdlib.h>

#ifndef CCW_KERNEL_DIR
#define CCW_KERNEL_DIR "kernels"
#endif
#ifndef CCW_FIXTURE_DIR
#define CCW_FIXTURE_DIR "tests/fixtures"
#endif

static char *path_in(const char *dir, const char *file)
{
    size_t n = strlen(dir) + strlen(file) + 2u;
    char *p = (char *)malloc(n);
    snprintf(p, n, "%s/%s", dir, file);
    return p;
}

static char *copy_string(const char *s)
{
    if (s == NULL) return NULL;
    size_t n = strlen(s) + 1u;
    char *p = (char *)malloc(n);
    if (p != NULL) memcpy(p, s, n);
    return p;
}

/* Host edit interposition: reject every replacement (§2.3). */
static int g_edits_seen = 0;

static bool reject_all_edits(void *user_data, ccw_ir *ir, ccw_edit_kind kind,
                             ccw_node target, ccw_node incoming)
{
    (void)user_data; (void)ir; (void)kind; (void)target; (void)incoming;
    g_edits_seen++;
    return false;
}

/* imul %x, 8 in a one-block function; strength-reduce must rewrite it. */
static ccw_ir *sample_module(void)
{
    ccw_ir *m = ccw_ir_module_create("glue-sample", CCW_PROFILE_TILLY);
    ccw_node fn = ccw_ir_function_add(m, "f", CCW_TY_I64);
    ccw_ir_function_add_param(m, fn, CCW_TY_I64, "x");
    ccw_node blk = ccw_ir_block_add(m, fn, "entry");

    ccw_node mul = ccw_ir_instr_build(m, "imul", CCW_TY_I64);
    ccw_ir_instr_set_dest(m, mul, "t0");
    ccw_ir_instr_add_operand(m, mul, ccw_ir_operand_reg(m, "x"));
    ccw_ir_instr_add_operand(m, mul, ccw_ir_operand_const_int(m, CCW_TY_I64, 8));
    ccw_ir_block_append_instr(m, blk, mul);

    /* Not a power of two: must be left alone. */
    ccw_node mul7 = ccw_ir_instr_build(m, "imul", CCW_TY_I64);
    ccw_ir_instr_set_dest(m, mul7, "t1");
    ccw_ir_instr_add_operand(m, mul7, ccw_ir_operand_reg(m, "x"));
    ccw_ir_instr_add_operand(m, mul7, ccw_ir_operand_const_int(m, CCW_TY_I64, 7));
    ccw_ir_block_append_instr(m, blk, mul7);

    /* Constant times constant: must be left alone. */
    ccw_node mulcc = ccw_ir_instr_build(m, "imul", CCW_TY_I64);
    ccw_ir_instr_set_dest(m, mulcc, "t2");
    ccw_ir_instr_add_operand(m, mulcc, ccw_ir_operand_const_int(m, CCW_TY_I64, 4));
    ccw_ir_instr_add_operand(m, mulcc, ccw_ir_operand_const_int(m, CCW_TY_I64, 8));
    ccw_ir_block_append_instr(m, blk, mulcc);
    return m;
}

/* Constant folding must preserve zero and refuse results outside ccw_val's
 * signed 64-bit integer boundary. */
static ccw_ir *constant_module(void)
{
    ccw_ir *m = ccw_ir_module_create("constant-sample", CCW_PROFILE_TILLY);
    ccw_node fn = ccw_ir_function_add(m, "f", CCW_TY_I64);
    ccw_node blk = ccw_ir_block_add(m, fn, "entry");

    ccw_node zero = ccw_ir_instr_build(m, "isub", CCW_TY_I64);
    ccw_ir_instr_set_dest(m, zero, "zero");
    ccw_ir_instr_add_operand(m, zero, ccw_ir_operand_const_int(m, CCW_TY_I64, 7));
    ccw_ir_instr_add_operand(m, zero, ccw_ir_operand_const_int(m, CCW_TY_I64, 7));
    ccw_ir_block_append_instr(m, blk, zero);

    ccw_node overflow = ccw_ir_instr_build(m, "iadd", CCW_TY_I64);
    ccw_ir_instr_set_dest(m, overflow, "overflow");
    ccw_ir_instr_add_operand(
        m, overflow, ccw_ir_operand_const_int(m, CCW_TY_I64, INT64_MAX));
    ccw_ir_instr_add_operand(m, overflow, ccw_ir_operand_const_int(m, CCW_TY_I64, 1));
    ccw_ir_block_append_instr(m, blk, overflow);

    ccw_node unrelated = ccw_ir_instr_build(m, "icmp.eq", CCW_TY_I1);
    ccw_ir_instr_set_dest(m, unrelated, "same");
    ccw_ir_instr_add_operand(m, unrelated, ccw_ir_operand_const_int(m, CCW_TY_I64, 1));
    ccw_ir_instr_add_operand(m, unrelated, ccw_ir_operand_const_int(m, CCW_TY_I64, 1));
    ccw_ir_block_append_instr(m, blk, unrelated);
    return m;
}

static ccw_ir *copy_module(void)
{
    ccw_ir *m = ccw_ir_module_create("copy-sample", CCW_PROFILE_TILLY);
    ccw_node fn = ccw_ir_function_add(m, "f", CCW_TY_I64);
    ccw_node blk = ccw_ir_block_add(m, fn, "entry");

    ccw_node copy = ccw_ir_instr_build(m, "imov", CCW_TY_I64);
    ccw_ir_instr_set_dest(m, copy, "tmp");
    ccw_ir_instr_add_operand(m, copy, ccw_ir_operand_reg(m, "x"));
    ccw_ir_block_append_instr(m, blk, copy);

    ccw_node add = ccw_ir_instr_build(m, "iadd", CCW_TY_I64);
    ccw_ir_instr_set_dest(m, add, "out");
    ccw_ir_instr_add_operand(m, add, ccw_ir_operand_reg(m, "tmp"));
    ccw_ir_instr_add_operand(m, add, ccw_ir_operand_const_int(m, CCW_TY_I64, 1));
    ccw_ir_block_append_instr(m, blk, add);
    return m;
}

static ccw_ir *unreachable_module(void)
{
    ccw_ir *m = ccw_ir_module_create("unreachable-sample", CCW_PROFILE_TILLY);
    ccw_node fn = ccw_ir_function_add(m, "f", CCW_TY_VOID);
    ccw_node entry = ccw_ir_block_add(m, fn, "entry");
    ccw_node dead = ccw_ir_block_add(m, fn, "dead");
    ccw_node ret = ccw_ir_instr_build(m, "ret", CCW_TY_VOID);
    ccw_ir_block_append_instr(m, entry, ret);
    ret = ccw_ir_instr_build(m, "ret", CCW_TY_VOID);
    ccw_ir_block_append_instr(m, dead, ret);
    return m;
}

static ccw_ir *linear_module(void)
{
    ccw_ir *m = ccw_ir_module_create("linear-sample", CCW_PROFILE_TILLY);
    ccw_node fn = ccw_ir_function_add(m, "f", CCW_TY_VOID);
    ccw_node entry = ccw_ir_block_add(m, fn, "entry");
    ccw_node next = ccw_ir_block_add(m, fn, "next");
    ccw_node jump = ccw_ir_instr_build(m, "br", CCW_TY_VOID);
    ccw_ir_instr_add_operand(m, jump, ccw_ir_operand_block(m, "next"));
    ccw_ir_block_append_instr(m, entry, jump);
    ccw_node ret = ccw_ir_instr_build(m, "ret", CCW_TY_VOID);
    ccw_ir_block_append_instr(m, next, ret);
    return m;
}

static ccw_ir *phi_module(void)
{
    ccw_ir *m = ccw_ir_module_create("phi-sample", CCW_PROFILE_TILLY);
    ccw_node fn = ccw_ir_function_add(m, "f", CCW_TY_I64);
    ccw_node blk = ccw_ir_block_add(m, fn, "entry");

    ccw_node redundant = ccw_ir_instr_build(m, "phi", CCW_TY_I64);
    ccw_ir_instr_set_dest(m, redundant, "same");
    ccw_ir_instr_add_operand(m, redundant, ccw_ir_operand_reg(m, "x"));
    ccw_ir_instr_add_operand(m, redundant, ccw_ir_operand_reg(m, "x"));
    ccw_ir_block_append_instr(m, blk, redundant);

    ccw_node distinct = ccw_ir_instr_build(m, "phi", CCW_TY_I64);
    ccw_ir_instr_set_dest(m, distinct, "different");
    ccw_ir_instr_add_operand(m, distinct, ccw_ir_operand_reg(m, "x"));
    ccw_ir_instr_add_operand(m, distinct, ccw_ir_operand_reg(m, "y"));
    ccw_ir_block_append_instr(m, blk, distinct);
    return m;
}

static ccw_ir *null_check_module(void)
{
    ccw_ir *m = ccw_ir_module_create("null-check-sample", CCW_PROFILE_TILLY);
    ccw_node fn = ccw_ir_function_add(m, "f", CCW_TY_VOID);
    ccw_node blk = ccw_ir_block_add(m, fn, "entry");

    for (int i = 0; i < 2; i++) {
        ccw_node check = ccw_ir_instr_build(m, "null-check", CCW_TY_VOID);
        ccw_ir_instr_add_operand(m, check, ccw_ir_operand_reg(m, "value"));
        ccw_ir_block_append_instr(m, blk, check);
    }
    ccw_node other = ccw_ir_instr_build(m, "null-check", CCW_TY_VOID);
    ccw_ir_instr_add_operand(m, other, ccw_ir_operand_reg(m, "other"));
    ccw_ir_block_append_instr(m, blk, other);
    return m;
}

static ccw_ir *tail_call_module(ccw_node *call_out)
{
    ccw_ir *m = ccw_ir_module_create("tail-call-sample", CCW_PROFILE_TILLY);
    ccw_node fn = ccw_ir_function_add(m, "f", CCW_TY_I64);
    ccw_node blk = ccw_ir_block_add(m, fn, "entry");

    ccw_node call = ccw_ir_instr_build(m, "call", CCW_TY_I64);
    ccw_ir_instr_set_dest(m, call, "result");
    ccw_ir_instr_add_operand(m, call, ccw_ir_operand_func(m, "callee"));
    ccw_ir_block_append_instr(m, blk, call);

    ccw_node ret = ccw_ir_instr_build(m, "ret", CCW_TY_VOID);
    ccw_ir_instr_add_operand(m, ret, ccw_ir_operand_reg(m, "result"));
    ccw_ir_block_append_instr(m, blk, ret);
    *call_out = call;
    return m;
}

static ccw_ir *deopt_module(ccw_node *deopt_out)
{
    ccw_ir *m = ccw_ir_module_create("deopt-sample", CCW_PROFILE_ON1X);
    ccw_node fn = ccw_ir_function_add(m, "f", CCW_TY_VOID);
    ccw_node blk = ccw_ir_block_add(m, fn, "entry");
    ccw_node deopt = ccw_ir_instr_build(m, "deopt", CCW_TY_VOID);
    ccw_ir_instr_add_operand(m, deopt, ccw_ir_operand_block(m, "entry"));
    ccw_ir_block_append_instr(m, blk, deopt);
    *deopt_out = deopt;
    return m;
}

int main(void)
{
    ccw_executor *ex = ccw_executor_create();
    CCW_CHECK(ex != NULL, "executor creation failed");
    if (ex == NULL) return ccw_test_report("glue-executor");

    CCW_CHECK(ccw_executor_abi_version(ex) == CCW_GLUE_ABI_VERSION,
              "executor must report ABI version %d", CCW_GLUE_ABI_VERSION);
    CCW_CHECK(ccw_executor_name(ex) != NULL && ccw_executor_name(ex)[0] != '\0',
              "executor must identify itself");

    CCW_CHECK(ccw_host_register_core_accessors(ex) == CCW_OK,
              "core accessor registration failed");

    /* --- a kernel missing a required export fails to load --- */
    char *bad = path_in(CCW_FIXTURE_DIR, "incomplete-kernel.scm");
    char *err = NULL;
    int bad_id = ccw_kernel_load(ex, bad, &err);
    CCW_CHECK(bad_id == CCW_ERR_LOAD, "kernel missing an export must fail to load");
    CCW_CHECK(err != NULL, "failed load must set error_message");
    free(err);
    free(bad);

    /* --- load the real kernel --- */
    char *kpath = path_in(CCW_KERNEL_DIR, "strength-reduce.scm");
    err = NULL;
    int kid = ccw_kernel_load(ex, kpath, &err);
    CCW_CHECK(kid >= 0, "strength-reduce failed to load: %s", err ? err : "");
    free(err);
    free(kpath);
    if (kid < 0) { ccw_executor_destroy(ex); return ccw_test_report("glue-executor"); }

    /* --- kernel-info marshalling (caller frees each string) --- */
    char *name = NULL, *version = NULL, *description = NULL;
    CCW_CHECK(ccw_kernel_info(ex, kid, &name, &version, &description) == CCW_OK,
              "kernel-info failed");
    CCW_CHECK_STREQ(name, "strength-reduce");
    CCW_CHECK_STREQ(version, "1.0.0");
    CCW_CHECK(description != NULL, "kernel-info must report a description");
    free(name); free(version); free(description);

    /* --- live capability enumeration --- */
    int ncaps = ccw_kernel_capability_count(ex, kid);
    CCW_CHECK(ncaps == 1, "expected 1 capability, got %d", ncaps);
    CCW_CHECK_STREQ(ccw_kernel_capability(ex, kid, 0), "opt.strength-reduction");

    /* --- capability verified before dispatch --- */
    ccw_ir *m = sample_module();
    err = NULL;
    CCW_CHECK(ccw_kernel_apply(ex, kid, "opt.nonexistent", m, NULL, &err)
                  == CCW_ERR_NO_CAPABILITY,
              "unknown capability must be refused before dispatch");
    free(err);

    /* --- successful apply rewrites imul x,8 into shl x,3 --- */
    const char *options[] = { "level=2", NULL };
    err = NULL;
    ccw_status st = ccw_kernel_apply(ex, kid, "opt.strength-reduction", m, options, &err);
    CCW_CHECK(st == CCW_OK, "kernel-apply failed: %s", err ? err : "");
    free(err);

    ccw_node fn = ccw_ir_function_ref(m, 0);
    ccw_node blk = ccw_ir_function_block_ref(m, fn, 0);
    CCW_CHECK(ccw_ir_block_instr_count(m, blk) == 3, "instruction count changed");

    ccw_node i0 = ccw_ir_block_instr_ref(m, blk, 0);
    CCW_CHECK_STREQ(ccw_ir_instr_opcode(m, i0), "shl");
    int64_t shift = 0;
    CCW_CHECK(ccw_ir_const_int_value(m, ccw_ir_instr_operand(m, i0, 1), &shift) == CCW_OK
                  && shift == 3,
              "imul x,8 must become shl x,3");
    CCW_CHECK_STREQ(ccw_ir_instr_dest(m, i0), "t0");

    CCW_CHECK_STREQ(ccw_ir_instr_opcode(m, ccw_ir_block_instr_ref(m, blk, 1)), "imul");
    CCW_CHECK_STREQ(ccw_ir_instr_opcode(m, ccw_ir_block_instr_ref(m, blk, 2)), "imul");

    /* --- constant folding handles zero and the ABI integer boundary --- */
    char *const_path = path_in(CCW_KERNEL_DIR, "const-fold.scm");
    err = NULL;
    int const_id = ccw_kernel_load(ex, const_path, &err);
    CCW_CHECK(const_id >= 0, "const-fold failed to load: %s", err ? err : "");
    free(err);
    free(const_path);
    if (const_id >= 0) {
        ccw_ir *cm = constant_module();
        err = NULL;
        CCW_CHECK(ccw_kernel_apply(ex, const_id, "opt.constant-folding", cm, NULL, &err)
                      == CCW_OK,
                  "const-fold apply failed: %s", err ? err : "");
        free(err);

        ccw_node cfn = ccw_ir_function_ref(cm, 0);
        ccw_node cblk = ccw_ir_function_block_ref(cm, cfn, 0);
        ccw_node folded = ccw_ir_block_instr_ref(cm, cblk, 0);
        int64_t folded_value = -1;
        CCW_CHECK_STREQ(ccw_ir_instr_opcode(cm, folded), "imov");
        CCW_CHECK(ccw_ir_const_int_value(
                      cm, ccw_ir_instr_operand(cm, folded, 0), &folded_value)
                      == CCW_OK
                      && folded_value == 0,
                  "constant result zero must be folded");
        CCW_CHECK_STREQ(
            ccw_ir_instr_opcode(cm, ccw_ir_block_instr_ref(cm, cblk, 1)), "iadd");
        CCW_CHECK_STREQ(
            ccw_ir_instr_opcode(cm, ccw_ir_block_instr_ref(cm, cblk, 2)), "icmp.eq");
        ccw_ir_module_destroy(cm);
    }

    /* --- Phase 1 kernels publish facts and rewrite scalar uses --- */
    char *purity_path = path_in(CCW_KERNEL_DIR, "purity.scm");
    int purity_id = ccw_kernel_load(ex, purity_path, &err);
    CCW_CHECK(purity_id >= 0, "purity failed to load: %s", err ? err : "");
    free(err); err = NULL;
    free(purity_path);

    char *copy_path = path_in(CCW_KERNEL_DIR, "copy-prop.scm");
    int copy_id = ccw_kernel_load(ex, copy_path, &err);
    CCW_CHECK(copy_id >= 0, "copy-prop failed to load: %s", err ? err : "");
    free(err); err = NULL;
    free(copy_path);

    ccw_ir *copy_ir = copy_module();
    ccw_node copy_fn = ccw_ir_function_ref(copy_ir, 0);
    ccw_node copy_blk = ccw_ir_function_block_ref(copy_ir, copy_fn, 0);
    ccw_node copy_ins = ccw_ir_block_instr_ref(copy_ir, copy_blk, 0);
    if (purity_id >= 0) {
        CCW_CHECK(ccw_kernel_apply(ex, purity_id, "analysis.purity", copy_ir, NULL, &err)
                      == CCW_OK,
                  "purity apply failed: %s", err ? err : "");
        free(err); err = NULL;
        CCW_CHECK_STREQ(ccw_ir_attr_lookup(
                            copy_ir, copy_ins, "analysis.analysis.purity.side-effect?"),
                        "false");
    }
    if (copy_id >= 0) {
        CCW_CHECK(ccw_kernel_apply(ex, copy_id, "opt.copy-propagation", copy_ir, NULL, &err)
                      == CCW_OK,
                  "copy-prop apply failed: %s", err ? err : "");
        free(err); err = NULL;
        ccw_node add = ccw_ir_block_instr_ref(copy_ir, copy_blk, 1);
        CCW_CHECK_STREQ(ccw_ir_operand_name(copy_ir, ccw_ir_instr_operand(copy_ir, add, 0)),
                        "x");
    }
    ccw_ir_module_destroy(copy_ir);

    char *unreachable_path = path_in(CCW_KERNEL_DIR, "unreachable-elim.scm");
    int unreachable_id = ccw_kernel_load(ex, unreachable_path, &err);
    CCW_CHECK(unreachable_id >= 0, "unreachable-elim failed to load: %s", err ? err : "");
    free(err); err = NULL;
    free(unreachable_path);
    ccw_ir *unreachable_ir = unreachable_module();
    if (unreachable_id >= 0) {
        CCW_CHECK(ccw_kernel_apply(ex, unreachable_id, "opt.unreachable-elim",
                                   unreachable_ir, NULL, &err) == CCW_OK,
                  "unreachable-elim apply failed: %s", err ? err : "");
        free(err); err = NULL;
        CCW_CHECK(ccw_ir_function_block_count(
                      unreachable_ir, ccw_ir_function_ref(unreachable_ir, 0)) == 1,
                  "unreachable-elim must remove the detached block");
    }
    ccw_ir_module_destroy(unreachable_ir);

    char *merge_path = path_in(CCW_KERNEL_DIR, "block-merge.scm");
    int merge_id = ccw_kernel_load(ex, merge_path, &err);
    CCW_CHECK(merge_id >= 0, "block-merge failed to load: %s", err ? err : "");
    free(err); err = NULL;
    free(merge_path);
    ccw_ir *linear_ir = linear_module();
    if (merge_id >= 0) {
        CCW_CHECK(ccw_kernel_apply(ex, merge_id, "opt.block-merge",
                                   linear_ir, NULL, &err) == CCW_OK,
                  "block-merge apply failed: %s", err ? err : "");
        free(err); err = NULL;
        CCW_CHECK(ccw_ir_function_block_count(
                      linear_ir, ccw_ir_function_ref(linear_ir, 0)) == 1,
                  "block-merge must merge a linear block pair");
        ccw_node entry = ccw_ir_function_block_ref(
            linear_ir, ccw_ir_function_ref(linear_ir, 0), 0);
        CCW_CHECK_STREQ(ccw_ir_instr_opcode(
                            linear_ir, ccw_ir_block_instr_ref(linear_ir, entry, 0)),
                        "ret");
    }
    ccw_ir_module_destroy(linear_ir);

    /* --- formerly metadata-only kernels now perform conservative work --- */
    char *cfg_path = path_in(CCW_KERNEL_DIR, "cfg-canonicalize.scm");
    int cfg_id = ccw_kernel_load(ex, cfg_path, &err);
    CCW_CHECK(cfg_id >= 0, "cfg-canonicalize failed to load: %s", err ? err : "");
    free(err); err = NULL;
    free(cfg_path);
    linear_ir = linear_module();
    if (cfg_id >= 0) {
        CCW_CHECK(ccw_kernel_apply(ex, cfg_id, "normalize.cfg",
                                   linear_ir, NULL, &err) == CCW_OK,
                  "cfg-canonicalize apply failed: %s", err ? err : "");
        free(err); err = NULL;
        CCW_CHECK(ccw_ir_function_block_count(
                      linear_ir, ccw_ir_function_ref(linear_ir, 0)) == 1,
                  "cfg-canonicalize must collapse a linear CFG");
    }
    ccw_ir_module_destroy(linear_ir);

    char *phi_path = path_in(CCW_KERNEL_DIR, "phi-simplify.scm");
    int phi_id = ccw_kernel_load(ex, phi_path, &err);
    CCW_CHECK(phi_id >= 0, "phi-simplify failed to load: %s", err ? err : "");
    free(err); err = NULL;
    free(phi_path);
    ccw_ir *phi_ir = phi_module();
    if (phi_id >= 0) {
        CCW_CHECK(ccw_kernel_apply(ex, phi_id, "opt.phi-simplify",
                                   phi_ir, NULL, &err) == CCW_OK,
                  "phi-simplify apply failed: %s", err ? err : "");
        free(err); err = NULL;
        ccw_node phi_fn = ccw_ir_function_ref(phi_ir, 0);
        ccw_node phi_blk = ccw_ir_function_block_ref(phi_ir, phi_fn, 0);
        CCW_CHECK_STREQ(
            ccw_ir_instr_opcode(phi_ir, ccw_ir_block_instr_ref(phi_ir, phi_blk, 0)),
            "imov");
        CCW_CHECK_STREQ(
            ccw_ir_instr_opcode(phi_ir, ccw_ir_block_instr_ref(phi_ir, phi_blk, 1)),
            "phi");
    }
    ccw_ir_module_destroy(phi_ir);

    char *null_path = path_in(CCW_KERNEL_DIR, "null-check-elim.scm");
    int null_id = ccw_kernel_load(ex, null_path, &err);
    CCW_CHECK(null_id >= 0, "null-check-elim failed to load: %s", err ? err : "");
    free(err); err = NULL;
    free(null_path);
    ccw_ir *null_ir = null_check_module();
    if (null_id >= 0) {
        CCW_CHECK(ccw_kernel_apply(ex, null_id, "opt.null-check-elim",
                                   null_ir, NULL, &err) == CCW_OK,
                  "null-check-elim apply failed: %s", err ? err : "");
        free(err); err = NULL;
        ccw_node null_fn = ccw_ir_function_ref(null_ir, 0);
        ccw_node null_blk = ccw_ir_function_block_ref(null_ir, null_fn, 0);
        CCW_CHECK(ccw_ir_block_instr_count(null_ir, null_blk) == 2,
                  "null-check-elim must remove only the repeated SSA check");
    }
    ccw_ir_module_destroy(null_ir);

    char *tail_path = path_in(CCW_KERNEL_DIR, "tail-call.scm");
    int tail_id = ccw_kernel_load(ex, tail_path, &err);
    CCW_CHECK(tail_id >= 0, "tail-call failed to load: %s", err ? err : "");
    free(err); err = NULL;
    free(tail_path);
    ccw_node tail_call = 0;
    ccw_ir *tail_ir = tail_call_module(&tail_call);
    if (tail_id >= 0) {
        CCW_CHECK(ccw_kernel_apply(ex, tail_id, "opt.tailcall",
                                   tail_ir, NULL, &err) == CCW_OK,
                  "tail-call apply failed: %s", err ? err : "");
        free(err); err = NULL;
        CCW_CHECK_STREQ(ccw_ir_attr_lookup(
                            tail_ir, tail_call, "analysis.opt.tailcall.eligible?"),
                        "true");
    }
    ccw_ir_module_destroy(tail_ir);

    char *deopt_path = path_in(CCW_KERNEL_DIR, "deopt-points.scm");
    int deopt_id = ccw_kernel_load(ex, deopt_path, &err);
    CCW_CHECK(deopt_id >= 0, "deopt-points failed to load: %s", err ? err : "");
    free(err); err = NULL;
    free(deopt_path);
    ccw_node deopt = 0;
    ccw_ir *deopt_ir = deopt_module(&deopt);
    if (deopt_id >= 0) {
        CCW_CHECK(ccw_kernel_apply(ex, deopt_id, "vm.deopt-points",
                                   deopt_ir, NULL, &err) == CCW_OK,
                  "deopt-points apply failed: %s", err ? err : "");
        free(err); err = NULL;
        CCW_CHECK_STREQ(ccw_ir_attr_lookup(
                            deopt_ir, deopt,
                            "analysis.vm.deopt-points.requires-state?"),
                        "true");
    }
    ccw_ir_module_destroy(deopt_ir);

    /* --- implemented kernels advertise and accept their capability --- */
    char *reserved_path = path_in(CCW_KERNEL_DIR, "dce.scm");
    err = NULL;
    int reserved_id = ccw_kernel_load(ex, reserved_path, &err);
    CCW_CHECK(reserved_id >= 0, "dce kernel failed to load: %s", err ? err : "");
    free(err);
    free(reserved_path);
    if (reserved_id >= 0) {
        CCW_CHECK(ccw_kernel_capability_count(ex, reserved_id) == 1,
                  "dce kernel must advertise one capability");
        err = NULL;
        CCW_CHECK(ccw_kernel_apply(
                      ex, reserved_id, "opt.dead-code-elimination", m, NULL, &err)
                      == CCW_OK,
                  "dce capability must dispatch successfully");
        free(err);
    }

    /* --- accessor arity violation raises in Scheme --- */
    char *arity_path = path_in(CCW_FIXTURE_DIR, "bad-arity-kernel.scm");
    err = NULL;
    int aid = ccw_kernel_load(ex, arity_path, &err);
    CCW_CHECK(aid >= 0, "arity fixture failed to load: %s", err ? err : "");
    free(err);
    free(arity_path);
    if (aid >= 0) {
        err = NULL;
        CCW_CHECK(ccw_kernel_apply(ex, aid, "test.arity", m, NULL, &err) == CCW_ERR_KERNEL,
                  "accessor arity violation must surface as CCW_ERR_KERNEL");
        CCW_CHECK(err != NULL, "kernel error must carry condition text");
        free(err);
    }

    /* --- accessor failure surfaces as a Scheme condition --- */
    char *fail_path = path_in(CCW_FIXTURE_DIR, "accessor-error-kernel.scm");
    err = NULL;
    int fid = ccw_kernel_load(ex, fail_path, &err);
    CCW_CHECK(fid >= 0, "accessor-error fixture failed to load: %s", err ? err : "");
    free(err);
    free(fail_path);
    if (fid >= 0) {
        err = NULL;
        CCW_CHECK(ccw_kernel_apply(ex, fid, "test.accessor-error", m, NULL, &err)
                      == CCW_ERR_KERNEL,
                  "accessor error must surface as CCW_ERR_KERNEL");
        free(err);
    }

    CCW_CHECK(ccw_kernel_unload(ex, kid) == CCW_OK, "unload failed");
    CCW_CHECK(ccw_kernel_capability_count(ex, kid) == CCW_ERR_LOAD,
              "unloaded kernel ids must not be reusable");

    /* --- the host can interpose on and reject structural edits --- */
    char *reject_path = path_in(CCW_FIXTURE_DIR, "rejected-edit-kernel.scm");
    err = NULL;
    int rid = ccw_kernel_load(ex, reject_path, &err);
    CCW_CHECK(rid >= 0, "edit-rejection fixture failed to load: %s", err ? err : "");
    free(err);
    free(reject_path);
    if (rid >= 0) {
        const char *before = ccw_ir_instr_opcode(m, ccw_ir_block_instr_ref(m, blk, 0));
        char *saved = copy_string(before);
        ccw_host_set_edit_hook(reject_all_edits, NULL);
        err = NULL;
        CCW_CHECK(ccw_kernel_apply(ex, rid, "test.rejected-edit", m, NULL, &err)
                      == CCW_ERR_KERNEL,
                  "a rejected edit must surface as a kernel error");
        free(err);
        ccw_host_set_edit_hook(NULL, NULL);
        CCW_CHECK(g_edits_seen > 0, "host must observe every structural edit");
        CCW_CHECK_STREQ(ccw_ir_instr_opcode(m, ccw_ir_block_instr_ref(m, blk, 0)),
                        saved ? saved : "");
        free(saved);
    }

    /* --- a kernel-mutated module still round-trips and validates --- */
    char *err2 = NULL;
    CCW_CHECK(ccw_ir_validate(m, &err2) == CCW_OK,
              "module must still validate after kernel edits: %s", err2 ? err2 : "");
    free(err2);
    char *text = ccw_ir_print(m);
    err2 = NULL;
    ccw_ir *back = ccw_ir_parse(text, &err2);
    CCW_CHECK(back != NULL && ccw_ir_equal(m, back),
              "kernel-edited module must round-trip: %s", err2 ? err2 : "");
    free(err2);
    free(text);
    ccw_ir_module_destroy(back);

    ccw_ir_module_destroy(m);
    ccw_executor_destroy(ex);
    return ccw_test_report("glue-executor");
}
