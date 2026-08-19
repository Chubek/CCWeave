/* Delphi/Pascal frontend conformance: routine headers, locals, scalar
 * expressions, calls, conditionals, loops, and malformed-CST policy. */

#include "ccw_swaff.h"
#include "ccw_test.h"

#include <stdlib.h>
#include <string.h>

static bool has_opcode(const ccw_ir *ir, ccw_node fn, const char *opcode)
{
    for (int bi = 0; bi < ccw_ir_function_block_count(ir, fn); bi++) {
        ccw_node b = ccw_ir_function_block_ref(ir, fn, bi);
        for (int ii = 0; ii < ccw_ir_block_instr_count(ir, b); ii++) {
            ccw_node ins = ccw_ir_block_instr_ref(ir, b, ii);
            if (ccw_ir_instr_opcode(ir, ins) &&
                strcmp(ccw_ir_instr_opcode(ir, ins), opcode) == 0)
                return true;
        }
    }
    return false;
}

static void check_profile(ccw_profile profile)
{
    const char *source =
        "function Twice(x: Integer): Integer;\n"
        "begin\n"
        "  Twice := x * 2;\n"
        "end;\n"
        "procedure Choose(x: Integer);\n"
        "var y: Integer;\n"
        "begin\n"
        "  y := x + 1;\n"
        "  if y > 2 then y := y - 1 else y := y + 1;\n"
        "  while y < 10 do y := y + 1;\n"
        "end;\n";
    ccw_swaff_report report;
    char *error = NULL;
    ccw_ir *ir = ccw_swaff_lower(
        ccw_swaff_frontend_delphi(), source, strlen(source), "delphi",
        profile, CCW_SWAFF_REJECT_ON_ERROR, &report, &error);
    CCW_CHECK(ir != NULL, "Delphi lowering failed: %s",
              error ? error : "(no message)");
    free(error);
    if (!ir) return;
    CCW_CHECK(report.error_nodes == 0 && report.missing_nodes == 0,
              "valid Delphi produced malformed CST");
    CCW_CHECK(report.functions_lowered == 2 && ccw_ir_function_count(ir) == 2,
              "expected two lowered routines");
    CCW_CHECK(report.declarations_lowered == 1, "expected one local declaration");
    ccw_node twice = ccw_ir_function_ref(ir, 0);
    ccw_node choose = ccw_ir_function_ref(ir, 1);
    CCW_CHECK_STREQ(ccw_ir_function_name(ir, twice), "Twice");
    CCW_CHECK(has_opcode(ir, twice, "imul"), "multiply was not lowered");
    CCW_CHECK(has_opcode(ir, choose, "br.cond") &&
              has_opcode(ir, choose, "iadd") &&
              has_opcode(ir, choose, "isub"),
              "conditional/loop expressions were not lowered");
    error = NULL;
    CCW_CHECK(ccw_ir_validate(ir, &error) == CCW_OK,
              "lowered Delphi IR did not validate: %s",
              error ? error : "(no message)");
    free(error);
    ccw_ir_module_destroy(ir);
}

int main(void)
{
    CCW_CHECK(ccw_swaff_available(), "Tree-sitter support must be enabled");
    CCW_CHECK_STREQ(ccw_swaff_frontend_name(ccw_swaff_frontend_delphi()),
                    "delphi");
    check_profile(CCW_PROFILE_TILLY);
    check_profile(CCW_PROFILE_ON1X);

    const char *bad = "procedure Broken(x: Integer); begin if x > then ; end;";
    ccw_swaff_report report;
    char *error = NULL;
    ccw_ir *ir = ccw_swaff_lower(
        ccw_swaff_frontend_delphi(), bad, strlen(bad), "bad",
        CCW_PROFILE_TILLY, CCW_SWAFF_REJECT_ON_ERROR, &report, &error);
    CCW_CHECK(ir == NULL && error != NULL &&
              report.error_nodes + report.missing_nodes > 0,
              "reject policy must reject malformed Delphi CST");
    free(error);
    return ccw_test_report("swaff-delphi");
}
