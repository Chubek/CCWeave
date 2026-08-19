/* Cephyr lowering — §6.
 *
 * Lowers the typed AST to Weave IR (core) via the Kliche imperative
 * stereotype. Generates Tilly-profile Weave IR modules. */

#include "cephyr_lower.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../../kliche/ccw_kliche.h"
#include "kvec.h"

/* ---------- lowering context ---------- */

struct cephyr_lower_ctx
{
  int label_counter;
  int reg_counter;
  char err_buf[512];
};

static char *
cephyr_lower_strdup (const char *s)
{
  if (!s)
    return NULL;
  size_t n = strlen (s) + 1u;
  char *copy = malloc (n);
  if (copy)
    memcpy (copy, s, n);
  return copy;
}

cephyr_lower_ctx *
cephyr_lower_create (void)
{
  return calloc (1, sizeof (cephyr_lower_ctx));
}

void
cephyr_lower_destroy (cephyr_lower_ctx *ctx)
{
  free (ctx);
}

/* ---------- type mapping ---------- */

ccw_ir_type
cephyr_lower_map_type (const cephyr_type *ct)
{
  if (!ct)
    return CCW_TY_VOID;
  switch (ct->kind)
    {
    case CEPHYR_TY_VOID:
      return CCW_TY_VOID;
    case CEPHYR_TY_BOOL:
      return CCW_TY_I1;
    case CEPHYR_TY_CHAR:
    case CEPHYR_TY_SCHAR:
    case CEPHYR_TY_UCHAR:
      return CCW_TY_I8;
    case CEPHYR_TY_SHORT:
    case CEPHYR_TY_USHORT:
      return CCW_TY_I16;
    case CEPHYR_TY_INT:
    case CEPHYR_TY_UINT:
      return CCW_TY_I32;
    case CEPHYR_TY_LONG:
    case CEPHYR_TY_ULONG:
    case CEPHYR_TY_LONGLONG:
    case CEPHYR_TY_ULONGLONG:
      return CCW_TY_I64;
    case CEPHYR_TY_FLOAT:
      return CCW_TY_F32;
    case CEPHYR_TY_DOUBLE:
    case CEPHYR_TY_LONGDOUBLE:
      return CCW_TY_F64;
    case CEPHYR_TY_PTR:
      return CCW_TY_PTR;
    case CEPHYR_TY_ARRAY:
      return CCW_TY_PTR; /* arrays decay to pointers */
    case CEPHYR_TY_ENUM:
      return CCW_TY_I32;
    case CEPHYR_TY_STRUCT:
    case CEPHYR_TY_UNION:
      return CCW_TY_PTR; /* structs are passed by pointer */
    case CEPHYR_TY_FUNC:
      return CCW_TY_PTR; /* function pointers */
    case CEPHYR_TY_TYPEDEF:
      return ct->inner ? cephyr_lower_map_type (ct->inner) : CCW_TY_VOID;
    default:
      return CCW_TY_VOID;
    }
}

/* ---------- helpers ---------- */

static char *
fresh_reg (cephyr_lower_ctx *ctx)
{
  char *buf = malloc (32);
  snprintf (buf, 32, "r%d", ctx->reg_counter++);
  return buf;
}

static char *
fresh_label (cephyr_lower_ctx *ctx, const char *prefix)
{
  char *buf = malloc (64);
  snprintf (buf, 64, "%s%d", prefix, ctx->label_counter++);
  return buf;
}

/* ---------- expression lowering ---------- */

static char *lower_expr (cephyr_lower_ctx *ctx, ccw_ir *ir, ccw_node blk,
                         const cephyr_ast_node *node);

static char *
lower_binary (cephyr_lower_ctx *ctx, ccw_ir *ir, ccw_node blk,
              const cephyr_ast_node *node)
{
  char *lhs = lower_expr (ctx, ir, blk, node->data.binop.lhs);
  char *rhs = lower_expr (ctx, ir, blk, node->data.binop.rhs);
  char *dest = fresh_reg (ctx);
  ccw_ir_type ty = cephyr_lower_map_type (node->type);

  /* Map C operators to Weave IR opcodes */
  const char *op = node->data.binop.op;
  const char *ir_op = "iadd"; /* default */
  if (strcmp (op, "+") == 0)
    ir_op = "iadd";
  else if (strcmp (op, "-") == 0)
    ir_op = "isub";
  else if (strcmp (op, "*") == 0)
    ir_op = "imul";
  else if (strcmp (op, "/") == 0)
    ir_op = "idiv";
  else if (strcmp (op, "%") == 0)
    ir_op = "irem";
  else if (strcmp (op, "==") == 0)
    ir_op = "icmp.eq";
  else if (strcmp (op, "!=") == 0)
    ir_op = "icmp.ne";
  else if (strcmp (op, "<") == 0)
    ir_op = "icmp.lt";
  else if (strcmp (op, ">") == 0)
    ir_op = "icmp.gt";
  else if (strcmp (op, "<=") == 0)
    ir_op = "icmp.le";
  else if (strcmp (op, ">=") == 0)
    ir_op = "icmp.ge";
  else if (strcmp (op, "&&") == 0)
    ir_op = "logic.and";
  else if (strcmp (op, "||") == 0)
    ir_op = "logic.or";
  else if (strcmp (op, "&") == 0)
    ir_op = "iand";
  else if (strcmp (op, "|") == 0)
    ir_op = "ior";
  else if (strcmp (op, "^") == 0)
    ir_op = "ixor";
  else if (strcmp (op, "<<") == 0)
    ir_op = "shl";
  else if (strcmp (op, ">>") == 0)
    ir_op = "ashr";

  ccw_kliche_binary (ir, blk, ir_op, dest, lhs, rhs, ty);
  free (lhs);
  free (rhs);
  return dest;
}

static char *
lower_unary (cephyr_lower_ctx *ctx, ccw_ir *ir, ccw_node blk,
             const cephyr_ast_node *node)
{
  char *operand = lower_expr (ctx, ir, blk, node->data.unop.operand);
  char *dest = fresh_reg (ctx);
  ccw_ir_type ty = cephyr_lower_map_type (node->type);

  const char *op = node->data.unop.op;
  const char *ir_op = "ineg"; /* default */
  if (strcmp (op, "-") == 0)
    ir_op = "ineg";
  else if (strcmp (op, "!") == 0)
    ir_op = "logic.not";
  else if (strcmp (op, "~") == 0)
    ir_op = "inot";
  else if (strcmp (op, "*") == 0)
    ir_op = "load";
  else if (strcmp (op, "&") == 0)
    {
      /* Address-of: just return the operand (it's a reference to the variable)
       */
      free (dest);
      return operand;
    }

  ccw_kliche_unary (ir, blk, ir_op, dest, operand, ty);
  free (operand);
  return dest;
}

static char *
lower_call (cephyr_lower_ctx *ctx, ccw_ir *ir, ccw_node blk,
            const cephyr_ast_node *node)
{
  /* Lower args */
  kvec_t (char *) arg_regs;
  kv_init (arg_regs);
  for (int i = 0; i < node->data.call.arg_count; i++)
    {
      kv_push (char *, arg_regs,
               lower_expr (ctx, ir, blk, node->data.call.args[i]));
    }

  /* Get callee name */
  const char *callee = "unknown";
  if (node->data.call.callee->kind == CEPHYR_NODE_EXPR_IDENT
      && node->data.call.callee->name)
    {
      callee = node->data.call.callee->name;
    }

  char *dest = fresh_reg (ctx);
  ccw_ir_type result_ty = cephyr_lower_map_type (node->type);

  ccw_kliche_call (ir, blk, dest, callee, (const char *const *)arg_regs.a,
                   (int)kv_size (arg_regs), result_ty);

  for (size_t i = 0; i < kv_size (arg_regs); i++)
    free (kv_A (arg_regs, i));
  kv_destroy (arg_regs);
  return dest;
}

static char *
lower_expr (cephyr_lower_ctx *ctx, ccw_ir *ir, ccw_node blk,
            const cephyr_ast_node *node)
{
  if (!node)
    {
      char *r = fresh_reg (ctx);
      ccw_kliche_int_const (ir, blk, r, 0);
      return r;
    }

  switch (node->kind)
    {
    case CEPHYR_NODE_EXPR_INT_CONST:
      {
        char *r = fresh_reg (ctx);
        ccw_kliche_int_const (ir, blk, r, node->data.int_value);
        return r;
      }
    case CEPHYR_NODE_EXPR_IDENT:
      {
        /* Just return the identifier name as a reg reference */
        char *r = malloc (strlen (node->name) + 4);
        snprintf (r, strlen (node->name) + 1, "%s", node->name);
        return r;
      }
    case CEPHYR_NODE_EXPR_BINARY:
      return lower_binary (ctx, ir, blk, node);
    case CEPHYR_NODE_EXPR_UNARY:
      return lower_unary (ctx, ir, blk, node);
    case CEPHYR_NODE_EXPR_CALL:
      return lower_call (ctx, ir, blk, node);
    case CEPHYR_NODE_EXPR_ASSIGN:
      {
        char *lhs = lower_expr (ctx, ir, blk, node->data.binop.lhs);
        char *rhs = lower_expr (ctx, ir, blk, node->data.binop.rhs);
        /* Store rhs into lhs */
        ccw_kliche_local_store (ir, blk, lhs, rhs);
        free (rhs);
        return lhs;
      }
    default:
      /* Default: emit a constant 0 */
      {
        char *r = fresh_reg (ctx);
        ccw_kliche_int_const (ir, blk, r, 0);
        return r;
      }
    }
}

/* ---------- statement lowering ---------- */

static void lower_stmt (cephyr_lower_ctx *ctx, ccw_ir *ir, ccw_node fn,
                        const cephyr_ast_node *node, ccw_node *current_blk);

static void
lower_block (cephyr_lower_ctx *ctx, ccw_ir *ir, ccw_node fn,
             const cephyr_ast_node *node, ccw_node *current_blk)
{
  if (!node || node->kind != CEPHYR_NODE_BLOCK)
    return;
  for (int i = 0; i < node->data.block.stmt_count; i++)
    {
      lower_stmt (ctx, ir, fn, node->data.block.stmts[i], current_blk);
    }
}

static void
lower_stmt (cephyr_lower_ctx *ctx, ccw_ir *ir, ccw_node fn,
            const cephyr_ast_node *node, ccw_node *current_blk)
{
  if (!node)
    return;

  switch (node->kind)
    {
    case CEPHYR_NODE_BLOCK:
      lower_block (ctx, ir, fn, node, current_blk);
      break;

    case CEPHYR_NODE_STMT_RETURN:
      {
        if (node->data.unop.operand)
          {
            char *val
                = lower_expr (ctx, ir, *current_blk, node->data.unop.operand);
            ccw_kliche_return (ir, *current_blk, val);
            free (val);
          }
        else
          {
            ccw_kliche_return (ir, *current_blk, NULL);
          }
        break;
      }

    case CEPHYR_NODE_STMT_IF:
      {
        char *cond
            = lower_expr (ctx, ir, *current_blk, node->data.if_stmt.cond);
        char *then_lbl = fresh_label (ctx, "then");
        char *else_lbl = fresh_label (ctx, "else");
        char *end_lbl = fresh_label (ctx, "endif");

        ccw_node then_blk = ccw_ir_block_add (ir, fn, then_lbl);
        ccw_node else_blk = ccw_ir_block_add (ir, fn, else_lbl);
        ccw_node end_blk = ccw_ir_block_add (ir, fn, end_lbl);

        ccw_kliche_branch_if (ir, *current_blk, cond, then_lbl, else_lbl);
        free (cond);

        *current_blk = then_blk;
        lower_stmt (ctx, ir, fn, node->data.if_stmt.then_branch, current_blk);
        if (*current_blk)
          ccw_kliche_jump (ir, *current_blk, end_lbl);

        *current_blk = else_blk;
        if (node->data.if_stmt.else_branch)
          {
            lower_stmt (ctx, ir, fn, node->data.if_stmt.else_branch,
                        current_blk);
          }
        if (*current_blk)
          ccw_kliche_jump (ir, *current_blk, end_lbl);

        *current_blk = end_blk;
        free (then_lbl);
        free (else_lbl);
        free (end_lbl);
        break;
      }

    case CEPHYR_NODE_STMT_WHILE:
      {
        char *cond_lbl = fresh_label (ctx, "while_cond");
        char *body_lbl = fresh_label (ctx, "while_body");
        char *end_lbl = fresh_label (ctx, "while_end");

        ccw_node cond_blk = ccw_ir_block_add (ir, fn, cond_lbl);
        ccw_node body_blk = ccw_ir_block_add (ir, fn, body_lbl);
        ccw_node end_blk = ccw_ir_block_add (ir, fn, end_lbl);

        ccw_kliche_jump (ir, *current_blk, cond_lbl);
        *current_blk = cond_blk;

        char *cond
            = lower_expr (ctx, ir, *current_blk, node->data.while_stmt.cond);
        ccw_kliche_branch_if (ir, *current_blk, cond, body_lbl, end_lbl);
        free (cond);

        *current_blk = body_blk;
        lower_stmt (ctx, ir, fn, node->data.while_stmt.body, current_blk);
        if (*current_blk)
          ccw_kliche_jump (ir, *current_blk, cond_lbl);

        *current_blk = end_blk;
        free (cond_lbl);
        free (body_lbl);
        free (end_lbl);
        break;
      }

    case CEPHYR_NODE_STMT_EXPR:
      {
        if (node->data.unop.operand)
          {
            char *val
                = lower_expr (ctx, ir, *current_blk, node->data.unop.operand);
            free (val);
          }
        break;
      }

    case CEPHYR_NODE_STMT_DECL:
      {
        /* Variable declaration: allocate a local slot */
        if (node->name && node->type)
          {
            ccw_ir_type ty = cephyr_lower_map_type (node->type);
            char *dest = malloc (strlen (node->name) + 4);
            snprintf (dest, strlen (node->name) + 1, "%s", node->name);
            ccw_kliche_local_alloc (ir, *current_blk, dest, ty);
            free (dest);
          }
        break;
      }

    case CEPHYR_NODE_VAR_DECL:
      {
        /* Global variable (or local with init) */
        if (node->name && node->type)
          {
            ccw_ir_type ty = cephyr_lower_map_type (node->type);
            char *dest = malloc (strlen (node->name) + 4);
            snprintf (dest, strlen (node->name) + 1, "%s", node->name);
            ccw_kliche_local_alloc (ir, *current_blk, dest, ty);
            if (node->data.var_decl.init)
              {
                char *init_val = lower_expr (ctx, ir, *current_blk,
                                             node->data.var_decl.init);
                ccw_kliche_local_store (ir, *current_blk, dest, init_val);
                free (init_val);
              }
            free (dest);
          }
        break;
      }

    default:
      break;
    }
}

/* ---------- function lowering ---------- */

ccw_node
cephyr_lower_function (cephyr_lower_ctx *ctx, ccw_ir *ir,
                       const cephyr_ast_node *func_def, char **error_message)
{
  if (!func_def || func_def->kind != CEPHYR_NODE_FUNC_DEF)
    {
      if (error_message)
        *error_message = cephyr_lower_strdup ("not a function definition");
      return 0;
    }

  const char *name = func_def->name ? func_def->name : "anon";
  ccw_ir_type result_ty = CCW_TY_VOID;
  if (func_def->type && func_def->type->return_type)
    {
      result_ty = cephyr_lower_map_type (func_def->type->return_type);
    }

  ccw_node fn = ccw_ir_function_add (ir, name, result_ty);
  if (!fn)
    {
      if (error_message)
        *error_message = cephyr_lower_strdup ("failed to create function");
      return 0;
    }

  /* Add parameters */
  if (func_def->type && func_def->type->param_count > 0)
    {
      for (int i = 0; i < func_def->type->param_count; i++)
        {
          ccw_ir_type pty
              = cephyr_lower_map_type (func_def->type->param_types[i]);
          const char *pname
              = (func_def->type->param_names && func_def->type->param_names[i])
                    ? func_def->type->param_names[i]
                    : "?";
          ccw_ir_function_add_param (ir, fn, pty, pname);
        }
    }

  /* Create entry block */
  ccw_node entry_blk = ccw_ir_block_add (ir, fn, "entry");
  ccw_node current_blk = entry_blk;

  /* Lower the function body */
  ctx->label_counter = 0;
  ctx->reg_counter = 0;
  lower_stmt (ctx, ir, fn, func_def->data.func_def.body, &current_blk);

  return fn;
}

/* ---------- program lowering ---------- */

ccw_ir *
cephyr_lower_program (cephyr_lower_ctx *ctx, const cephyr_ast_node *program,
                      const char *module_name, char **error_message)
{
  if (!program)
    {
      if (error_message)
        *error_message = cephyr_lower_strdup ("null program AST");
      return NULL;
    }

  ccw_ir *ir = ccw_ir_module_create (
      module_name ? module_name : "cephyr_module", CCW_PROFILE_TILLY);
  if (!ir)
    {
      if (error_message)
        *error_message = cephyr_lower_strdup ("failed to create IR module");
      return NULL;
    }

  /* Walk the program and lower each function definition */
  const cephyr_ast_node *child = program;
  if (child->kind == CEPHYR_NODE_PROGRAM)
    {
      /* Children are in the block's stmt array */
      if (child->data.block.stmts)
        {
          for (int i = 0; i < child->data.block.stmt_count; i++)
            {
              cephyr_ast_node *node = child->data.block.stmts[i];
              if (node->kind == CEPHYR_NODE_FUNC_DEF)
                {
                  cephyr_lower_function (ctx, ir, node, error_message);
                }
            }
        }
    }

  return ir;
}
