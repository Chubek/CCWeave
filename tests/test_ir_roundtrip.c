/* §5.4 conformance: text <-> memory round-trip on every construct in
 * both profiles. parse(print(m)) must be structurally equal to m, and
 * print(parse(t)) must be byte-identical to t. */

#include "ccw_ir.h"
#include "tilly/ccw_tilly.h"
#include "on1x/ccw_on1x.h"
#include "ccw_test.h"

#include <stdlib.h>

static void roundtrip(ccw_ir *m, const char *label)
{
    char *text = ccw_ir_print(m);
    CCW_CHECK(text != NULL, "%s: printing failed", label);
    if (text == NULL) return;

    char *err = NULL;
    ccw_ir *reparsed = ccw_ir_parse(text, &err);
    CCW_CHECK(reparsed != NULL, "%s: parse failed: %s", label,
              err ? err : "(no message)");
    free(err);
    if (reparsed == NULL) { free(text); return; }

    CCW_CHECK(ccw_ir_equal(m, reparsed), "%s: structures differ after round-trip",
              label);

    char *text2 = ccw_ir_print(reparsed);
    CCW_CHECK(text2 != NULL && strcmp(text, text2) == 0,
              "%s: text not stable across round-trip:\n--- first ---\n%s\n--- second ---\n%s",
              label, text, text2 ? text2 : "(null)");

    free(text2);
    free(text);
    ccw_ir_module_destroy(reparsed);
}

/* Core constructs available to both profiles. */
static ccw_node build_core_function(ccw_ir *m)
{
    ccw_node fn = ccw_ir_function_add(m, "core", CCW_TY_I64);
    ccw_ir_function_add_param(m, fn, CCW_TY_I64, "x");
    ccw_ir_function_add_param(m, fn, CCW_TY_F64, "y");
    ccw_ir_attr_set(m, fn, "inline", "never");

    ccw_node entry = ccw_ir_block_add(m, fn, "entry");
    ccw_node next  = ccw_ir_block_add(m, fn, "next");
    ccw_ir_attr_set(m, entry, "freq", "1000");

    ccw_node mul = ccw_ir_instr_build(m, "imul", CCW_TY_I64);
    ccw_ir_instr_set_dest(m, mul, "t0");
    ccw_ir_instr_add_operand(m, mul, ccw_ir_operand_reg(m, "x"));
    ccw_ir_instr_add_operand(m, mul, ccw_ir_operand_const_int(m, CCW_TY_I64, 8));
    ccw_ir_block_append_instr(m, entry, mul);

    ccw_node fadd = ccw_ir_instr_build(m, "fadd", CCW_TY_F64);
    ccw_ir_instr_set_dest(m, fadd, "t1");
    ccw_ir_instr_add_operand(m, fadd, ccw_ir_operand_reg(m, "y"));
    ccw_ir_instr_add_operand(m, fadd, ccw_ir_operand_const_float(m, CCW_TY_F64, 0.5));
    ccw_ir_attr_set(m, fadd, "fastmath", "off");
    ccw_ir_block_append_instr(m, entry, fadd);

    ccw_node br = ccw_ir_instr_build(m, "br", CCW_TY_VOID);
    ccw_ir_instr_add_operand(m, br, ccw_ir_operand_block(m, "next"));
    ccw_ir_block_append_instr(m, entry, br);

    ccw_node ret = ccw_ir_instr_build(m, "ret", CCW_TY_VOID);
    ccw_ir_instr_add_operand(m, ret, ccw_ir_operand_reg(m, "t0"));
    ccw_ir_block_append_instr(m, next, ret);
    return fn;
}

int main(void)
{
    /* --- Tilly --- */
    ccw_ir *tilly = ccw_ir_module_create("tilly-sample", CCW_PROFILE_TILLY);
    ccw_node tfn = build_core_function(tilly);
    ccw_node tblk = ccw_ir_function_block_ref(tilly, tfn, 0);
    ccw_tilly_build_call_static(tilly, tblk, "c0", CCW_TY_I64, "callee");
    ccw_tilly_build_reloc(tilly, tblk, "r0", "global_table", 16);
    ccw_tilly_set_link_section(tilly, tfn, ".text.hot");
    ccw_tilly_set_layout(tilly, "dense");
    ccw_node tnext = ccw_ir_function_block_ref(tilly, tfn, 1);
    CCW_CHECK(ccw_ir_block_successor_count(tilly, tblk) == 1,
              "entry block must have one CFG successor");
    CCW_CHECK(ccw_ir_block_successor_ref(tilly, tblk, 0) == tnext,
              "entry successor must resolve its block target");
    CCW_CHECK(ccw_ir_block_predecessor_count(tilly, tnext) == 1,
              "next block must have one CFG predecessor");
    CCW_CHECK(ccw_ir_block_predecessor_ref(tilly, tnext, 0) == tblk,
              "next predecessor must be entry");
    char *err = NULL;
    CCW_CHECK(ccw_ir_validate(tilly, &err) == CCW_OK,
              "tilly module must validate: %s", err ? err : "");
    free(err);
    roundtrip(tilly, "tilly");

    /* --- On1x --- */
    ccw_ir *on1x = ccw_ir_module_create("on1x-sample", CCW_PROFILE_ON1X);
    ccw_node ofn = build_core_function(on1x);
    ccw_node oblk = ccw_ir_function_block_ref(on1x, ofn, 0);
    ccw_on1x_build_call_dynamic(on1x, oblk, "d0", CCW_TY_I64, "x", "selector", 4);
    ccw_on1x_build_safepoint(on1x, oblk);
    ccw_on1x_build_deopt(on1x, oblk, "next");
    err = NULL;
    CCW_CHECK(ccw_ir_validate(on1x, &err) == CCW_OK,
              "on1x module must validate: %s", err ? err : "");
    free(err);
    roundtrip(on1x, "on1x");

    /* --- empty module still round-trips --- */
    ccw_ir *empty = ccw_ir_module_create("empty", CCW_PROFILE_TILLY);
    roundtrip(empty, "empty");

    /* --- a module header without a profile is rejected --- */
    err = NULL;
    ccw_ir *bad = ccw_ir_parse("(module \"x\" (function @f i64 (params)))", &err);
    CCW_CHECK(bad == NULL, "module without a profile must not parse");
    CCW_CHECK(err != NULL, "failed parse must report an error message");
    free(err);
    ccw_ir_module_destroy(bad);

    ccw_ir_module_destroy(tilly);
    ccw_ir_module_destroy(on1x);
    ccw_ir_module_destroy(empty);
    return ccw_test_report("ir-roundtrip");
}
