#include "cephyr_lower.h"
#include "ccw_test.h"

#include <stdlib.h>
#include <string.h>

static cephyr_type *type(cephyr_type_kind kind)
{
    cephyr_type *out = calloc(1, sizeof(*out));
    out->kind = kind;
    return out;
}

static char *copy_string(const char *s)
{
    size_t n = strlen(s) + 1u;
    char *out = malloc(n);
    if (out != NULL) memcpy(out, s, n);
    return out;
}

static bool function_has_opcode(const ccw_ir *ir, ccw_node fn,
                                const char *opcode)
{
    int blocks = ccw_ir_function_block_count(ir, fn);
    for (int bi = 0; bi < blocks; bi++) {
        ccw_node block = ccw_ir_function_block_ref(ir, fn, bi);
        int instructions = ccw_ir_block_instr_count(ir, block);
        for (int ii = 0; ii < instructions; ii++) {
            ccw_node instruction = ccw_ir_block_instr_ref(ir, block, ii);
            if (strcmp(ccw_ir_instr_opcode(ir, instruction), opcode) == 0)
                return true;
        }
    }
    return false;
}

int main(void)
{
    cephyr_ast_node *program = cephyr_ast_node_alloc(CEPHYR_NODE_PROGRAM);
    cephyr_ast_node *function = cephyr_ast_node_alloc(CEPHYR_NODE_FUNC_DEF);
    cephyr_ast_node *body = cephyr_ast_node_alloc(CEPHYR_NODE_BLOCK);
    cephyr_ast_node *ret = cephyr_ast_node_alloc(CEPHYR_NODE_STMT_RETURN);
    cephyr_ast_node *add = cephyr_ast_node_alloc(CEPHYR_NODE_EXPR_BINARY);
    cephyr_ast_node *lhs = cephyr_ast_node_alloc(CEPHYR_NODE_EXPR_INT_CONST);
    cephyr_ast_node *rhs = cephyr_ast_node_alloc(CEPHYR_NODE_EXPR_INT_CONST);

    function->name = copy_string("sum");
    function->type = type(CEPHYR_TY_FUNC);
    function->type->return_type = type(CEPHYR_TY_INT);
    function->type->param_count = 1;
    function->type->param_types = calloc(1, sizeof(*function->type->param_types));
    function->type->param_names = calloc(1, sizeof(*function->type->param_names));
    function->type->param_types[0] = type(CEPHYR_TY_INT);
    function->type->param_names[0] = copy_string("value");

    add->type = type(CEPHYR_TY_INT);
    add->data.binop.op = "+";
    lhs->type = type(CEPHYR_TY_INT);
    lhs->data.int_value = 2;
    rhs->type = type(CEPHYR_TY_INT);
    rhs->data.int_value = 3;
    add->data.binop.lhs = lhs;
    add->data.binop.rhs = rhs;
    ret->data.unop.operand = add;
    body->data.block.stmt_count = 1;
    body->data.block.stmts = calloc(1, sizeof(*body->data.block.stmts));
    body->data.block.stmts[0] = ret;
    function->data.func_def.body = body;
    program->data.block.stmt_count = 1;
    program->data.block.stmts = calloc(1, sizeof(*program->data.block.stmts));
    program->data.block.stmts[0] = function;

    cephyr_lower_ctx *ctx = cephyr_lower_create();
    char *error = NULL;
    ccw_ir *ir = cephyr_lower_program(ctx, program, "lower-test", &error);
    CCW_CHECK(ir != NULL, "typed AST lowering failed: %s",
              error ? error : "(no message)");
    free(error);

    if (ir != NULL) {
        CCW_CHECK(ccw_ir_module_profile(ir) == CCW_PROFILE_TILLY,
                  "Cephyr lowering must always produce Tilly IR");
        CCW_CHECK(ccw_ir_function_count(ir) == 1,
                  "lowering must emit one function");
        ccw_node fn = ccw_ir_function_ref(ir, 0);
        CCW_CHECK_STREQ(ccw_ir_function_name(ir, fn), "sum");
        CCW_CHECK(ccw_ir_function_param_count(ir, fn) == 1,
                  "function parameters must survive lowering");
        CCW_CHECK(function_has_opcode(ir, fn, "iconst"),
                  "integer literals must lower to iconst");
        CCW_CHECK(function_has_opcode(ir, fn, "iadd"),
                  "binary addition must lower to iadd");
        CCW_CHECK(function_has_opcode(ir, fn, "ret"),
                  "returns must lower to ret");

        error = NULL;
        CCW_CHECK(ccw_ir_validate(ir, &error) == CCW_OK,
                  "lowered IR must validate: %s", error ? error : "(no message)");
        free(error);

        char *text = ccw_ir_print(ir);
        error = NULL;
        ccw_ir *roundtrip = ccw_ir_parse(text, &error);
        CCW_CHECK(roundtrip != NULL && ccw_ir_equal(ir, roundtrip),
                  "lowered IR must round-trip: %s",
                  error ? error : "(no message)");
        free(error);
        free(text);
        ccw_ir_module_destroy(roundtrip);
        ccw_ir_module_destroy(ir);
    }

    cephyr_lower_destroy(ctx);
    cephyr_ast_free_recursive(function);
    free(program->data.block.stmts);
    free(program);

    return ccw_test_report("cephyr-lower");
}
