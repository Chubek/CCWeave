/* C frontend conformance: use the vendored Tree-sitter runtime and C
 * grammar, lower non-trivial CSTs through Kliche, and handle malformed
 * nodes according to the selected policy. */

#include "ccw_swaff.h"
#include "ccw_test.h"

#include <stdlib.h>

static bool block_has_opcode(const ccw_ir *ir, ccw_node block,
                             const char *opcode)
{
    int count = ccw_ir_block_instr_count(ir, block);
    for (int i = 0; i < count; i++) {
        ccw_node ins = ccw_ir_block_instr_ref(ir, block, i);
        const char *actual = ccw_ir_instr_opcode(ir, ins);
        if (actual != NULL && strcmp(actual, opcode) == 0) return true;
    }
    return false;
}

static bool function_has_opcode(const ccw_ir *ir, ccw_node fn,
                                const char *opcode)
{
    int count = ccw_ir_function_block_count(ir, fn);
    for (int i = 0; i < count; i++)
        if (block_has_opcode(ir, ccw_ir_function_block_ref(ir, fn, i), opcode))
            return true;
    return false;
}

static void check_nontrivial_lowering(ccw_profile profile, const char *label)
{
    const char *source =
        "#include <stdint.h>\n"
        "int helper(int x) { return x * 2; }\n"
        "int compute(int a, int b) {\n"
        "  int sum = a + b;\n"
        "  if (sum > 10) {\n"
        "    sum = sum - 1;\n"
        "  } else {\n"
        "    sum += 1;\n"
        "  }\n"
        "  return helper(sum);\n"
        "}\n";

    ccw_swaff_report report;
    char *error = NULL;
    ccw_ir *ir = ccw_swaff_lower(
        ccw_swaff_frontend_c(), source, strlen(source), label, profile,
        CCW_SWAFF_REJECT_ON_ERROR, &report, &error);

    CCW_CHECK(ir != NULL, "%s C lowering failed: %s", label,
              error ? error : "(no message)");
    free(error);
    if (ir == NULL) return;

    CCW_CHECK(report.error_nodes == 0 && report.missing_nodes == 0,
              "%s: valid C produced malformed CST nodes", label);
    CCW_CHECK(report.functions_lowered == 2,
              "%s: expected two functions, got %d", label,
              report.functions_lowered);
    CCW_CHECK(report.declarations_lowered == 1,
              "%s: expected one local declaration, got %d", label,
              report.declarations_lowered);
    CCW_CHECK(report.unsupported_nodes == 0,
              "%s: supported input left %d unsupported nodes", label,
              report.unsupported_nodes);
    CCW_CHECK(ccw_ir_function_count(ir) == 2,
              "%s: expected two IR functions", label);

    ccw_node helper = ccw_ir_function_ref(ir, 0);
    ccw_node compute = ccw_ir_function_ref(ir, 1);
    CCW_CHECK_STREQ(ccw_ir_function_name(ir, helper), "helper");
    CCW_CHECK_STREQ(ccw_ir_function_name(ir, compute), "compute");
    CCW_CHECK(ccw_ir_function_param_count(ir, helper) == 1,
              "%s: helper parameter was not lowered", label);
    CCW_CHECK(ccw_ir_function_param_count(ir, compute) == 2,
              "%s: compute parameters were not lowered", label);
    CCW_CHECK(ccw_ir_function_block_count(ir, compute) == 4,
              "%s: if/else must lower to then, else, and merge blocks", label);

    CCW_CHECK(function_has_opcode(ir, helper, "imul"),
              "%s: multiplication expression was not lowered", label);
    CCW_CHECK(function_has_opcode(ir, compute, "local.alloc"),
              "%s: local declaration was not lowered", label);
    CCW_CHECK(function_has_opcode(ir, compute, "local.store"),
              "%s: assignment was not lowered", label);
    CCW_CHECK(function_has_opcode(ir, compute, "iadd"),
              "%s: addition was not lowered", label);
    CCW_CHECK(function_has_opcode(ir, compute, "isub"),
              "%s: subtraction was not lowered", label);
    CCW_CHECK(function_has_opcode(ir, compute, "icmp.gt"),
              "%s: comparison was not lowered", label);
    CCW_CHECK(function_has_opcode(ir, compute, "br.cond"),
              "%s: conditional branch was not lowered", label);
    CCW_CHECK(function_has_opcode(ir, compute, "call"),
              "%s: direct call was not lowered", label);
    CCW_CHECK(function_has_opcode(ir, compute, "ret"),
              "%s: return was not lowered", label);

    error = NULL;
    CCW_CHECK(ccw_ir_validate(ir, &error) == CCW_OK,
              "%s: lowered module did not validate: %s", label,
              error ? error : "(no message)");
    free(error);

    char *text = ccw_ir_print(ir);
    error = NULL;
    ccw_ir *roundtrip = ccw_ir_parse(text, &error);
    CCW_CHECK(roundtrip != NULL && ccw_ir_equal(ir, roundtrip),
              "%s: lowered module did not round-trip: %s", label,
              error ? error : "(no message)");
    free(error);
    free(text);
    ccw_ir_module_destroy(roundtrip);
    ccw_ir_module_destroy(ir);
}

static void check_array_expressions(void)
{
    const char *source =
        "int arrays(void) {\n"
        "  int a[2][2] = {{1, 2}, {3, 4}};\n"
        "  a[1][0] = a[0][1] + 5;\n"
        "  return a[1][0];\n"
        "}\n";
    ccw_swaff_report report;
    char *error = NULL;
    ccw_ir *ir = ccw_swaff_lower(
        ccw_swaff_frontend_c(), source, strlen(source), "arrays",
        CCW_PROFILE_TILLY, CCW_SWAFF_REJECT_ON_ERROR, &report, &error);
    CCW_CHECK(ir != NULL, "array expressions failed: %s",
              error ? error : "(no message)");
    free(error);
    if (!ir) return;
    ccw_node fn = ccw_ir_function_ref(ir, 0);
    CCW_CHECK(function_has_opcode(ir, fn, "array.alloc"),
              "array declaration was not lowered");
    CCW_CHECK(function_has_opcode(ir, fn, "array.load"),
              "array read was not lowered");
    CCW_CHECK(function_has_opcode(ir, fn, "array.store"),
              "array write was not lowered");
    CCW_CHECK(report.unsupported_nodes == 0,
              "array expressions left %d unsupported nodes",
              report.unsupported_nodes);
    ccw_ir_module_destroy(ir);
}

static void check_control_statements(void)
{
    const char *source =
        "int control(int n) {\n"
        "  int i = 0;\n"
        "  for (i = 0; i < n; i++) { if (i == 2) continue; }\n"
        "  while (i < n + 2) { i++; if (i > n) break; }\n"
        "  do { i--; } while (i > n);\n"
        "  switch (i) { case 0: i = 1; break; default: i = 2; }\n"
        "  label: i = i + 1;\n"
        "  return i;\n"
        "}\n";
    ccw_swaff_report report;
    char *error = NULL;
    ccw_ir *ir = ccw_swaff_lower(
        ccw_swaff_frontend_c(), source, strlen(source), "control",
        CCW_PROFILE_TILLY, CCW_SWAFF_REJECT_ON_ERROR, &report, &error);
    CCW_CHECK(ir != NULL, "control statements failed: %s",
              error ? error : "(no message)");
    free(error);
    if (!ir) return;
    ccw_node fn = ccw_ir_function_ref(ir, 0);
    CCW_CHECK(function_has_opcode(ir, fn, "br.cond"),
              "control statements did not lower conditional branches");
    CCW_CHECK(report.unsupported_nodes == 0,
              "control statements left %d unsupported nodes",
              report.unsupported_nodes);
    ccw_ir_module_destroy(ir);
}

int main(void)
{
    CCW_CHECK(ccw_swaff_available(),
              "vendored Tree-sitter C frontend must be enabled by default");
    CCW_CHECK_STREQ(ccw_swaff_frontend_name(ccw_swaff_frontend_c()), "c");

    check_nontrivial_lowering(CCW_PROFILE_TILLY, "c-tilly");
    check_nontrivial_lowering(CCW_PROFILE_ON1X, "c-on1x");
    check_array_expressions();
    check_control_statements();

    const char *bad = "int broken(int x { return x + ; }\n";
    ccw_swaff_report report;
    char *error = NULL;
    ccw_ir *rejected = ccw_swaff_lower(
        ccw_swaff_frontend_c(), bad, strlen(bad), "bad",
        CCW_PROFILE_TILLY, CCW_SWAFF_REJECT_ON_ERROR, &report, &error);
    CCW_CHECK(rejected == NULL,
              "reject policy must refuse ERROR/MISSING nodes");
    CCW_CHECK(error != NULL && report.error_nodes + report.missing_nodes > 0,
              "rejection must report malformed CST nodes");
    free(error);

    error = NULL;
    ccw_ir *recovered = ccw_swaff_lower(
        ccw_swaff_frontend_c(), bad, strlen(bad), "recovered",
        CCW_PROFILE_TILLY, CCW_SWAFF_RECOVER_ON_ERROR, &report, &error);
    CCW_CHECK(recovered != NULL,
              "recover policy must return a module: %s",
              error ? error : "(no message)");
    CCW_CHECK(report.recovered_subtrees > 0,
              "recover policy must report skipped malformed subtrees");
    free(error);
    ccw_ir_module_destroy(recovered);

    return ccw_test_report("swaff-c");
}
