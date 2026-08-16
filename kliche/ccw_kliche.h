/* Kliche — paradigm stereotypes (§6.1).
 *
 * A stereotype is a documented mapping from paradigm concepts to core-IR
 * construction calls. Stereotypes MUST NOT require a specific profile;
 * everything here is built from the core subset. Nothing in Kliche sees
 * Tree-sitter types. */

#ifndef CCW_KLICHE_H
#define CCW_KLICHE_H

#include "ccw_ir.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- functional stereotype ----------
 * Closures are records: (closure-alloc @code, captured...) followed by
 * indexed field reads, and applied with an indirect call. */

ccw_node ccw_kliche_closure_alloc(ccw_ir *ir, ccw_node blk, const char *dest,
                                  const char *code_symbol, int captured_count);
ccw_node ccw_kliche_closure_capture(ccw_ir *ir, ccw_node blk,
                                    const char *closure_reg, int slot,
                                    const char *value_reg);
ccw_node ccw_kliche_closure_ref(ccw_ir *ir, ccw_node blk, const char *dest,
                                const char *closure_reg, int slot);
ccw_node ccw_kliche_closure_apply(ccw_ir *ir, ccw_node blk, const char *dest,
                                  const char *closure_reg, const char *arg_reg);

/* ---------- imperative stereotype ----------
 * Mutable locals as slots, plus structured control flow lowered to
 * conditional and unconditional branches. */

ccw_node ccw_kliche_local_alloc(ccw_ir *ir, ccw_node blk, const char *dest,
                                ccw_ir_type type);
ccw_node ccw_kliche_local_store(ccw_ir *ir, ccw_node blk,
                                const char *slot_reg, const char *value_reg);
ccw_node ccw_kliche_local_load(ccw_ir *ir, ccw_node blk, const char *dest,
                               const char *slot_reg, ccw_ir_type type);
ccw_node ccw_kliche_branch_if(ccw_ir *ir, ccw_node blk, const char *cond_reg,
                              const char *then_block, const char *else_block);
ccw_node ccw_kliche_jump(ccw_ir *ir, ccw_node blk, const char *target_block);

/* ---------- oop stereotype ----------
 * Vtable layout, object headers, and exception frames. Dispatch here is
 * the profile-agnostic vtable form; On1x refines it with inline caches. */

ccw_node ccw_kliche_object_alloc(ccw_ir *ir, ccw_node blk, const char *dest,
                                 const char *class_symbol, int field_count);
ccw_node ccw_kliche_vtable_load(ccw_ir *ir, ccw_node blk, const char *dest,
                                const char *object_reg);
ccw_node ccw_kliche_vtable_dispatch(ccw_ir *ir, ccw_node blk, const char *dest,
                                    const char *vtable_reg, int slot,
                                    const char *receiver_reg);
ccw_node ccw_kliche_frame_push(ccw_ir *ir, ccw_node blk, const char *handler_block);
ccw_node ccw_kliche_frame_pop(ccw_ir *ir, ccw_node blk);

#ifdef __cplusplus
}
#endif
#endif /* CCW_KLICHE_H */
