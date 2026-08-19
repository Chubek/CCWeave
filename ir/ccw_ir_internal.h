/* Internal layout of the canonical in-memory module (§5.4).
 * Shared by the core, the printer, the parser, and the profile passes. */

#ifndef CCW_IR_INTERNAL_H
#define CCW_IR_INTERNAL_H

#include "ccw_ir.h"
#include "kvec.h"

typedef struct
{
  char *key;
  char *value;
} ccw_attr;

typedef struct
{
  ccw_attr *items;
  int count;
  int cap;
} ccw_attrs;

typedef struct
{
  ccw_node *items;
  int count;
  int cap;
} ccw_node_vec;

typedef struct ccw_ir_node
{
  ccw_node_kind kind;
  ccw_node id;
  ccw_node parent; /* block for instrs, function for blocks */
  char *name;      /* function/block name, operand name, dest */
  char *opcode;    /* instructions */
  ccw_ir_type type;
  ccw_operand_kind okind;
  int64_t ival;
  double fval;
  ccw_node_vec children;    /* fn: blocks; block: instrs; instr: operands */
  ccw_node_vec param_types; /* functions: encoded param operand nodes */
  ccw_attrs attrs;
  bool attached; /* instruction spliced into a block */
} ccw_ir_node;

struct ccw_ir
{
  char *name;
  ccw_profile profile;
  ccw_attrs attrs;
  ccw_ir_node *nodes; /* index 0 unused: node id 0 is nil */
  size_t node_count;
  size_t node_cap;
  ccw_node_vec functions;
};

ccw_ir_node *ccw_ir_node_get (const ccw_ir *ir, ccw_node id);
ccw_ir_node *ccw_ir_node_get_kind (const ccw_ir *ir, ccw_node id,
                                   ccw_node_kind k);
void ccw_node_vec_push (ccw_node_vec *v, ccw_node n);
char *ccw_ir_strdup (const char *s);

#endif /* CCW_IR_INTERNAL_H */
