/* Lua frontend conformance: expressions, statements, loops, tables,
 * strings, and explicit malformed-CST handling. */

#include "ccw_swaff.h"
#include "ccw_test.h"

#include <stdlib.h>
#include <string.h>

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

static void check_basic(ccw_profile profile)
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
    CCW_CHECK(report.functions_lowered >= 1, "expected at least one Lua function");
    CCW_CHECK(report.declarations_lowered >= 1, "expected local declaration");
    CCW_CHECK(report.unsupported_nodes == 0, "unexpected unsupported Lua nodes");
    CCW_CHECK(ccw_ir_function_count(ir) >= 1, "expected at least one IR function");
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

static void check_strings(ccw_profile profile)
{
    const char *source =
        "function greet(name)\n"
        "  local msg = \"Hello, \" .. name\n"
        "  return msg\n"
        "end\n";
    ccw_swaff_report report;
    char *error = NULL;
    ccw_ir *ir = ccw_swaff_lower(
        ccw_swaff_frontend_lua(), source, strlen(source), "lua", profile,
        CCW_SWAFF_REJECT_ON_ERROR, &report, &error);

    CCW_CHECK(ir != NULL, "String lowering failed: %s", error ? error : "");
    free(error);
    if (ir == NULL) return;
    CCW_CHECK(report.unsupported_nodes == 0, "unexpected unsupported nodes");
    CCW_CHECK(function_has_opcode(ir, ccw_ir_function_ref(ir, 0), "str.const"),
              "string constant missing");
    CCW_CHECK(function_has_opcode(ir, ccw_ir_function_ref(ir, 0), "str.concat"),
              "string concatenation missing");
    error = NULL;
    CCW_CHECK(ccw_ir_validate(ir, &error) == CCW_OK,
              "string IR did not validate: %s", error ? error : "");
    free(error);
    ccw_ir_module_destroy(ir);
}

static void check_while_loop(ccw_profile profile)
{
    const char *source =
        "function countdown(n)\n"
        "  while n > 0 do\n"
        "    n = n - 1\n"
        "  end\n"
        "  return n\n"
        "end\n";
    ccw_swaff_report report;
    char *error = NULL;
    ccw_ir *ir = ccw_swaff_lower(
        ccw_swaff_frontend_lua(), source, strlen(source), "lua", profile,
        CCW_SWAFF_REJECT_ON_ERROR, &report, &error);

    CCW_CHECK(ir != NULL, "While loop lowering failed: %s", error ? error : "");
    free(error);
    if (ir == NULL) return;
    CCW_CHECK(report.unsupported_nodes == 0, "unexpected unsupported nodes");
    CCW_CHECK(report.functions_lowered >= 1, "expected at least one function");
    error = NULL;
    CCW_CHECK(ccw_ir_validate(ir, &error) == CCW_OK,
              "while IR did not validate: %s", error ? error : "");
    free(error);
    ccw_ir_module_destroy(ir);
}

static void check_repeat_loop(ccw_profile profile)
{
    const char *source =
        "function countup(n)\n"
        "  local i = 0\n"
        "  repeat\n"
        "    i = i + 1\n"
        "  until i >= n\n"
        "  return i\n"
        "end\n";
    ccw_swaff_report report;
    char *error = NULL;
    ccw_ir *ir = ccw_swaff_lower(
        ccw_swaff_frontend_lua(), source, strlen(source), "lua", profile,
        CCW_SWAFF_REJECT_ON_ERROR, &report, &error);

    CCW_CHECK(ir != NULL, "Repeat loop lowering failed: %s", error ? error : "");
    free(error);
    if (ir == NULL) return;
    CCW_CHECK(report.unsupported_nodes == 0, "unexpected unsupported nodes");
    error = NULL;
    CCW_CHECK(ccw_ir_validate(ir, &error) == CCW_OK,
              "repeat IR did not validate: %s", error ? error : "");
    free(error);
    ccw_ir_module_destroy(ir);
}

static void check_for_loop(ccw_profile profile)
{
    const char *source =
        "function sum_to(n)\n"
        "  local s = 0\n"
        "  for i = 1, n do\n"
        "    s = s + i\n"
        "  end\n"
        "  return s\n"
        "end\n";
    ccw_swaff_report report;
    char *error = NULL;
    ccw_ir *ir = ccw_swaff_lower(
        ccw_swaff_frontend_lua(), source, strlen(source), "lua", profile,
        CCW_SWAFF_REJECT_ON_ERROR, &report, &error);

    CCW_CHECK(ir != NULL, "For loop lowering failed: %s", error ? error : "");
    free(error);
    if (ir == NULL) return;
    CCW_CHECK(report.unsupported_nodes == 0, "unexpected unsupported nodes");
    error = NULL;
    CCW_CHECK(ccw_ir_validate(ir, &error) == CCW_OK,
              "for IR did not validate: %s", error ? error : "");
    free(error);
    ccw_ir_module_destroy(ir);
}

static void check_elseif(ccw_profile profile)
{
    const char *source =
        "function sign(x)\n"
        "  if x > 0 then\n"
        "    return 1\n"
        "  elseif x < 0 then\n"
        "    return -1\n"
        "  else\n"
        "    return 0\n"
        "  end\n"
        "end\n";
    ccw_swaff_report report;
    char *error = NULL;
    ccw_ir *ir = ccw_swaff_lower(
        ccw_swaff_frontend_lua(), source, strlen(source), "lua", profile,
        CCW_SWAFF_REJECT_ON_ERROR, &report, &error);

    CCW_CHECK(ir != NULL, "Elseif lowering failed: %s", error ? error : "");
    free(error);
    if (ir == NULL) return;
    CCW_CHECK(report.unsupported_nodes == 0, "unexpected unsupported nodes");
    CCW_CHECK(report.functions_lowered >= 1, "expected at least one function");
    error = NULL;
    CCW_CHECK(ccw_ir_validate(ir, &error) == CCW_OK,
              "elseif IR did not validate: %s", error ? error : "");
    free(error);
    ccw_ir_module_destroy(ir);
}

static void check_top_level(ccw_profile profile)
{
    const char *source =
        "local x = 42\n"
        "print(x)\n"
        "function foo() return 1 end\n";
    ccw_swaff_report report;
    char *error = NULL;
    ccw_ir *ir = ccw_swaff_lower(
        ccw_swaff_frontend_lua(), source, strlen(source), "lua", profile,
        CCW_SWAFF_REJECT_ON_ERROR, &report, &error);

    CCW_CHECK(ir != NULL, "Top-level lowering failed: %s", error ? error : "");
    free(error);
    if (ir == NULL) return;
    /* Should have foo and @module */
    CCW_CHECK(ccw_ir_function_count(ir) >= 2, "expected at least two functions (foo + @module)");
    bool has_foo = false, has_module = false;
    for (int i = 0; i < ccw_ir_function_count(ir); i++) {
        ccw_node fn = ccw_ir_function_ref(ir, i);
        const char *name = ccw_ir_function_name(ir, fn);
        if (strcmp(name, "foo") == 0) has_foo = true;
        if (strcmp(name, "@module") == 0) has_module = true;
    }
    CCW_CHECK(has_foo, "missing function foo");
    CCW_CHECK(has_module, "missing @module top-level function");
    CCW_CHECK(report.unsupported_nodes == 0, "unexpected unsupported nodes");
    error = NULL;
    CCW_CHECK(ccw_ir_validate(ir, &error) == CCW_OK,
              "top-level IR did not validate: %s", error ? error : "");
    free(error);
    ccw_ir_module_destroy(ir);
}

static void check_break_statement(ccw_profile profile)
{
    const char *source =
        "function find_first(t, n)\n"
        "  for i = 1, n do\n"
        "    if i == 5 then\n"
        "      break\n"
        "    end\n"
        "  end\n"
        "  return 0\n"
        "end\n";
    ccw_swaff_report report;
    char *error = NULL;
    ccw_ir *ir = ccw_swaff_lower(
        ccw_swaff_frontend_lua(), source, strlen(source), "lua", profile,
        CCW_SWAFF_REJECT_ON_ERROR, &report, &error);

    CCW_CHECK(ir != NULL, "Break lowering failed: %s", error ? error : "");
    free(error);
    if (ir == NULL) return;
    CCW_CHECK(report.unsupported_nodes == 0, "unexpected unsupported nodes");
    error = NULL;
    CCW_CHECK(ccw_ir_validate(ir, &error) == CCW_OK,
              "break IR did not validate: %s", error ? error : "");
    free(error);
    ccw_ir_module_destroy(ir);
}

int main(void)
{
    CCW_CHECK(ccw_swaff_available(), "Tree-sitter support must be enabled");
    CCW_CHECK_STREQ(ccw_swaff_frontend_name(ccw_swaff_frontend_lua()), "lua");

    /* Test across both profiles */
    check_basic(CCW_PROFILE_TILLY);
    check_basic(CCW_PROFILE_ON1X);
    check_strings(CCW_PROFILE_ON1X);
    check_while_loop(CCW_PROFILE_ON1X);
    check_repeat_loop(CCW_PROFILE_ON1X);
    check_for_loop(CCW_PROFILE_ON1X);
    check_elseif(CCW_PROFILE_ON1X);
    check_top_level(CCW_PROFILE_ON1X);
    check_break_statement(CCW_PROFILE_ON1X);

    /* Malformed CST rejection */
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
