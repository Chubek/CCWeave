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
