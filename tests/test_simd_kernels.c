#include "GlueSTD.h"
#include "ccw_host_accessors.h"
#include "ccw_ir.h"
#include "ccw_test.h"

#include <stdlib.h>
#include <string.h>

#ifndef CCW_KERNEL_DIR
#define CCW_KERNEL_DIR "kernels"
#endif

static int load_apply(ccw_executor *ex, const char *file, const char *cap,
                      ccw_ir *ir)
{
    char path[512], *error = NULL;
    snprintf(path, sizeof(path), "%s/%s", CCW_KERNEL_DIR, file);
    int id = ccw_kernel_load(ex, path, &error);
    CCW_CHECK(id >= 0, "SIMD kernel load failed: %s", error ? error : "");
    free(error);
    if (id < 0) return 0;
    error = NULL;
    ccw_status st = ccw_kernel_apply(ex, id, cap, ir, NULL, &error);
    CCW_CHECK(st == CCW_OK, "SIMD kernel apply failed: %s", error ? error : "");
    free(error);
    return st == CCW_OK;
}

int main(void)
{
    float a[4] = { 1, 2, 3, 4 }, b[4] = { 5, 6, 7, 8 }, out[4];
    ccw_v128 va = ccw_simde_loadu(a), vb = ccw_simde_loadu(b);
    ccw_simde_storeu(out, ccw_simde_add_f32x4(va, vb));
    CCW_CHECK(out[0] == 6 && out[3] == 12, "SIMDe f32 add mismatch");
    CCW_CHECK(ccw_simde_hreduce_add_f32x4(va) == 10.0f,
              "SIMDe horizontal reduction mismatch");

    ccw_executor *ex = ccw_executor_create();
    CCW_CHECK(ex != NULL, "executor creation failed");
    CCW_CHECK(ccw_host_register_core_accessors(ex) == CCW_OK,
              "core accessor registration failed");
    ccw_ir *ir = ccw_ir_module_create("simd", CCW_PROFILE_TILLY);
    ccw_node fn = ccw_ir_function_add(ir, "f", CCW_TY_F32);
    ccw_node block = ccw_ir_block_add(ir, fn, "loop");
    ccw_node br = ccw_ir_instr_build(ir, "br", CCW_TY_VOID);
    ccw_ir_instr_add_operand(ir, br, ccw_ir_operand_block(ir, "loop"));
    ccw_ir_block_append_instr(ir, block, br);
    CCW_CHECK(load_apply(ex, "vec-width.scm", "analysis.vector-width", ir),
              "vec-width failed");
    CCW_CHECK_STREQ(ccw_ir_attr_lookup(ir, fn,
                                       "analysis.analysis.vector-width.lanes-f32"),
                    "4");
    CCW_CHECK(load_apply(ex, "vec-legality.scm", "analysis.vectorizable", ir),
              "vec-legality failed");
    CCW_CHECK_STREQ(ccw_ir_attr_lookup(ir, block,
                                       "analysis.analysis.vectorizable.legal?"),
                    "true");
    ccw_ir_module_destroy(ir);
    ccw_executor_destroy(ex);
    return ccw_test_report("SIMD kernels");
}
