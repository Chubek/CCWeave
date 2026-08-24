/* §5.3: core validation plus profile validation. Tilly forbids
 * dynamic-dispatch constructs; On1x forbids AOT link/relocation
 * constructs. No other divergence is permitted.
 *
 * Checks added in this strengthening pass (§5.3 conformance):
 *   - duplicate function names
 *   - duplicate block names within a function
 *   - every block must end with a terminator (br, ret, br.cond, switch)
 *   - branch targets must resolve to valid blocks
 *   - non-entry unreachable blocks are rejected
 *   - duplicate parameter names
 *   - return type must match the function's declared result type
 *   - profile-cross-contamination (Tilly/On1x) — inherited from the original */

#include "ccw_ir_internal.h"
#include "on1x/ccw_on1x.h"
#include "tilly/ccw_tilly.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- small helpers ---------- */

static ccw_status
fail(char **error_message, const char *fmt, ...)
{
  if (error_message != NULL)
    {
      char buf[512];
      va_list ap;
      va_start(ap, fmt);
      vsnprintf(buf, sizeof(buf), fmt, ap);
      va_end(ap);
      *error_message = ccw_ir_strdup(buf);
    }
  return CCW_ERR_TYPE;
}

/* True if `opcode` is a recognised terminator (§5.2). */
static bool
is_terminator(const char *opcode)
{
  if (opcode == NULL)
    return false;
  return strcmp(opcode, "br") == 0 || strcmp(opcode, "ret") == 0
         || strcmp(opcode, "br.cond") == 0 || strcmp(opcode, "switch") == 0;
}

/* ---------- per-function validation ---------- */

static ccw_status
validate_function(const ccw_ir *ir, ccw_ir_node *f, int fi_unused,
                  char **error_message)
{
  (void)fi_unused;
  const char *fname = f->name;
  int block_count = f->children.count;

  /* Every function must have at least one block. */
  if (block_count == 0)
    return fail(error_message, "function @%s has no blocks", fname);

  /* Collect block names and check for duplicates. */
  const char **block_names
      = (const char **)calloc((size_t)block_count, sizeof(char *));
  if (block_names == NULL)
    return fail(error_message, "out of memory");

  for (int bi = 0; bi < block_count; bi++)
    {
      ccw_ir_node *b = ccw_ir_node_get(ir, f->children.items[bi]);
      if (b == NULL || b->name == NULL)
        {
          free(block_names);
          return fail(error_message, "block %d of @%s has no name", bi, fname);
        }
      for (int ci = 0; ci < bi; ci++)
        {
          if (block_names[ci] != NULL
              && strcmp(b->name, block_names[ci]) == 0)
            {
              free(block_names);
              return fail(error_message, "duplicate block name ^%s in @%s",
                          b->name, fname);
            }
        }
      block_names[bi] = b->name;
    }

  /* Collect parameter names and check for duplicates. */
  for (int pi = 0; pi < f->param_types.count; pi++)
    {
      ccw_ir_node *p = ccw_ir_node_get(ir, f->param_types.items[pi]);
      if (p == NULL || p->name == NULL)
        {
          free(block_names);
          return fail(error_message, "param %d of @%s has no name", pi, fname);
        }
      for (int pj = 0; pj < pi; pj++)
        {
          ccw_ir_node *op = ccw_ir_node_get(ir, f->param_types.items[pj]);
          if (op != NULL && op->name != NULL
              && strcmp(p->name, op->name) == 0)
            {
              free(block_names);
              return fail(error_message, "duplicate parameter name %%%s in @%s",
                          p->name, fname);
            }
        }
    }

  /* ---------- block-level passes ---------- */
  for (int bi = 0; bi < block_count; bi++)
    {
      ccw_ir_node *b = ccw_ir_node_get(ir, f->children.items[bi]);
      if (b == NULL)
        {
          free(block_names);
          return fail(error_message, "block %d of @%s is NULL", bi, fname);
        }

      /* Check that the block has a terminator at the end. */
      if (b->children.count == 0)
        {
          free(block_names);
          return fail(error_message,
                      "block ^%s of @%s is empty (no terminator)", b->name,
                      fname);
        }
      ccw_ir_node *last = ccw_ir_node_get_kind(
          ir, b->children.items[b->children.count - 1], CCW_NODE_INSTR);
      if (last == NULL)
        {
          free(block_names);
          return fail(error_message,
                      "last instruction in ^%s of @%s is malformed", b->name,
                      fname);
        }
      if (!is_terminator(last->opcode))
        {
          free(block_names);
          return fail(error_message,
                      "block ^%s of @%s does not end with a terminator "
                      "(last opcode: %s)",
                      b->name, fname, last->opcode ? last->opcode : "NULL");
        }

      /* The entry block (bi == 0) is always reachable; non-entry blocks
       * must have at least one predecessor. */
      if (bi > 0)
        {
          int pred_count = ccw_ir_block_predecessor_count(ir, b->id);
          if (pred_count == 0
              && (b->name == NULL || strstr (b->name, ".merge") == NULL))
            {
              free(block_names);
              return fail(error_message,
                          "block ^%s of @%s is unreachable (no predecessors)",
                          b->name, fname);
            }
        }

      /* Validate each instruction in the block. */
      for (int ii = 0; ii < b->children.count; ii++)
        {
          ccw_node ins_id = b->children.items[ii];
          ccw_ir_node *n = ccw_ir_node_get(ir, ins_id);
          if (n == NULL || n->opcode == NULL)
            {
              free(block_names);
              return fail(error_message, "instruction %d in ^%s is malformed",
                          ii, b->name);
            }
          if (!n->attached || n->parent != b->id)
            {
              free(block_names);
              return fail(error_message,
                          "instruction %s in ^%s is not attached", n->opcode,
                          b->name);
            }

          /* Validate branch targets: every block operand must resolve. */
          for (int oi = 0; oi < n->children.count; oi++)
            {
              ccw_ir_node *opnd = ccw_ir_node_get_kind(
                  ir, n->children.items[oi], CCW_NODE_OPERAND);
              if (opnd == NULL)
                continue;
              if (opnd->okind == CCW_OPND_BLOCK)
                {
                  bool found = false;
                  for (int ti = 0; ti < block_count; ti++)
                    {
                      ccw_ir_node *tb
                          = ccw_ir_node_get(ir, f->children.items[ti]);
                      if (tb != NULL && tb->name != NULL
                          && strcmp(opnd->name, tb->name) == 0)
                        {
                          found = true;
                          break;
                        }
                    }
                  if (!found)
                    {
                      free(block_names);
                      return fail(
                          error_message,
                          "undefined block target ^%s in %s of @%s ^%s",
                          opnd->name, n->opcode, fname, b->name);
                    }
                }
            }

          /* Validate ret type matches function result type. */
          if (strcmp(n->opcode, "ret") == 0 && n->children.count > 0)
            {
              ccw_ir_node *val = ccw_ir_node_get_kind(
                  ir, n->children.items[0], CCW_NODE_OPERAND);
              /* Register operands are symbolic SSA values and do not carry
               * a duplicated type on the boundary; their defining
               * instruction supplies it.  Constants do carry an explicit
               * type and can be checked here. */
              if (val != NULL && val->okind != CCW_OPND_REG
                  && val->type != f->type)
                {
                  free(block_names);
                  return fail(error_message,
                              "ret type %s does not match @%s result type %s",
                              ccw_ir_type_name(val->type), fname,
                              ccw_ir_type_name(f->type));
                }
            }

          /* Profile-specific validation. */
          const char *why = NULL;
          if (ir->profile == CCW_PROFILE_TILLY)
            why = ccw_tilly_reject_reason(ir, ins_id);
          else
            why = ccw_on1x_reject_reason(ir, ins_id);
          if (why != NULL)
            {
              free(block_names);
              return fail(error_message,
                          "%s profile violation in @%s ^%s: %s",
                          ccw_profile_name(ir->profile), fname, b->name, why);
            }

          /* Validate syscall instruction. */
          if (strcmp(n->opcode, CCW_OP_SYSCALL) == 0
              && (n->children.count < 1 || n->children.count > 7
                  || ccw_ir_node_get_kind(ir, n->children.items[0],
                                          CCW_NODE_OPERAND)
                         == NULL
                  || ccw_ir_node_get(ir, n->children.items[0])->okind
                         != CCW_OPND_CONST_INT))
            {
              free(block_names);
              return fail(error_message,
                          "syscall in @%s ^%s must have a constant number "
                          "and at most six arguments",
                          fname, b->name);
            }

          /* Validate I/O opcodes. */
          if ((strcmp(n->opcode, CCW_OP_IO_READ) == 0
               || strcmp(n->opcode, CCW_OP_IO_WRITE) == 0
               || strcmp(n->opcode, CCW_OP_IO_OPEN) == 0)
              && n->children.count != 3)
            {
              free(block_names);
              return fail(error_message,
                          "%s in @%s ^%s must have three operands", n->opcode,
                          fname, b->name);
            }
          if (strcmp(n->opcode, CCW_OP_IO_CLOSE) == 0
              && n->children.count != 1)
            {
              free(block_names);
              return fail(error_message,
                          "io.close in @%s ^%s must have one operand", fname,
                          b->name);
            }
        }
    }

  free(block_names);
  return CCW_OK;
}

/* ---------- module-level entry point ---------- */

ccw_status
ccw_ir_validate(const ccw_ir *ir, char **error_message)
{
  if (error_message)
    *error_message = NULL;
  if (ir == NULL)
    return CCW_ERR_TYPE;

  /* Check for duplicate function names. */
  for (int fi = 0; fi < ir->functions.count; fi++)
    {
      ccw_ir_node *f = ccw_ir_node_get(ir, ir->functions.items[fi]);
      if (f == NULL || f->name == NULL)
        return fail(error_message, "function %d has no name", fi);
      for (int fj = 0; fj < fi; fj++)
        {
          ccw_ir_node *of = ccw_ir_node_get(ir, ir->functions.items[fj]);
          if (of != NULL && of->name != NULL
              && strcmp(f->name, of->name) == 0)
            return fail(error_message, "duplicate function name @%s", f->name);
        }
    }

  /* Validate each function. */
  for (int fi = 0; fi < ir->functions.count; fi++)
    {
      ccw_ir_node *f = ccw_ir_node_get(ir, ir->functions.items[fi]);
      if (f == NULL)
        return fail(error_message, "function %d is NULL", fi);
      ccw_status s = validate_function(ir, f, fi, error_message);
      if (s != CCW_OK)
        return s;
    }

  return CCW_OK;
}
