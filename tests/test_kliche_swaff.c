/* Kliche stereotypes build valid core IR under both profiles (§6.1),
 * and the Swaff adapter decides explicitly about ERROR/MISSING (§6.2). */

#include "ccw_kliche.h"
#include "ccw_swaff.h"
#include "ccw_test.h"

#include <stdlib.h>

static void check_stereotypes(ccw_profile profile, const char *label)
{
    ccw_ir *m = ccw_ir_module_create("kliche", profile);
    ccw_node fn = ccw_ir_function_add(m, "f", CCW_TY_I64);
    ccw_node blk = ccw_ir_block_add(m, fn, "entry");

    /* functional */
    CCW_CHECK(ccw_kliche_closure_alloc(m, blk, "c0", "code", 2) != 0,
              "%s: closure.alloc failed", label);
    CCW_CHECK(ccw_kliche_closure_capture(m, blk, "c0", 0, "x") != 0,
              "%s: closure.capture failed", label);
    CCW_CHECK(ccw_kliche_closure_ref(m, blk, "v0", "c0", 0) != 0,
              "%s: closure.ref failed", label);
    CCW_CHECK(ccw_kliche_closure_apply(m, blk, "r0", "c0", "x") != 0,
              "%s: closure application failed", label);

    /* imperative */
    CCW_CHECK(ccw_kliche_local_alloc(m, blk, "s0", CCW_TY_I64) != 0,
              "%s: local.alloc failed", label);
    CCW_CHECK(ccw_kliche_local_store(m, blk, "s0", "x") != 0,
              "%s: local.store failed", label);
    CCW_CHECK(ccw_kliche_local_load(m, blk, "l0", "s0", CCW_TY_I64) != 0,
              "%s: local.load failed", label);
    CCW_CHECK(ccw_kliche_branch_if(m, blk, "l0", "then", "else") != 0,
              "%s: br.cond failed", label);

    /* oop */
    CCW_CHECK(ccw_kliche_object_alloc(m, blk, "o0", "Point", 2) != 0,
              "%s: object.alloc failed", label);
    CCW_CHECK(ccw_kliche_vtable_load(m, blk, "vt", "o0") != 0,
              "%s: vtable.load failed", label);
    CCW_CHECK(ccw_kliche_vtable_dispatch(m, blk, "d0", "vt", 3, "o0") != 0,
              "%s: vtable.dispatch failed", label);
    CCW_CHECK(ccw_kliche_frame_push(m, blk, "handler") != 0,
              "%s: frame.push failed", label);
    CCW_CHECK(ccw_kliche_frame_pop(m, blk) != 0, "%s: frame.pop failed", label);

    /* Stereotypes must not require a profile, so validation passes for both. */
    char *err = NULL;
    CCW_CHECK(ccw_ir_validate(m, &err) == CCW_OK,
              "%s: stereotype output must validate: %s", label, err ? err : "");
    free(err);

    /* And the result must round-trip like any other module. */
    char *text = ccw_ir_print(m);
    err = NULL;
    ccw_ir *back = ccw_ir_parse(text, &err);
    CCW_CHECK(back != NULL && ccw_ir_equal(m, back),
              "%s: stereotype output must round-trip: %s", label, err ? err : "");
    free(err);
    free(text);
    ccw_ir_module_destroy(back);
    ccw_ir_module_destroy(m);
}

int main(void)
{
    check_stereotypes(CCW_PROFILE_TILLY, "tilly");
    check_stereotypes(CCW_PROFILE_ON1X, "on1x");

    const ccw_swaff_frontend *fe = ccw_swaff_frontend_c();
    CCW_CHECK_STREQ(ccw_swaff_frontend_name(fe), "c");

    if (!ccw_swaff_available()) {
        /* Without Tree-sitter the adapter must fail loudly, not silently. */
        char *err = NULL;
        ccw_ir *m = ccw_swaff_lower(fe, "int main(void) { return 0; }", 28,
                                    "m", CCW_PROFILE_TILLY,
                                    CCW_SWAFF_REJECT_ON_ERROR, NULL, &err);
        CCW_CHECK(m == NULL && err != NULL,
                  "a build without Tree-sitter must report why lowering is unavailable");
        free(err);

        err = NULL;
        m = ccw_swaff_lower(ccw_swaff_frontend_ocaml(),
                            "let identity x = x\n", 19, "m",
                            CCW_PROFILE_TILLY, CCW_SWAFF_REJECT_ON_ERROR,
                            NULL, &err);
        CCW_CHECK(m == NULL && err != NULL,
                  "the OCaml adapter must report disabled Tree-sitter support");
        free(err);
        return ccw_test_report("kliche-swaff (no tree-sitter)");
    }

    /* --- well-formed source lowers --- */
    const char *good = "int add(int a, int b) { int c; return c; }\n";
    ccw_swaff_report report;
    char *err = NULL;
    ccw_ir *m = ccw_swaff_lower(fe, good, strlen(good), "good", CCW_PROFILE_TILLY,
                                CCW_SWAFF_REJECT_ON_ERROR, &report, &err);
    CCW_CHECK(m != NULL, "well-formed C must lower: %s", err ? err : "");
    free(err);
    if (m != NULL) {
        CCW_CHECK(report.functions_lowered == 1, "expected one lowered function");
        CCW_CHECK(ccw_ir_function_count(m) == 1, "module must contain one function");
        CCW_CHECK_STREQ(ccw_ir_function_name(m, ccw_ir_function_ref(m, 0)), "add");
        err = NULL;
        CCW_CHECK(ccw_ir_validate(m, &err) == CCW_OK,
                  "lowered module must validate: %s", err ? err : "");
        free(err);
        ccw_ir_module_destroy(m);
    }

    /* --- malformed source: reject policy --- */
    const char *bad = "int broken( { ;;; }\n";
    err = NULL;
    ccw_ir *rejected = ccw_swaff_lower(fe, bad, strlen(bad), "bad", CCW_PROFILE_TILLY,
                                       CCW_SWAFF_REJECT_ON_ERROR, &report, &err);
    CCW_CHECK(rejected == NULL, "reject policy must refuse a CST with ERROR nodes");
    CCW_CHECK(err != NULL, "rejection must explain itself");
    free(err);

    /* --- malformed source: recover policy --- */
    err = NULL;
    ccw_ir *recovered = ccw_swaff_lower(fe, bad, strlen(bad), "bad", CCW_PROFILE_TILLY,
                                        CCW_SWAFF_RECOVER_ON_ERROR, &report, &err);
    CCW_CHECK(recovered != NULL, "recover policy must produce a module: %s",
              err ? err : "");
    CCW_CHECK(report.recovered_subtrees > 0 || report.error_nodes > 0,
              "recovery must be reported, not silent");
    free(err);
    ccw_ir_module_destroy(recovered);

    return ccw_test_report("kliche-swaff");
}
