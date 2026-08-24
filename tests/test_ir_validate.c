/* §5.3: comprehensive validation tests for the strengthened IR validator.
 *
 * Covers:
 *   - duplicate function names
 *   - duplicate block names within a function
 *   - missing terminator
 *   - empty block (no instructions)
 *   - undefined block target in branch
 *   - unreachable block (non-entry with no predecessors)
 *   - duplicate parameter names
 *   - return type mismatch
 *   - profile cross-contamination (inherited from originals)
 *   - round-trip after validation */

#include "ccw_ir.h"
#include "tilly/ccw_tilly.h"
#include "on1x/ccw_on1x.h"
#include "ccw_test.h"

#include <stdlib.h>
#include <string.h>

/* ---------- helpers ---------- */

static ccw_node
make_fn(ccw_ir *m, const char *name)
{
    ccw_node fn = ccw_ir_function_add(m, name, CCW_TY_I64);
    ccw_ir_function_add_param(m, fn, CCW_TY_I64, "x");
    return fn;
}

static ccw_node
entry_block(ccw_ir *m, ccw_node fn)
{
    return ccw_ir_block_add(m, fn, "entry");
}

/* Append a ret of the first param (or 0) so the block is well-terminated. */
static void
terminate(ccw_ir *m, ccw_node blk, ccw_node fn)
{
    ccw_node val = ccw_ir_function_param_count(m, fn) > 0
        ? ccw_ir_operand_reg(m, ccw_ir_function_param_name(m, fn, 0))
        : ccw_ir_operand_const_int(m, CCW_TY_I64, 0);
    ccw_node ret = ccw_ir_instr_build(m, "ret", CCW_TY_VOID);
    ccw_ir_instr_add_operand(m, ret, val);
    ccw_ir_block_append_instr(m, blk, ret);
}

/* ---------- tests ---------- */

int main(void)
{
    char *err = NULL;

    /* --- duplicate function names --- */
    {
        ccw_ir *m = ccw_ir_module_create("dup-fn", CCW_PROFILE_TILLY);
        ccw_node f1 = make_fn(m, "f");
        ccw_node b1 = entry_block(m, f1);
        terminate(m, b1, f1);
        ccw_node f2 = ccw_ir_function_add(m, "f", CCW_TY_I64);
        ccw_node b2 = entry_block(m, f2);
        terminate(m, b2, f2);
        err = NULL;
        CCW_CHECK(ccw_ir_validate(m, &err) != CCW_OK,
                  "duplicate function names must be rejected");
        CCW_CHECK(err != NULL, "rejection must provide an error message");
        free(err);
        ccw_ir_module_destroy(m);
    }

    /* --- duplicate block names within a function --- */
    {
        ccw_ir *m = ccw_ir_module_create("dup-blk", CCW_PROFILE_TILLY);
        ccw_node fn = make_fn(m, "f");
        ccw_node b1 = entry_block(m, fn);
        terminate(m, b1, fn);
        ccw_node b2 = ccw_ir_block_add(m, fn, "entry");
        terminate(m, b2, fn);
        err = NULL;
        CCW_CHECK(ccw_ir_validate(m, &err) != CCW_OK,
                  "duplicate block names must be rejected");
        CCW_CHECK(err != NULL, "rejection must provide an error message");
        free(err);
        ccw_ir_module_destroy(m);
    }

    /* --- missing terminator (block ends with a non-terminator) --- */
    {
        ccw_ir *m = ccw_ir_module_create("no-term", CCW_PROFILE_TILLY);
        ccw_node fn = make_fn(m, "f");
        ccw_node blk = entry_block(m, fn);
        ccw_node add = ccw_ir_instr_build(m, "iadd", CCW_TY_I64);
        ccw_ir_instr_set_dest(m, add, "t0");
        ccw_ir_instr_add_operand(m, add, ccw_ir_operand_reg(m, "x"));
        ccw_ir_instr_add_operand(m, add, ccw_ir_operand_const_int(m, CCW_TY_I64, 1));
        ccw_ir_block_append_instr(m, blk, add);
        /* No terminator appended */
        err = NULL;
        CCW_CHECK(ccw_ir_validate(m, &err) != CCW_OK,
                  "block without terminator must be rejected");
        CCW_CHECK(err != NULL, "rejection must provide an error message");
        free(err);
        ccw_ir_module_destroy(m);
    }

    /* --- empty block --- */
    {
        ccw_ir *m = ccw_ir_module_create("empty-blk", CCW_PROFILE_TILLY);
        ccw_node fn = make_fn(m, "f");
        (void)entry_block(m, fn);
        /* No instructions at all */
        err = NULL;
        CCW_CHECK(ccw_ir_validate(m, &err) != CCW_OK,
                  "empty block must be rejected");
        free(err);
        ccw_ir_module_destroy(m);
    }

    /* --- undefined block target in branch --- */
    {
        ccw_ir *m = ccw_ir_module_create("bad-target", CCW_PROFILE_TILLY);
        ccw_node fn = make_fn(m, "f");
        ccw_node blk = entry_block(m, fn);
        ccw_node br = ccw_ir_instr_build(m, "br", CCW_TY_VOID);
        ccw_ir_instr_add_operand(m, br, ccw_ir_operand_block(m, "nonexistent"));
        ccw_ir_block_append_instr(m, blk, br);
        err = NULL;
        CCW_CHECK(ccw_ir_validate(m, &err) != CCW_OK,
                  "branch to undefined block must be rejected");
        CCW_CHECK(err != NULL, "rejection must provide an error message");
        free(err);
        ccw_ir_module_destroy(m);
    }

    /* --- unreachable block (non-entry with no predecessors) --- */
    {
        ccw_ir *m = ccw_ir_module_create("unreachable", CCW_PROFILE_TILLY);
        ccw_node fn = make_fn(m, "f");
        ccw_node b1 = entry_block(m, fn);
        terminate(m, b1, fn);
        /* Second block added but nothing branches to it */
        ccw_node b2 = ccw_ir_block_add(m, fn, "dead");
        terminate(m, b2, fn);
        err = NULL;
        CCW_CHECK(ccw_ir_validate(m, &err) != CCW_OK,
                  "unreachable block must be rejected");
        CCW_CHECK(err != NULL, "rejection must provide an error message");
        free(err);
        ccw_ir_module_destroy(m);
    }

    /* --- duplicate parameter names --- */
    {
        ccw_ir *m = ccw_ir_module_create("dup-param", CCW_PROFILE_TILLY);
        ccw_node fn = ccw_ir_function_add(m, "f", CCW_TY_I64);
        ccw_ir_function_add_param(m, fn, CCW_TY_I64, "x");
        ccw_ir_function_add_param(m, fn, CCW_TY_I64, "x");
        ccw_node blk = entry_block(m, fn);
        terminate(m, blk, fn);
        err = NULL;
        CCW_CHECK(ccw_ir_validate(m, &err) != CCW_OK,
                  "duplicate parameter names must be rejected");
        CCW_CHECK(err != NULL, "rejection must provide an error message");
        free(err);
        ccw_ir_module_destroy(m);
    }

    /* --- return type mismatch --- */
    {
        ccw_ir *m = ccw_ir_module_create("ret-type", CCW_PROFILE_TILLY);
        ccw_node fn = ccw_ir_function_add(m, "f", CCW_TY_I64);
        ccw_node blk = ccw_ir_block_add(m, fn, "entry");
        ccw_node ret = ccw_ir_instr_build(m, "ret", CCW_TY_VOID);
        /* Return a float in a function that returns i64 */
        ccw_ir_instr_add_operand(m, ret, ccw_ir_operand_const_float(m, CCW_TY_F64, 3.14));
        ccw_ir_block_append_instr(m, blk, ret);
        err = NULL;
        CCW_CHECK(ccw_ir_validate(m, &err) != CCW_OK,
                  "return type mismatch must be rejected");
        CCW_CHECK(err != NULL, "rejection must provide an error message");
        free(err);
        ccw_ir_module_destroy(m);
    }

    /* --- profile cross-contamination: Tilly + On1x opcode --- */
    {
        ccw_ir *m = ccw_ir_module_create("cross-tilly", CCW_PROFILE_TILLY);
        ccw_node fn = ccw_ir_function_add(m, "f", CCW_TY_I64);
        ccw_node blk = ccw_ir_block_add(m, fn, "entry");
        ccw_on1x_build_call_dynamic(m, blk, "d", CCW_TY_I64, "recv", "sel", 2);
        terminate(m, blk, fn);
        err = NULL;
        CCW_CHECK(ccw_ir_validate(m, &err) != CCW_OK,
                  "On1x opcode in Tilly module must be rejected");
        free(err);
        ccw_ir_module_destroy(m);
    }

    /* --- core constructs validate successfully --- */
    {
        ccw_ir *m = ccw_ir_module_create("core-ok", CCW_PROFILE_TILLY);
        ccw_node fn = make_fn(m, "f");
        ccw_node blk = entry_block(m, fn);
        ccw_node add = ccw_ir_instr_build(m, "iadd", CCW_TY_I64);
        ccw_ir_instr_set_dest(m, add, "t0");
        ccw_ir_instr_add_operand(m, add, ccw_ir_operand_reg(m, "x"));
        ccw_ir_instr_add_operand(m, add, ccw_ir_operand_const_int(m, CCW_TY_I64, 1));
        ccw_ir_block_append_instr(m, blk, add);
        terminate(m, blk, fn);
        err = NULL;
        CCW_CHECK(ccw_ir_validate(m, &err) == CCW_OK,
                  "core constructs with proper terminator must validate: %s",
                  err ? err : "");
        free(err);
        ccw_ir_module_destroy(m);
    }

    /* --- ccw_ir_instr_is_terminator smoke test --- */
    {
        ccw_ir *m = ccw_ir_module_create("terminator-test", CCW_PROFILE_TILLY);
        ccw_node fn = make_fn(m, "f");
        ccw_node blk = entry_block(m, fn);
        ccw_node add = ccw_ir_instr_build(m, "iadd", CCW_TY_I64);
        ccw_ir_instr_set_dest(m, add, "t0");
        ccw_ir_instr_add_operand(m, add, ccw_ir_operand_reg(m, "x"));
        ccw_ir_instr_add_operand(m, add, ccw_ir_operand_const_int(m, CCW_TY_I64, 1));
        ccw_ir_block_append_instr(m, blk, add);
        CCW_CHECK(!ccw_ir_instr_is_terminator(m, add),
                  "iadd must not be recognised as a terminator");
        ccw_node ret = ccw_ir_instr_build(m, "ret", CCW_TY_VOID);
        ccw_ir_instr_add_operand(m, ret, ccw_ir_operand_reg(m, "x"));
        ccw_ir_block_append_instr(m, blk, ret);
        CCW_CHECK(ccw_ir_instr_is_terminator(m, ret),
                  "ret must be recognised as a terminator");
        ccw_ir_module_destroy(m);
    }

    /* --- validation round-trip: validate then print then parse then validate --- */
    {
        ccw_ir *m = ccw_ir_module_create("validate-rt", CCW_PROFILE_TILLY);
        ccw_node fn = make_fn(m, "f");
        ccw_node b1 = entry_block(m, fn);
        ccw_node add = ccw_ir_instr_build(m, "iadd", CCW_TY_I64);
        ccw_ir_instr_set_dest(m, add, "t0");
        ccw_ir_instr_add_operand(m, add, ccw_ir_operand_reg(m, "x"));
        ccw_ir_instr_add_operand(m, add, ccw_ir_operand_const_int(m, CCW_TY_I64, 1));
        ccw_ir_block_append_instr(m, b1, add);
        terminate(m, b1, fn);
        err = NULL;
        CCW_CHECK(ccw_ir_validate(m, &err) == CCW_OK,
                  "valid module must validate: %s", err ? err : "");
        free(err);
        char *text = ccw_ir_print(m);
        CCW_CHECK(text != NULL, "printing must succeed");
        err = NULL;
        ccw_ir *back = ccw_ir_parse(text, &err);
        CCW_CHECK(back != NULL, "parsing must succeed: %s", err ? err : "");
        free(err);
        free(text);
        err = NULL;
        CCW_CHECK(ccw_ir_validate(back, &err) == CCW_OK,
                  "round-tripped module must validate: %s", err ? err : "");
        free(err);
        ccw_ir_module_destroy(back);
        ccw_ir_module_destroy(m);
    }

    return ccw_test_report("ir-validate");
}
