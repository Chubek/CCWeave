/* OCaml frontend conformance: lower functional application, immutable
 * bindings, scalar expressions, and value-producing control flow from the
 * vendored implementation grammar under both Weave IR profiles. */

#include "ccw_swaff.h"
#include "ccw_test.h"

#include <stdlib.h>

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
        "let twice (x : int) = x * 2\n"
        "let choose flag x y = if flag then x + y else x - y\n"
        "let apply f x = f x\n"
        "let compute n = let doubled = twice n in doubled + 1\n"
        "let identity = fun x -> x\n";
    ccw_swaff_report report;
    char *error = NULL;
    ccw_ir *ir = ccw_swaff_lower(
        ccw_swaff_frontend_ocaml(), source, strlen(source), module_name,
        profile, CCW_SWAFF_REJECT_ON_ERROR, &report, &error);

    CCW_CHECK(ir != NULL, "%s: OCaml lowering failed: %s", module_name,
              error ? error : "(no message)");
    free(error);
    if (ir == NULL) return;

    CCW_CHECK(report.error_nodes == 0 && report.missing_nodes == 0,
              "%s: valid OCaml produced malformed CST nodes", module_name);
    CCW_CHECK(report.functions_lowered == 5,
              "%s: expected five functions, got %d", module_name,
              report.functions_lowered);
    CCW_CHECK(report.declarations_lowered == 1,
              "%s: expected one local let binding, got %d", module_name,
              report.declarations_lowered);
    CCW_CHECK(report.unsupported_nodes == 0,
              "%s: supported input left %d unsupported nodes", module_name,
              report.unsupported_nodes);
    CCW_CHECK(ccw_ir_function_count(ir) == 5,
              "%s: expected five IR functions", module_name);

    ccw_node twice = ccw_ir_function_ref(ir, 0);
    ccw_node choose = ccw_ir_function_ref(ir, 1);
    ccw_node apply = ccw_ir_function_ref(ir, 2);
    ccw_node compute = ccw_ir_function_ref(ir, 3);
    ccw_node identity = ccw_ir_function_ref(ir, 4);
    CCW_CHECK_STREQ(ccw_ir_function_name(ir, twice), "twice");
    CCW_CHECK_STREQ(ccw_ir_function_name(ir, choose), "choose");
    CCW_CHECK_STREQ(ccw_ir_function_name(ir, apply), "apply");
    CCW_CHECK_STREQ(ccw_ir_function_name(ir, compute), "compute");
    CCW_CHECK_STREQ(ccw_ir_function_name(ir, identity), "identity");
    CCW_CHECK(ccw_ir_function_param_count(ir, choose) == 3,
              "%s: choose parameters were not lowered", module_name);
    CCW_CHECK(ccw_ir_function_block_count(ir, choose) == 4,
              "%s: if expression must create then, else, and merge blocks",
              module_name);

    CCW_CHECK(function_has_opcode(ir, twice, "imul"),
              "%s: integer multiplication was not lowered", module_name);
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
              "%s: direct call or immutable local let was not lowered",
              module_name);
    CCW_CHECK(function_has_opcode(ir, identity, "ret"),
              "%s: fun expression was not normalized to a function",
              module_name);

    error = NULL;
    CCW_CHECK(ccw_ir_validate(ir, &error) == CCW_OK,
              "%s: lowered OCaml module did not validate: %s", module_name,
              error ? error : "(no message)");
    free(error);

    char *text = ccw_ir_print(ir);
    error = NULL;
    ccw_ir *roundtrip = ccw_ir_parse(text, &error);
    CCW_CHECK(roundtrip != NULL && ccw_ir_equal(ir, roundtrip),
              "%s: lowered OCaml module did not round-trip: %s", module_name,
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
        ccw_swaff_frontend_name(ccw_swaff_frontend_ocaml()), "ocaml");

    check_lowering(CCW_PROFILE_TILLY, "ocaml-tilly");
    check_lowering(CCW_PROFILE_ON1X, "ocaml-on1x");

    const char *bad = "let broken x = if x then else 1\n";
    ccw_swaff_report report;
    char *error = NULL;
    ccw_ir *rejected = ccw_swaff_lower(
        ccw_swaff_frontend_ocaml(), bad, strlen(bad), "bad",
        CCW_PROFILE_TILLY, CCW_SWAFF_REJECT_ON_ERROR, &report, &error);
    CCW_CHECK(rejected == NULL,
              "OCaml reject policy must refuse ERROR/MISSING nodes");
    CCW_CHECK(error != NULL && report.error_nodes + report.missing_nodes > 0,
              "OCaml rejection must report malformed CST nodes");
    free(error);

    error = NULL;
    ccw_ir *recovered = ccw_swaff_lower(
        ccw_swaff_frontend_ocaml(), bad, strlen(bad), "recovered",
        CCW_PROFILE_TILLY, CCW_SWAFF_RECOVER_ON_ERROR, &report, &error);
    CCW_CHECK(recovered != NULL,
              "OCaml recover policy must return a module: %s",
              error ? error : "(no message)");
    CCW_CHECK(report.recovered_subtrees > 0,
              "OCaml recover policy must report skipped malformed subtrees");
    free(error);
    ccw_ir_module_destroy(recovered);

    return ccw_test_report("swaff-ocaml");
}
