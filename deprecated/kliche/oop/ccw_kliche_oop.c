/* OOP stereotype: object headers, vtable layout and dispatch, and
 * exception frames.  Also field access, instanceof checks, super calls,
 * and interface dispatch.  Profile-agnostic: an On1x host may refine
 * vtable.dispatch into a dynamic dispatch site with inline caches (§6.1). */

#include "../ccw_kliche_common.h"

/* ---------- object allocation ---------- */

/* §6.1 oop: allocate an object with the given class and field count. */

ccw_node
ccw_kliche_object_alloc (ccw_ir *ir, ccw_node blk, const char *dest,
                         const char *class_symbol, int field_count)
{
  ccw_kliche_opnd ops[]
      = { CCW_K_FUNC (class_symbol), CCW_K_INT (field_count), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "object.alloc", CCW_TY_PTR, dest, ops);
}

/* §6.1 oop: allocate and construct an object in one step.
 * class_symbol names the class; field_values is a parallel array of
 * register names for the initial field values.  Returns the object pointer
 * in dest, or 0 on failure. */

ccw_node
ccw_kliche_new (ccw_ir *ir, ccw_node blk, const char *dest,
                const char *class_symbol, const char *const *field_values,
                int field_count)
{
  if (ir == NULL || blk == 0 || dest == NULL || class_symbol == NULL
      || field_count < 0)
    return 0;

  ccw_node obj = ccw_kliche_object_alloc (ir, blk, dest, class_symbol,
                                          field_count);
  if (obj == 0)
    return 0;

  for (int i = 0; i < field_count; i++)
    {
      if (field_values != NULL && field_values[i] != NULL)
        ccw_kliche_field_set (ir, blk, dest, i, field_values[i]);
    }
  return obj;
}

/* §6.1 oop: load the vtable pointer from an object header. */

ccw_node
ccw_kliche_vtable_load (ccw_ir *ir, ccw_node blk, const char *dest,
                        const char *object_reg)
{
  ccw_kliche_opnd ops[] = { CCW_K_REG (object_reg), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "vtable.load", CCW_TY_PTR, dest, ops);
}

/* §6.1 oop: store a vtable pointer into an object header.
 * Used during construction or when changing an object's dynamic type. */

ccw_node
ccw_kliche_vtable_store (ccw_ir *ir, ccw_node blk, const char *object_reg,
                         const char *vtable_reg)
{
  if (ir == NULL || blk == 0 || object_reg == NULL || vtable_reg == NULL)
    return 0;
  ccw_kliche_opnd ops[]
      = { CCW_K_REG (object_reg), CCW_K_REG (vtable_reg), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "vtable.store", CCW_TY_VOID, NULL, ops);
}

/* §6.1 oop: build a vtable from a list of method symbols.
 * methods is an array of method_count function symbols.  Returns a
 * vtable pointer in dest. */

ccw_node
ccw_kliche_vtable_build (ccw_ir *ir, ccw_node blk, const char *dest,
                         const char *const *methods, int method_count)
{
  if (ir == NULL || blk == 0 || dest == NULL || methods == NULL
      || method_count < 0)
    return 0;
  ccw_kliche_opnd ops[] = { CCW_K_INT (method_count), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "vtable.build", CCW_TY_PTR, dest, ops);
}

ccw_node
ccw_kliche_vtable_dispatch (ccw_ir *ir, ccw_node blk, const char *dest,
                            const char *vtable_reg, int slot,
                            const char *receiver_reg)
{
  ccw_kliche_opnd ops[] = { CCW_K_REG (vtable_reg), CCW_K_INT (slot),
                            CCW_K_REG (receiver_reg), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "vtable.dispatch", CCW_TY_I64, dest, ops);
}

/* ---------- field access ---------- */

/* §6.1 oop: read a field from an object.  field_idx is 0-based, indexing
 * into the object's data fields (after the header). */

ccw_node
ccw_kliche_field_get (ccw_ir *ir, ccw_node blk, const char *dest,
                      const char *object_reg, int field_idx,
                      ccw_ir_type type)
{
  if (ir == NULL || blk == 0 || dest == NULL || object_reg == NULL
      || field_idx < 0)
    return 0;
  ccw_kliche_opnd ops[]
      = { CCW_K_REG (object_reg), CCW_K_INT (field_idx), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "field.get", type, dest, ops);
}

/* §6.1 oop: write a value into an object's field. */

ccw_node
ccw_kliche_field_set (ccw_ir *ir, ccw_node blk, const char *object_reg,
                      int field_idx, const char *value_reg)
{
  if (ir == NULL || blk == 0 || object_reg == NULL || value_reg == NULL
      || field_idx < 0)
    return 0;
  ccw_kliche_opnd ops[] = { CCW_K_REG (object_reg), CCW_K_INT (field_idx),
                            CCW_K_REG (value_reg), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "field.set", CCW_TY_VOID, NULL, ops);
}

/* ---------- inheritance ---------- */

/* §6.1 oop: call a superclass method through the parent vtable.
 * subclass_vtable_reg is the vtable of the subclass (which contains
 * the parent vtable pointer at offset 0).  slot is the method index
 * in the parent's vtable.  receiver_reg is the object. */

ccw_node
ccw_kliche_super_call (ccw_ir *ir, ccw_node blk, const char *dest,
                       const char *subclass_vtable_reg, int slot,
                       const char *receiver_reg)
{
  if (ir == NULL || blk == 0 || subclass_vtable_reg == NULL
      || receiver_reg == NULL || slot < 0)
    return 0;
  ccw_kliche_opnd ops[]
      = { CCW_K_REG (subclass_vtable_reg), CCW_K_INT (slot),
          CCW_K_REG (receiver_reg), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "super.call", CCW_TY_I64, dest, ops);
}

/* ---------- interface dispatch ---------- */

/* §6.1 oop: dispatch through an interface vtable.
 * object_reg is the receiver; interface_id is a compile-time constant
 * identifying the interface; slot is the method index within the
 * interface's vtable.  The runtime resolves the interface vtable from
 * the object's type information. */

ccw_node
ccw_kliche_interface_dispatch (ccw_ir *ir, ccw_node blk, const char *dest,
                               const char *object_reg, int64_t interface_id,
                               int slot)
{
  if (ir == NULL || blk == 0 || object_reg == NULL || slot < 0)
    return 0;
  ccw_kliche_opnd ops[] = { CCW_K_REG (object_reg), CCW_K_INT (interface_id),
                            CCW_K_INT (slot), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "interface.dispatch", CCW_TY_I64, dest,
                          ops);
}

/* ---------- type checking ---------- */

/* §6.1 oop: check whether an object is an instance of a class.
 * class_symbol is a compile-time class name; the runtime resolves it
 * against the object's vtable.  Returns an i1 boolean in dest. */

ccw_node
ccw_kliche_instanceof (ccw_ir *ir, ccw_node blk, const char *dest,
                       const char *object_reg, const char *class_symbol)
{
  if (ir == NULL || blk == 0 || dest == NULL || object_reg == NULL
      || class_symbol == NULL)
    return 0;
  ccw_kliche_opnd ops[]
      = { CCW_K_REG (object_reg), CCW_K_FUNC (class_symbol), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "instanceof", CCW_TY_I1, dest, ops);
}

/* §6.1 oop: dynamic cast that checks instanceof and branches.
 * If the object is an instance of class_symbol, jump to success_block;
 * otherwise jump to fail_block.  This is a combined instanceof + branch. */

ccw_node
ccw_kliche_dynamic_cast (ccw_ir *ir, ccw_node blk, const char *object_reg,
                         const char *class_symbol,
                         const char *success_block, const char *fail_block)
{
  if (ir == NULL || blk == 0 || object_reg == NULL || class_symbol == NULL
      || success_block == NULL || fail_block == NULL)
    return 0;

  /* Emit instanceof check into a temp. */
  ccw_node check = ccw_kliche_instanceof (ir, blk, "cast.tmp", object_reg,
                                          class_symbol);
  if (check == 0)
    return 0;

  /* Branch on the result. */
  ccw_kliche_opnd ops[]
      = { CCW_K_REG ("cast.tmp"), CCW_K_BLOCK (success_block),
          CCW_K_BLOCK (fail_block), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "br.cond", CCW_TY_VOID, NULL, ops);
}

/* ---------- exception frames ---------- */

/* §6.1 oop: push an exception frame onto the handler stack.
 * handler_block is where execution resumes when an exception is thrown
 * while this frame is active. */

ccw_node
ccw_kliche_frame_push (ccw_ir *ir, ccw_node blk, const char *handler_block)
{
  if (ir == NULL || blk == 0 || handler_block == NULL)
    return 0;
  ccw_kliche_opnd ops[] = { CCW_K_BLOCK (handler_block), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "frame.push", CCW_TY_VOID, NULL, ops);
}

ccw_node
ccw_kliche_frame_pop (ccw_ir *ir, ccw_node blk)
{
  if (ir == NULL || blk == 0)
    return 0;
  ccw_kliche_opnd ops[] = { CCW_K_END };
  return ccw_kliche_emit (ir, blk, "frame.pop", CCW_TY_VOID, NULL, ops);
}

/* §6.1 oop: throw an exception.  exception_reg is a pointer to an
 * exception object.  This unwinds the handler stack to the nearest
 * active frame.push and jumps to the handler block. */

ccw_node
ccw_kliche_throw (ccw_ir *ir, ccw_node blk, const char *exception_reg)
{
  if (ir == NULL || blk == 0 || exception_reg == NULL)
    return 0;
  ccw_kliche_opnd ops[] = { CCW_K_REG (exception_reg), CCW_K_END };
  return ccw_kliche_emit (ir, blk, "throw", CCW_TY_VOID, NULL, ops);
}

/* §6.1 oop: rethrow the current exception from within a handler.
 * Takes no arguments; uses the implicit current exception. */

ccw_node
ccw_kliche_rethrow (ccw_ir *ir, ccw_node blk)
{
  if (ir == NULL || blk == 0)
    return 0;
  ccw_kliche_opnd ops[] = { CCW_K_END };
  return ccw_kliche_emit (ir, blk, "rethrow", CCW_TY_VOID, NULL, ops);
}
