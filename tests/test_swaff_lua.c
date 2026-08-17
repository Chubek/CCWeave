/* Lua frontend conformance: scalar expressions, calls, locals, and
 * explicit malformed-CST handling. */

#include "ccw_swaff.h"
#include "ccw_test.h"

#include <stdlib.h>

static bool function_has_opcode(const ccw_ir *ir, ccw_node fn,
                                const char *opcode)
{
    int blocks = ccw_ir_function_block_count(ir, fn);
    for (int bi = 0; bi < blocks; bi++) {
        ccw_node block = ccw_ir_function_block_ref(ir, fn, bi);
        int instructions = ccw_ir_block_instr_count(ir, block);
        for (int ii = 0; ii < instructions; ii++) {
            ccw_node ins = ccw_ir_block_instr_ref(ir, block, ii);
            if (strcmp(ccw_ir_instr_opcode(ir, ins), opcode) == 0) return true;
        }
    }
    return false;
}

static void check_profile(ccw_profile profile)
{
    const char *source =
        "function add(a, b)\n"
        "  local sum = a + b\n"
        "  if sum > 10 then\n"
        "    return sum - 1\n"
        "  end\n"
        "  return sum\n"
        "end\n";
    ccw_swaff_report report;
    char *error = NULL;
    ccw_ir *ir = ccw_swaff_lower(
        ccw_swaff_frontend_lua(), source, strlen(source), "lua", profile,
        CCW_SWAFF_REJECT_ON_ERROR, &report, &error);

    CCW_CHECK(ir != NULL, "Lua lowering failed: %s", error ? error : "");
    free(error);
    if (ir == NULL) return;
    CCW_CHECK(report.error_nodes == 0 && report.missing_nodes == 0,
              "valid Lua produced malformed CST");
    CCW_CHECK(report.functions_lowered == 1, "expected one Lua function");
    CCW_CHECK(report.declarations_lowered == 1, "expected one local declaration");
    CCW_CHECK(report.unsupported_nodes == 0, "unexpected unsupported Lua nodes");
    CCW_CHECK(ccw_ir_function_count(ir) == 1, "expected one IR function");
    {
        ccw_node fn = ccw_ir_function_ref(ir, 0);
        CCW_CHECK_STREQ(ccw_ir_function_name(ir, fn), "add");
        CCW_CHECK(ccw_ir_function_param_count(ir, fn) == 2,
                  "Lua parameters were not lowered");
        CCW_CHECK(function_has_opcode(ir, fn, "iadd"), "addition missing");
        CCW_CHECK(function_has_opcode(ir, fn, "icmp.gt"), "comparison missing");
        CCW_CHECK(function_has_opcode(ir, fn, "br.cond"), "branch missing");
        CCW_CHECK(function_has_opcode(ir, fn, "ret"), "return missing");
    }
    error = NULL;
    CCW_CHECK(ccw_ir_validate(ir, &error) == CCW_OK,
              "lowered Lua IR did not validate: %s", error ? error : "");
    free(error);
    ccw_ir_module_destroy(ir);
}

int main(void)
{
    CCW_CHECK(ccw_swaff_available(), "Tree-sitter support must be enabled");
    CCW_CHECK_STREQ(ccw_swaff_frontend_name(ccw_swaff_frontend_lua()), "lua");
    check_profile(CCW_PROFILE_TILLY);
    check_profile(CCW_PROFILE_ON1X);

    {
        const char *bad = "function broken(a,\n";
        ccw_swaff_report report;
        char *error = NULL;
        ccw_ir *ir = ccw_swaff_lower(
            ccw_swaff_frontend_lua(), bad, strlen(bad), "bad",
            CCW_PROFILE_TILLY, CCW_SWAFF_REJECT_ON_ERROR, &report, &error);
        CCW_CHECK(ir == NULL && error != NULL,
                  "reject policy must refuse malformed Lua");
        free(error);
    }
    return ccw_test_report("swaff-lua");
}
