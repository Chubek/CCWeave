/* Kliche — paradigm stereotypes (§6.1).
 *
 * A stereotype is a documented mapping from paradigm concepts to core-IR
 * construction calls.  Each stereotype is a library of functions that
 * lower paradigm-specific constructs (closures, loops, vtables, etc.) into
 * canonical Weave IR instructions.  Stereotypes MUST NOT require a specific
 * profile; everything here is built from the core subset.  Nothing in
 * Kliche sees Tree-sitter types.
 *
 * Three stereotypes are provided:
 *
 *   functional — closures, algebraic data types (tagged unions),
 *                pattern-matching dispatch, partial application
 *   imperative — mutable locals, structured control flow (branches,
 *                loops), arithmetic, calls, arrays, type casts, phi nodes
 *   oop        — object allocation, vtable layout and dispatch, field
 *                access, inheritance (super calls), interface dispatch,
 *                instanceof/dynamic cast, exception frames
 *
 * Every function returns 0 on failure (null ir, null blk, null required
 * arguments, out-of-range indices, etc.).  Callers should check the
 * return value. */

#ifndef CCW_KLICHE_H
#define CCW_KLICHE_H

#include "ccw_ir.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

  /* =================================================================
   * FUNCTIONAL STEREOTYPE (§6.1)
   *
   * Closures are records: (closure-alloc @code, captured...) followed by
   * indexed field reads, and applied with an indirect call.
   *
   * Algebraic data types are tagged records: (record.alloc tag, fields...)
   * followed by tag extraction and field access.  Pattern matching lowers
   * to a chain of conditional branches on the tag. */

  /* --- closures --- */

  ccw_node ccw_kliche_closure_alloc (ccw_ir *ir, ccw_node blk,
                                     const char *dest, const char *code_symbol,
                                     int captured_count);
  ccw_node ccw_kliche_closure_capture (ccw_ir *ir, ccw_node blk,
                                       const char *closure_reg, int slot,
                                       const char *value_reg);
  ccw_node ccw_kliche_closure_ref (ccw_ir *ir, ccw_node blk, const char *dest,
                                   const char *closure_reg, int slot);
  ccw_node ccw_kliche_closure_apply (ccw_ir *ir, ccw_node blk,
                                     const char *dest, const char *closure_reg,
                                     const char *arg_reg);

  /* --- algebraic data types (tagged records) --- */

  ccw_node ccw_kliche_record_alloc (ccw_ir *ir, ccw_node blk,
                                    const char *dest, int64_t tag,
                                    int field_count);
  ccw_node ccw_kliche_record_tag (ccw_ir *ir, ccw_node blk, const char *dest,
                                  const char *record_reg);
  ccw_node ccw_kliche_record_field_get (ccw_ir *ir, ccw_node blk,
                                        const char *dest,
                                        const char *record_reg, int field_idx,
                                        ccw_ir_type type);
  ccw_node ccw_kliche_record_field_set (ccw_ir *ir, ccw_node blk,
                                        const char *record_reg, int field_idx,
                                        const char *value_reg);

  /* --- pattern-matching dispatch --- */

  ccw_node ccw_kliche_tag_switch (ccw_ir *ir, ccw_node blk,
                                  const char *tag_reg,
                                  const int64_t *case_tags,
                                  const char *const *case_blocks,
                                  int case_count, const char *default_block);

  /* =================================================================
   * IMPERATIVE STEREOTYPE (§6.1)
   *
   * Mutable locals as slots, structured control flow (branches, loops),
   * arithmetic, direct and indirect calls, returns, type casts, SSA phi
   * nodes, and array operations. */

  /* --- locals --- */

  ccw_node ccw_kliche_local_alloc (ccw_ir *ir, ccw_node blk, const char *dest,
                                   ccw_ir_type type);
  ccw_node ccw_kliche_local_store (ccw_ir *ir, ccw_node blk,
                                   const char *slot_reg,
                                   const char *value_reg);
  ccw_node ccw_kliche_local_load (ccw_ir *ir, ccw_node blk, const char *dest,
                                  const char *slot_reg, ccw_ir_type type);

  /* --- control flow --- */

  ccw_node ccw_kliche_branch_if (ccw_ir *ir, ccw_node blk,
                                 const char *cond_reg, const char *then_block,
                                 const char *else_block);
  ccw_node ccw_kliche_jump (ccw_ir *ir, ccw_node blk,
                            const char *target_block);

  /* --- constants --- */

  ccw_node ccw_kliche_int_const (ccw_ir *ir, ccw_node blk, const char *dest,
                                 int64_t value);
  ccw_node ccw_kliche_float_const (ccw_ir *ir, ccw_node blk, const char *dest,
                                   double value);

  /* --- arithmetic --- */

  ccw_node ccw_kliche_unary (ccw_ir *ir, ccw_node blk, const char *opcode,
                             const char *dest, const char *operand_reg,
                             ccw_ir_type type);
  ccw_node ccw_kliche_binary (ccw_ir *ir, ccw_node blk, const char *opcode,
                              const char *dest, const char *left_reg,
                              const char *right_reg, ccw_ir_type type);

  /* --- calls --- */

  ccw_node ccw_kliche_call (ccw_ir *ir, ccw_node blk, const char *dest,
                            const char *callee, const char *const *arg_regs,
                            int arg_count, ccw_ir_type result_type);

  /* --- return --- */

  ccw_node ccw_kliche_return (ccw_ir *ir, ccw_node blk, const char *value_reg);

  /* --- loops --- */

  ccw_node ccw_kliche_loop (ccw_ir *ir, ccw_node blk, const char *cond_reg,
                            const char *header_block, const char *body_block,
                            const char *exit_block);

  /* --- type casts --- */

  ccw_node ccw_kliche_cast (ccw_ir *ir, ccw_node blk, const char *dest,
                            const char *src_reg, ccw_ir_type src_type,
                            ccw_ir_type dst_type);

  /* --- phi nodes --- */

  ccw_node ccw_kliche_phi (ccw_ir *ir, ccw_node blk, const char *dest,
                           const char *const *values,
                           const char *const *blocks, int count,
                           ccw_ir_type type);

  /* --- arrays --- */

  ccw_node ccw_kliche_array_alloc (ccw_ir *ir, ccw_node blk, const char *dest,
                                   int64_t element_count,
                                   ccw_ir_type element_type);
  ccw_node ccw_kliche_array_load (ccw_ir *ir, ccw_node blk, const char *dest,
                                  const char *array_reg, const char *index_reg,
                                  ccw_ir_type element_type);
  ccw_node ccw_kliche_array_store (ccw_ir *ir, ccw_node blk,
                                   const char *array_reg, const char *index_reg,
                                   const char *value_reg,
                                   ccw_ir_type element_type);

  /* =================================================================
   * OOP STEREOTYPE (§6.1)
   *
   * Object headers, vtable layout and dispatch, field access,
   * inheritance (super calls), interface dispatch, type checking
   * (instanceof, dynamic cast), and exception frames.  Dispatch here is
   * the profile-agnostic vtable form; On1x refines it with inline caches. */

  /* --- object allocation --- */

  ccw_node ccw_kliche_object_alloc (ccw_ir *ir, ccw_node blk, const char *dest,
                                    const char *class_symbol, int field_count);
  ccw_node ccw_kliche_new (ccw_ir *ir, ccw_node blk, const char *dest,
                           const char *class_symbol,
                           const char *const *field_values, int field_count);

  /* --- vtable operations --- */

  ccw_node ccw_kliche_vtable_load (ccw_ir *ir, ccw_node blk, const char *dest,
                                   const char *object_reg);
  ccw_node ccw_kliche_vtable_store (ccw_ir *ir, ccw_node blk,
                                    const char *object_reg,
                                    const char *vtable_reg);
  ccw_node ccw_kliche_vtable_build (ccw_ir *ir, ccw_node blk, const char *dest,
                                    const char *const *methods,
                                    int method_count);
  ccw_node ccw_kliche_vtable_dispatch (ccw_ir *ir, ccw_node blk,
                                       const char *dest,
                                       const char *vtable_reg, int slot,
                                       const char *receiver_reg);

  /* --- field access --- */

  ccw_node ccw_kliche_field_get (ccw_ir *ir, ccw_node blk, const char *dest,
                                 const char *object_reg, int field_idx,
                                 ccw_ir_type type);
  ccw_node ccw_kliche_field_set (ccw_ir *ir, ccw_node blk,
                                 const char *object_reg, int field_idx,
                                 const char *value_reg);

  /* --- inheritance --- */

  ccw_node ccw_kliche_super_call (ccw_ir *ir, ccw_node blk, const char *dest,
                                  const char *subclass_vtable_reg, int slot,
                                  const char *receiver_reg);

  /* --- interface dispatch --- */

  ccw_node ccw_kliche_interface_dispatch (ccw_ir *ir, ccw_node blk,
                                          const char *dest,
                                          const char *object_reg,
                                          int64_t interface_id, int slot);

  /* --- type checking --- */

  ccw_node ccw_kliche_instanceof (ccw_ir *ir, ccw_node blk, const char *dest,
                                  const char *object_reg,
                                  const char *class_symbol);
  ccw_node ccw_kliche_dynamic_cast (ccw_ir *ir, ccw_node blk,
                                    const char *object_reg,
                                    const char *class_symbol,
                                    const char *success_block,
                                    const char *fail_block);

  /* --- exception frames --- */

  ccw_node ccw_kliche_frame_push (ccw_ir *ir, ccw_node blk,
                                  const char *handler_block);
  ccw_node ccw_kliche_frame_pop (ccw_ir *ir, ccw_node blk);
  ccw_node ccw_kliche_throw (ccw_ir *ir, ccw_node blk,
                             const char *exception_reg);
  ccw_node ccw_kliche_rethrow (ccw_ir *ir, ccw_node blk);

#ifdef __cplusplus
}
#endif
#endif /* CCW_KLICHE_H */
