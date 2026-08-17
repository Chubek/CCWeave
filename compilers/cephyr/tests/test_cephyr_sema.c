#include "cephyr_sema.h"
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

int main(void)
{
    cephyr_type small = { .kind = CEPHYR_TY_SHORT };
    cephyr_type *promoted = cephyr_type_integer_promote(&small);
    CCW_CHECK(promoted != NULL && promoted->kind == CEPHYR_TY_INT,
              "small integer types must promote to int");
    cephyr_type_free(promoted);

    cephyr_type integer = { .kind = CEPHYR_TY_INT };
    cephyr_type floating = { .kind = CEPHYR_TY_DOUBLE };
    cephyr_type *common = cephyr_type_usual_arithmetic(&integer, &floating);
    CCW_CHECK(common != NULL && common->kind == CEPHYR_TY_DOUBLE,
              "integer and double operands must use double");
    cephyr_type_free(common);

    cephyr_type *array_element = type(CEPHYR_TY_INT);
    cephyr_type array = { .kind = CEPHYR_TY_ARRAY, .inner = array_element,
                          .array_size = 3 };
    CCW_CHECK(cephyr_type_size(&array) == 12,
              "three int elements must occupy twelve bytes");
    CCW_CHECK(cephyr_type_align(&array) == 4,
              "array alignment must follow its element size");
    cephyr_type_free(array_element);

    cephyr_ast_node *program = cephyr_ast_node_alloc(CEPHYR_NODE_PROGRAM);
    cephyr_ast_node *first = cephyr_ast_node_alloc(CEPHYR_NODE_VAR_DECL);
    cephyr_ast_node *second = cephyr_ast_node_alloc(CEPHYR_NODE_VAR_DECL);
    first->name = copy_string("duplicate");
    second->name = copy_string("duplicate");
    first->type = type(CEPHYR_TY_INT);
    second->type = type(CEPHYR_TY_INT);
    second->source_file = "sema-test.c";
    second->source_line = 7;
    second->source_column = 3;
    first->next = second;
    program->data.block.stmt_count = 1;
    program->data.block.stmts = calloc(1, sizeof(*program->data.block.stmts));
    program->data.block.stmts[0] = first;

    cephyr_sema_ctx *ctx = cephyr_sema_create();
    int errors = cephyr_sema_run(ctx, program);
    CCW_CHECK(errors == 1, "redefinition must produce one semantic error");
    CCW_CHECK(cephyr_sema_error_count(ctx) == 1,
              "semantic error count must match the returned count");
    const cephyr_diagnostic *diag = cephyr_sema_diagnostic_ref(ctx, 0);
    CCW_CHECK(diag != NULL && strcmp(diag->id, CEPHYR_E0004) == 0,
              "redefinition must use stable diagnostic CE0004");
    CCW_CHECK(diag != NULL && diag->source_line == 7 &&
                  diag->source_column == 3,
              "diagnostics must preserve source locations");

    cephyr_sema_destroy(ctx);
    cephyr_ast_free_recursive(first);
    free(program->data.block.stmts);
    free(program);

    return ccw_test_report("cephyr-sema");
}
