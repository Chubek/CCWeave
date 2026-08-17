/* Standard ML frontend conformance: curried functions, higher-order
 * application, immutable bindings, scalar expressions, control flow, and
 * explicit malformed-CST handling under both Weave IR profiles. */

#include "ccw_swaff.h"
#include "ccw_test.h"

#include <stdlib.h>
#include <string.h>

static bool function_has_opcode(const ccw_ir *ir, ccw_node function,
                                const char *opcode)
{
    int block_count = ccw_ir_function_block_count(ir, function);
    for (int bi = 0; bi < block_count; bi++) {
        ccw_node block = ccw_ir_function_block_ref(ir, function, bi);
        int instruction_count = ccw_ir_block_instr_count(ir, block);
        for (int ii = 0; ii < instruction_count; ii++) {
            ccw_node instruction = ccw_ir_block_instr_ref(ir, block, ii);
            const char *actual = ccw_ir_instr_opcode(ir, instruction);
            if (actual != NULL && strcmp(actual, opcode) == 0) return true;
        }
    }
    return false;
}

static void check_lowering(ccw_profile profile, const char *module_name)
{
    const char *source =
        "(* CST trivia is discarded by the adapter. *)\n"
        "fun twice (x : int) = x * 2\n"
        "fun choose flag x y = if flag then x + y else x - y\n"
        "fun apply f x = f x\n"
        "fun compute n = let val doubled = twice n in doubled + 1 end\n"
        "fun guarded x y = x andalso y\n"
        "val identity = fn x => x\n";
    ccw_swaff_report report;
    char *error = NULL;
    ccw_ir *ir = ccw_swaff_lower(
        ccw_swaff_frontend_sml(), source, strlen(source), module_name,
        profile, CCW_SWAFF_REJECT_ON_ERROR, &report, &error);

    CCW_CHECK(ir != NULL, "%s: SML lowering failed: %s", module_name,
              error ? error : "(no message)");
    free(error);
    if (ir == NULL) return;

    CCW_CHECK(report.error_nodes == 0 && report.missing_nodes == 0,
              "%s: valid SML produced malformed CST nodes", module_name);
    CCW_CHECK(report.functions_lowered == 6,
              "%s: expected six functions, got %d", module_name,
              report.functions_lowered);
    CCW_CHECK(report.declarations_lowered == 1,
              "%s: expected one local val binding, got %d", module_name,
              report.declarations_lowered);
    CCW_CHECK(report.unsupported_nodes == 0,
              "%s: supported input left %d unsupported nodes", module_name,
              report.unsupported_nodes);
    CCW_CHECK(ccw_ir_function_count(ir) == 6,
              "%s: expected six IR functions", module_name);

    ccw_node twice = ccw_ir_function_ref(ir, 0);
    ccw_node choose = ccw_ir_function_ref(ir, 1);
    ccw_node apply = ccw_ir_function_ref(ir, 2);
    ccw_node compute = ccw_ir_function_ref(ir, 3);
    ccw_node guarded = ccw_ir_function_ref(ir, 4);
    ccw_node identity = ccw_ir_function_ref(ir, 5);
    CCW_CHECK_STREQ(ccw_ir_function_name(ir, twice), "twice");
    CCW_CHECK_STREQ(ccw_ir_function_name(ir, choose), "choose");
    CCW_CHECK_STREQ(ccw_ir_function_name(ir, apply), "apply");
    CCW_CHECK_STREQ(ccw_ir_function_name(ir, compute), "compute");
    CCW_CHECK_STREQ(ccw_ir_function_name(ir, guarded), "guarded");
    CCW_CHECK_STREQ(ccw_ir_function_name(ir, identity), "identity");
    CCW_CHECK(ccw_ir_function_param_count(ir, choose) == 3,
              "%s: curried parameters were not lowered", module_name);
    CCW_CHECK(ccw_ir_function_block_count(ir, choose) == 4,
              "%s: if expression must create then, else, and merge blocks",
              module_name);

    CCW_CHECK(function_has_opcode(ir, twice, "imul"),
              "%s: integer multiplication was not normalized", module_name);
    CCW_CHECK(function_has_opcode(ir, choose, "iadd") &&
                  function_has_opcode(ir, choose, "isub"),
              "%s: if branches were not lowered", module_name);
    CCW_CHECK(function_has_opcode(ir, choose, "br.cond") &&
                  function_has_opcode(ir, choose, "local.load"),
              "%s: value-producing if was not merged", module_name);
    CCW_CHECK(function_has_opcode(ir, apply, "call.indirect"),
              "%s: higher-order application did not use functional Kliche",
              module_name);
    CCW_CHECK(function_has_opcode(ir, compute, "call") &&
                  function_has_opcode(ir, compute, "iadd"),
              "%s: direct call or immutable local val was not lowered",
              module_name);
    CCW_CHECK(function_has_opcode(ir, guarded, "br.cond") &&
                  ccw_ir_function_block_count(ir, guarded) == 4,
              "%s: andalso did not preserve short-circuit control flow",
              module_name);
    CCW_CHECK(function_has_opcode(ir, identity, "ret"),
              "%s: fn expression was not normalized to a function",
              module_name);

    error = NULL;
    CCW_CHECK(ccw_ir_validate(ir, &error) == CCW_OK,
              "%s: lowered SML module did not validate: %s", module_name,
              error ? error : "(no message)");
    free(error);

    char *text = ccw_ir_print(ir);
    error = NULL;
    ccw_ir *roundtrip = ccw_ir_parse(text, &error);
    CCW_CHECK(roundtrip != NULL && ccw_ir_equal(ir, roundtrip),
              "%s: lowered SML module did not round-trip: %s", module_name,
              error ? error : "(no message)");
    free(error);
    free(text);
    ccw_ir_module_destroy(roundtrip);
    ccw_ir_module_destroy(ir);
}

int main(void)
{
    CCW_CHECK(ccw_swaff_available(),
              "vendored Tree-sitter frontends must be enabled");
    CCW_CHECK_STREQ(
        ccw_swaff_frontend_name(ccw_swaff_frontend_sml()), "sml");

    check_lowering(CCW_PROFILE_TILLY, "sml-tilly");
    check_lowering(CCW_PROFILE_ON1X, "sml-on1x");

    const char *bad = "fun broken x = if x then else 1\n";
    ccw_swaff_report report;
    char *error = NULL;
    ccw_ir *rejected = ccw_swaff_lower(
        ccw_swaff_frontend_sml(), bad, strlen(bad), "bad",
        CCW_PROFILE_TILLY, CCW_SWAFF_REJECT_ON_ERROR, &report, &error);
    CCW_CHECK(rejected == NULL,
              "SML reject policy must refuse ERROR/MISSING nodes");
    CCW_CHECK(error != NULL && report.error_nodes + report.missing_nodes > 0,
              "SML rejection must report malformed CST nodes");
    free(error);

    error = NULL;
    ccw_ir *recovered = ccw_swaff_lower(
        ccw_swaff_frontend_sml(), bad, strlen(bad), "recovered",
        CCW_PROFILE_TILLY, CCW_SWAFF_RECOVER_ON_ERROR, &report, &error);
    CCW_CHECK(recovered != NULL,
              "SML recover policy must return a module: %s",
              error ? error : "(no message)");
    CCW_CHECK(report.recovered_subtrees > 0,
              "SML recover policy must report skipped malformed subtrees");
    free(error);
    ccw_ir_module_destroy(recovered);

    return ccw_test_report("swaff-sml");
}
