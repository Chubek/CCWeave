/* §5.3: profile validation rejects cross-profile constructs. */

#include "ccw_ir.h"
#include "tilly/ccw_tilly.h"
#include "on1x/ccw_on1x.h"
#include "ccw_test.h"

#include <stdlib.h>

static ccw_node one_block(ccw_ir *m, const char *fname)
{
    ccw_node fn = ccw_ir_function_add(m, fname, CCW_TY_I64);
    return ccw_ir_block_add(m, fn, "entry");
}

int main(void)
{
    /* --- On1x constructs are rejected in a Tilly module --- */
    ccw_ir *tilly = ccw_ir_module_create("t", CCW_PROFILE_TILLY);
    ccw_node tblk = one_block(tilly, "f");
    ccw_on1x_build_call_dynamic(tilly, tblk, "d", CCW_TY_I64, "recv", "sel", 2);
    char *err = NULL;
    CCW_CHECK(ccw_ir_validate(tilly, &err) != CCW_OK,
              "dynamic dispatch must be rejected in a Tilly module");
    CCW_CHECK(err != NULL, "rejection must explain itself");
    free(err);
    ccw_ir_module_destroy(tilly);

    /* --- inline-cache metadata alone is enough to reject --- */
    ccw_ir *tilly2 = ccw_ir_module_create("t2", CCW_PROFILE_TILLY);
    ccw_node t2blk = one_block(tilly2, "f");
    ccw_node ins = ccw_ir_instr_build(tilly2, "call.indirect", CCW_TY_I64);
    ccw_ir_instr_add_operand(tilly2, ins, ccw_ir_operand_reg(tilly2, "x"));
    ccw_ir_block_append_instr(tilly2, t2blk, ins);
    ccw_ir_attr_set(tilly2, ins, "dispatch.inline-cache", "4");
    err = NULL;
    CCW_CHECK(ccw_ir_validate(tilly2, &err) != CCW_OK,
              "inline-cache metadata must be rejected in a Tilly module");
    free(err);
    ccw_ir_module_destroy(tilly2);

    /* --- Tilly constructs are rejected in an On1x module --- */
    ccw_ir *on1x = ccw_ir_module_create("o", CCW_PROFILE_ON1X);
    ccw_node oblk = one_block(on1x, "f");
    ccw_tilly_build_reloc(on1x, oblk, "r", "sym", 0);
    err = NULL;
    CCW_CHECK(ccw_ir_validate(on1x, &err) != CCW_OK,
              "relocations must be rejected in an On1x module");
    free(err);
    ccw_ir_module_destroy(on1x);

    /* --- profile-specific setters refuse the wrong profile --- */
    ccw_ir *on1x2 = ccw_ir_module_create("o2", CCW_PROFILE_ON1X);
    ccw_node ofn = ccw_ir_function_add(on1x2, "f", CCW_TY_I64);
    CCW_CHECK(ccw_tilly_set_link_section(on1x2, ofn, ".text") != CCW_OK,
              "link-section attributes are Tilly-only");
    CCW_CHECK(ccw_tilly_set_layout(on1x2, "dense") != CCW_OK,
              "layout directives are Tilly-only");
    ccw_ir_module_destroy(on1x2);

    /* --- core constructs validate under both profiles --- */
    for (int p = 0; p < 2; p++) {
        ccw_ir *m = ccw_ir_module_create("core", p == 0 ? CCW_PROFILE_TILLY
                                                        : CCW_PROFILE_ON1X);
        ccw_node blk = one_block(m, "f");
        ccw_node add = ccw_ir_instr_build(m, "iadd", CCW_TY_I64);
        ccw_ir_instr_set_dest(m, add, "t0");
        ccw_ir_instr_add_operand(m, add, ccw_ir_operand_reg(m, "x"));
        ccw_ir_instr_add_operand(m, add, ccw_ir_operand_const_int(m, CCW_TY_I64, 1));
        ccw_ir_block_append_instr(m, blk, add);
        err = NULL;
        CCW_CHECK(ccw_ir_validate(m, &err) == CCW_OK,
                  "core constructs must validate in both profiles: %s", err ? err : "");
        free(err);
        ccw_ir_module_destroy(m);
    }

    /* --- the declared profile survives a round-trip --- */
    ccw_ir *o3 = ccw_ir_module_create("o3", CCW_PROFILE_ON1X);
    ccw_node o3blk = one_block(o3, "f");
    ccw_on1x_build_safepoint(o3, o3blk);
    char *text = ccw_ir_print(o3);
    err = NULL;
    ccw_ir *back = ccw_ir_parse(text, &err);
    CCW_CHECK(back != NULL && ccw_ir_module_profile(back) == CCW_PROFILE_ON1X,
              "profile declaration must survive the round-trip");
    free(err);
    free(text);
    ccw_ir_module_destroy(back);
    ccw_ir_module_destroy(o3);

    return ccw_test_report("ir-profiles");
}
