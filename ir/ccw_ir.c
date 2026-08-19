/* Weave IR core: canonical in-memory representation and programmatic API. */

#include "ccw_ir_internal.h"

#include "kstring.h"

#include <stdlib.h>
#include <string.h>

/* ---------- small utilities ---------- */

char *
ccw_ir_strdup (const char *s)
{
  if (s == NULL)
    return NULL;
  kstring_t copy = { 0, 0, NULL };
  if (kputs (s, &copy) == EOF)
    return NULL;
  return ks_release (&copy);
}

void
ccw_node_vec_push (ccw_node_vec *v, ccw_node n)
{
  kvec_t (ccw_node) values = { (size_t)v->count, (size_t)v->cap, v->items };
  kv_push (ccw_node, values, n);
  v->items = values.a;
  v->count = (int)values.n;
  v->cap = (int)values.m;
}

static void
ccw_node_vec_free (ccw_node_vec *v)
{
  kvec_t (ccw_node) values = { (size_t)v->count, (size_t)v->cap, v->items };
  kv_destroy (values);
  v->items = NULL;
  v->count = v->cap = 0;
}

static void
ccw_node_vec_remove_at (ccw_node_vec *v, int idx)
{
  if (idx < 0 || idx >= v->count)
    return;
  memmove (&v->items[idx], &v->items[idx + 1],
           (size_t)(v->count - idx - 1) * sizeof (*v->items));
  v->count--;
}

static void
ccw_node_vec_insert_at (ccw_node_vec *v, int idx, ccw_node n)
{
  ccw_node_vec_push (v, 0);
  memmove (&v->items[idx + 1], &v->items[idx],
           (size_t)(v->count - idx - 1) * sizeof (*v->items));
  v->items[idx] = n;
}

static int
ccw_node_vec_index_of (const ccw_node_vec *v, ccw_node n)
{
  for (int i = 0; i < v->count; i++)
    if (v->items[i] == n)
      return i;
  return -1;
}

static void
ccw_attrs_free (ccw_attrs *a)
{
  for (int i = 0; i < a->count; i++)
    {
      free (a->items[i].key);
      free (a->items[i].value);
    }
  free (a->items);
  a->items = NULL;
  a->count = a->cap = 0;
}

static ccw_status
ccw_attrs_set (ccw_attrs *a, const char *key, const char *value)
{
  if (key == NULL || value == NULL)
    return CCW_ERR_TYPE;
  for (int i = 0; i < a->count; i++)
    {
      if (strcmp (a->items[i].key, key) == 0)
        {
          char *v = ccw_ir_strdup (value);
          if (v == NULL)
            return CCW_ERR_OOM;
          free (a->items[i].value);
          a->items[i].value = v;
          return CCW_OK;
        }
    }
  if (a->count == a->cap)
    {
      kvec_t (ccw_attr) items = { (size_t)a->count, (size_t)a->cap, a->items };
      kv_push (ccw_attr, items, (ccw_attr){ 0 });
      a->items = items.a;
      a->cap = (int)items.m;
      a->count = (int)items.n - 1;
    }
  char *k = ccw_ir_strdup (key);
  char *v = ccw_ir_strdup (value);
  if (k == NULL || v == NULL)
    {
      free (k);
      free (v);
      return CCW_ERR_OOM;
    }
  a->items[a->count].key = k;
  a->items[a->count].value = v;
  a->count++;
  return CCW_OK;
}

/* ---------- profiles and types ---------- */

const char *
ccw_profile_name (ccw_profile p)
{
  return p == CCW_PROFILE_ON1X ? "on1x" : "tilly";
}

bool
ccw_profile_parse (const char *name, ccw_profile *out)
{
  if (name == NULL)
    return false;
  if (strcmp (name, "tilly") == 0)
    {
      if (out)
        *out = CCW_PROFILE_TILLY;
      return true;
    }
  if (strcmp (name, "on1x") == 0)
    {
      if (out)
        *out = CCW_PROFILE_ON1X;
      return true;
    }
  return false;
}

static const char *const ccw_type_names[]
    = { "void", "i1", "i8", "i16", "i32", "i64", "f32", "f64", "ptr" };

const char *
ccw_ir_type_name (ccw_ir_type t)
{
  if ((int)t < 0
      || (size_t)t >= sizeof (ccw_type_names) / sizeof (ccw_type_names[0]))
    return "void";
  return ccw_type_names[t];
}

bool
ccw_ir_type_parse (const char *name, ccw_ir_type *out)
{
  if (name == NULL)
    return false;
  for (size_t i = 0; i < sizeof (ccw_type_names) / sizeof (ccw_type_names[0]);
       i++)
    {
      if (strcmp (name, ccw_type_names[i]) == 0)
        {
          if (out)
            *out = (ccw_ir_type)i;
          return true;
        }
    }
  return false;
}

/* ---------- node table ---------- */

ccw_ir_node *
ccw_ir_node_get (const ccw_ir *ir, ccw_node id)
{
  if (ir == NULL || id == 0 || id >= ir->node_count)
    return NULL;
  ccw_ir_node *n = &ir->nodes[id];
  return n->kind == CCW_NODE_DEAD ? NULL : n;
}

ccw_ir_node *
ccw_ir_node_get_kind (const ccw_ir *ir, ccw_node id, ccw_node_kind k)
{
  ccw_ir_node *n = ccw_ir_node_get (ir, id);
  return (n != NULL && n->kind == k) ? n : NULL;
}

static ccw_ir_node *
ccw_ir_node_new (ccw_ir *ir, ccw_node_kind kind)
{
  if (ir->node_count == ir->node_cap)
    {
      kvec_t (ccw_ir_node) nodes = { ir->node_count, ir->node_cap, ir->nodes };
      kv_push (ccw_ir_node, nodes, (ccw_ir_node){ 0 });
      ir->nodes = nodes.a;
      ir->node_cap = nodes.m;
      ir->node_count = nodes.n - 1;
    }
  ccw_ir_node *n = &ir->nodes[ir->node_count];
  memset (n, 0, sizeof (*n));
  n->kind = kind;
  n->id = (ccw_node)ir->node_count;
  ir->node_count++;
  return n;
}

/* ---------- module lifecycle ---------- */

ccw_ir *
ccw_ir_module_create (const char *name, ccw_profile profile)
{
  ccw_ir *ir = (ccw_ir *)calloc (1, sizeof (*ir));
  if (ir == NULL)
    return NULL;
  ir->name = ccw_ir_strdup (name ? name : "module");
  ir->profile = profile;
  /* Id 0 is nil, so slot zero is permanently dead. */
  if (ir->name == NULL || ccw_ir_node_new (ir, CCW_NODE_DEAD) == NULL)
    {
      ccw_ir_module_destroy (ir);
      return NULL;
    }
  return ir;
}

void
ccw_ir_module_destroy (ccw_ir *ir)
{
  if (ir == NULL)
    return;
  for (size_t i = 0; i < ir->node_count; i++)
    {
      ccw_ir_node *n = &ir->nodes[i];
      free (n->name);
      free (n->opcode);
      ccw_node_vec_free (&n->children);
      ccw_node_vec_free (&n->param_types);
      ccw_attrs_free (&n->attrs);
    }
  free (ir->nodes);
  ccw_node_vec_free (&ir->functions);
  ccw_attrs_free (&ir->attrs);
  free (ir->name);
  free (ir);
}

const char *
ccw_ir_module_name (const ccw_ir *ir)
{
  return ir ? ir->name : NULL;
}

ccw_profile
ccw_ir_module_profile (const ccw_ir *ir)
{
  return ir ? ir->profile : CCW_PROFILE_TILLY;
}

/* ---------- attributes ---------- */

static ccw_attrs *
ccw_attrs_of (const ccw_ir *ir, ccw_node owner)
{
  if (ir == NULL)
    return NULL;
  if (owner == 0)
    return (ccw_attrs *)&ir->attrs;
  ccw_ir_node *n = ccw_ir_node_get (ir, owner);
  return n ? &n->attrs : NULL;
}

ccw_status
ccw_ir_attr_set (ccw_ir *ir, ccw_node owner, const char *key,
                 const char *value)
{
  ccw_attrs *a = ccw_attrs_of (ir, owner);
  if (a == NULL)
    return CCW_ERR_TYPE;
  return ccw_attrs_set (a, key, value);
}

int
ccw_ir_attr_count (const ccw_ir *ir, ccw_node owner)
{
  ccw_attrs *a = ccw_attrs_of (ir, owner);
  return a ? a->count : 0;
}

const char *
ccw_ir_attr_key (const ccw_ir *ir, ccw_node owner, int idx)
{
  ccw_attrs *a = ccw_attrs_of (ir, owner);
  if (a == NULL || idx < 0 || idx >= a->count)
    return NULL;
  return a->items[idx].key;
}

const char *
ccw_ir_attr_value (const ccw_ir *ir, ccw_node owner, int idx)
{
  ccw_attrs *a = ccw_attrs_of (ir, owner);
  if (a == NULL || idx < 0 || idx >= a->count)
    return NULL;
  return a->items[idx].value;
}

const char *
ccw_ir_attr_lookup (const ccw_ir *ir, ccw_node owner, const char *key)
{
  ccw_attrs *a = ccw_attrs_of (ir, owner);
  if (a == NULL || key == NULL)
    return NULL;
  for (int i = 0; i < a->count; i++)
    if (strcmp (a->items[i].key, key) == 0)
      return a->items[i].value;
  return NULL;
}

/* ---------- construction ---------- */

ccw_node
ccw_ir_function_add (ccw_ir *ir, const char *name, ccw_ir_type result)
{
  if (ir == NULL || name == NULL)
    return 0;
  ccw_ir_node *n = ccw_ir_node_new (ir, CCW_NODE_FUNCTION);
  if (n == NULL)
    return 0;
  n->name = ccw_ir_strdup (name);
  n->type = result;
  ccw_node_vec_push (&ir->functions, n->id);
  return n->id;
}

ccw_status
ccw_ir_function_add_param (ccw_ir *ir, ccw_node fn, ccw_ir_type type,
                           const char *name)
{
  if (ccw_ir_node_get_kind (ir, fn, CCW_NODE_FUNCTION) == NULL || name == NULL)
    return CCW_ERR_TYPE;
  ccw_node p = ccw_ir_operand_reg (ir, name);
  if (p == 0)
    return CCW_ERR_OOM;
  /* Operand creation may realloc the node table; re-resolve. */
  ccw_ir_node *f = ccw_ir_node_get_kind (ir, fn, CCW_NODE_FUNCTION);
  ccw_ir_node *pn = ccw_ir_node_get (ir, p);
  if (f == NULL || pn == NULL)
    return CCW_ERR_TYPE;
  pn->type = type;
  ccw_node_vec_push (&f->param_types, p);
  return CCW_OK;
}

ccw_node
ccw_ir_block_add (ccw_ir *ir, ccw_node fn, const char *name)
{
  if (ccw_ir_node_get_kind (ir, fn, CCW_NODE_FUNCTION) == NULL || name == NULL)
    return 0;
  ccw_ir_node *b = ccw_ir_node_new (ir, CCW_NODE_BLOCK);
  if (b == NULL)
    return 0;
  b->name = ccw_ir_strdup (name);
  b->parent = fn;
  ccw_node bid = b->id;
  ccw_ir_node *f = ccw_ir_node_get_kind (ir, fn, CCW_NODE_FUNCTION);
  ccw_node_vec_push (&f->children, bid);
  return bid;
}

ccw_node
ccw_ir_instr_build (ccw_ir *ir, const char *opcode, ccw_ir_type type)
{
  if (ir == NULL || opcode == NULL)
    return 0;
  ccw_ir_node *n = ccw_ir_node_new (ir, CCW_NODE_INSTR);
  if (n == NULL)
    return 0;
  n->opcode = ccw_ir_strdup (opcode);
  n->type = type;
  n->attached = false;
  return n->id;
}

ccw_status
ccw_ir_instr_set_dest (ccw_ir *ir, ccw_node ins, const char *dest)
{
  ccw_ir_node *n = ccw_ir_node_get_kind (ir, ins, CCW_NODE_INSTR);
  if (n == NULL)
    return CCW_ERR_TYPE;
  char *copy = dest ? ccw_ir_strdup (dest) : NULL;
  if (dest != NULL && copy == NULL)
    return CCW_ERR_OOM;
  free (n->name);
  n->name = copy;
  return CCW_OK;
}

ccw_status
ccw_ir_instr_add_operand (ccw_ir *ir, ccw_node ins, ccw_node operand)
{
  ccw_ir_node *n = ccw_ir_node_get_kind (ir, ins, CCW_NODE_INSTR);
  ccw_ir_node *o = ccw_ir_node_get_kind (ir, operand, CCW_NODE_OPERAND);
  if (n == NULL || o == NULL)
    return CCW_ERR_TYPE;
  ccw_node_vec_push (&n->children, operand);
  return CCW_OK;
}

ccw_status
ccw_ir_instr_set_operand (ccw_ir *ir, ccw_node ins, int index,
                          ccw_node operand)
{
  ccw_ir_node *instruction = ccw_ir_node_get_kind (ir, ins, CCW_NODE_INSTR);
  ccw_ir_node *new_operand
      = ccw_ir_node_get_kind (ir, operand, CCW_NODE_OPERAND);
  if (instruction == NULL || new_operand == NULL || index < 0
      || index >= instruction->children.count)
    return CCW_ERR_TYPE;
  instruction->children.items[index] = operand;
  return CCW_OK;
}

ccw_status
ccw_ir_block_append_instr (ccw_ir *ir, ccw_node blk, ccw_node ins)
{
  ccw_ir_node *b = ccw_ir_node_get_kind (ir, blk, CCW_NODE_BLOCK);
  ccw_ir_node *n = ccw_ir_node_get_kind (ir, ins, CCW_NODE_INSTR);
  if (b == NULL || n == NULL || n->attached)
    return CCW_ERR_TYPE;
  n->parent = blk;
  n->attached = true;
  ccw_node_vec_push (&b->children, ins);
  return CCW_OK;
}

static ccw_node
ccw_operand_new (ccw_ir *ir, ccw_operand_kind kind, const char *name)
{
  if (ir == NULL)
    return 0;
  ccw_ir_node *n = ccw_ir_node_new (ir, CCW_NODE_OPERAND);
  if (n == NULL)
    return 0;
  n->okind = kind;
  n->name = name ? ccw_ir_strdup (name) : NULL;
  return n->id;
}

ccw_node
ccw_ir_operand_reg (ccw_ir *ir, const char *name)
{
  return name ? ccw_operand_new (ir, CCW_OPND_REG, name) : 0;
}

ccw_node
ccw_ir_operand_func (ccw_ir *ir, const char *name)
{
  return name ? ccw_operand_new (ir, CCW_OPND_FUNC, name) : 0;
}

ccw_node
ccw_ir_operand_block (ccw_ir *ir, const char *name)
{
  return name ? ccw_operand_new (ir, CCW_OPND_BLOCK, name) : 0;
}

ccw_node
ccw_ir_operand_const_int (ccw_ir *ir, ccw_ir_type type, int64_t value)
{
  ccw_node id = ccw_operand_new (ir, CCW_OPND_CONST_INT, NULL);
  ccw_ir_node *n = ccw_ir_node_get (ir, id);
  if (n == NULL)
    return 0;
  n->type = type;
  n->ival = value;
  return id;
}

ccw_node
ccw_ir_operand_const_float (ccw_ir *ir, ccw_ir_type type, double value)
{
  ccw_node id = ccw_operand_new (ir, CCW_OPND_CONST_FLOAT, NULL);
  ccw_ir_node *n = ccw_ir_node_get (ir, id);
  if (n == NULL)
    return 0;
  n->type = type;
  n->fval = value;
  return id;
}

/* ---------- navigation / inspection ---------- */

int
ccw_ir_function_count (const ccw_ir *ir)
{
  return ir ? ir->functions.count : 0;
}

ccw_node
ccw_ir_function_ref (const ccw_ir *ir, int idx)
{
  if (ir == NULL || idx < 0 || idx >= ir->functions.count)
    return 0;
  return ir->functions.items[idx];
}

const char *
ccw_ir_function_name (const ccw_ir *ir, ccw_node fn)
{
  ccw_ir_node *n = ccw_ir_node_get_kind (ir, fn, CCW_NODE_FUNCTION);
  return n ? n->name : NULL;
}

ccw_ir_type
ccw_ir_function_result (const ccw_ir *ir, ccw_node fn)
{
  ccw_ir_node *n = ccw_ir_node_get_kind (ir, fn, CCW_NODE_FUNCTION);
  return n ? n->type : CCW_TY_VOID;
}

int
ccw_ir_function_param_count (const ccw_ir *ir, ccw_node fn)
{
  ccw_ir_node *n = ccw_ir_node_get_kind (ir, fn, CCW_NODE_FUNCTION);
  return n ? n->param_types.count : 0;
}

const char *
ccw_ir_function_param_name (const ccw_ir *ir, ccw_node fn, int idx)
{
  ccw_ir_node *n = ccw_ir_node_get_kind (ir, fn, CCW_NODE_FUNCTION);
  if (n == NULL || idx < 0 || idx >= n->param_types.count)
    return NULL;
  ccw_ir_node *p = ccw_ir_node_get (ir, n->param_types.items[idx]);
  return p ? p->name : NULL;
}

ccw_ir_type
ccw_ir_function_param_type (const ccw_ir *ir, ccw_node fn, int idx)
{
  ccw_ir_node *n = ccw_ir_node_get_kind (ir, fn, CCW_NODE_FUNCTION);
  if (n == NULL || idx < 0 || idx >= n->param_types.count)
    return CCW_TY_VOID;
  ccw_ir_node *p = ccw_ir_node_get (ir, n->param_types.items[idx]);
  return p ? p->type : CCW_TY_VOID;
}

int
ccw_ir_function_block_count (const ccw_ir *ir, ccw_node fn)
{
  ccw_ir_node *n = ccw_ir_node_get_kind (ir, fn, CCW_NODE_FUNCTION);
  return n ? n->children.count : 0;
}

ccw_node
ccw_ir_function_block_ref (const ccw_ir *ir, ccw_node fn, int idx)
{
  ccw_ir_node *n = ccw_ir_node_get_kind (ir, fn, CCW_NODE_FUNCTION);
  if (n == NULL || idx < 0 || idx >= n->children.count)
    return 0;
  return n->children.items[idx];
}

const char *
ccw_ir_block_name (const ccw_ir *ir, ccw_node blk)
{
  ccw_ir_node *n = ccw_ir_node_get_kind (ir, blk, CCW_NODE_BLOCK);
  return n ? n->name : NULL;
}

int
ccw_ir_block_instr_count (const ccw_ir *ir, ccw_node blk)
{
  ccw_ir_node *n = ccw_ir_node_get_kind (ir, blk, CCW_NODE_BLOCK);
  return n ? n->children.count : 0;
}

ccw_node
ccw_ir_block_instr_ref (const ccw_ir *ir, ccw_node blk, int idx)
{
  ccw_ir_node *n = ccw_ir_node_get_kind (ir, blk, CCW_NODE_BLOCK);
  if (n == NULL || idx < 0 || idx >= n->children.count)
    return 0;
  return n->children.items[idx];
}

static ccw_node
ccw_ir_block_target (const ccw_ir *ir, ccw_node function, const char *name)
{
  ccw_ir_node *fn = ccw_ir_node_get_kind (ir, function, CCW_NODE_FUNCTION);
  if (fn == NULL || name == NULL)
    return 0;
  for (int i = 0; i < fn->children.count; i++)
    {
      ccw_node block = fn->children.items[i];
      const char *block_name = ccw_ir_block_name (ir, block);
      if (block_name != NULL && strcmp (block_name, name) == 0)
        return block;
    }
  return 0;
}

static int
ccw_ir_block_successors (const ccw_ir *ir, ccw_node blk, ccw_node *out,
                         int capacity)
{
  ccw_ir_node *block = ccw_ir_node_get_kind (ir, blk, CCW_NODE_BLOCK);
  if (block == NULL || block->children.count == 0)
    return 0;
  ccw_ir_node *instruction = NULL;
  for (int index = block->children.count - 1; index >= 0; index--)
    {
      ccw_ir_node *candidate = ccw_ir_node_get_kind (
          ir, block->children.items[index], CCW_NODE_INSTR);
      if (candidate == NULL)
        continue;
      for (int operand_index = 0; operand_index < candidate->children.count;
           operand_index++)
        {
          if (ccw_ir_operand_kind (ir,
                                   candidate->children.items[operand_index])
              == CCW_OPND_BLOCK)
            {
              instruction = candidate;
              break;
            }
        }
      if (instruction != NULL)
        break;
    }
  if (instruction == NULL)
    return 0;
  int count = 0;
  for (int i = 0; i < instruction->children.count; i++)
    {
      ccw_node operand = instruction->children.items[i];
      if (ccw_ir_operand_kind (ir, operand) != CCW_OPND_BLOCK)
        continue;
      ccw_node target = ccw_ir_block_target (
          ir, block->parent, ccw_ir_operand_name (ir, operand));
      if (target == 0)
        continue;
      bool duplicate = false;
      for (int j = 0; j < count; j++)
        if (out != NULL && j < capacity && out[j] == target)
          {
            duplicate = true;
            break;
          }
      if (!duplicate)
        {
          if (out != NULL && count < capacity)
            out[count] = target;
          count++;
        }
    }
  return count;
}

int
ccw_ir_block_successor_count (const ccw_ir *ir, ccw_node blk)
{
  return ccw_ir_block_successors (ir, blk, NULL, 0);
}

ccw_node
ccw_ir_block_successor_ref (const ccw_ir *ir, ccw_node blk, int idx)
{
  ccw_node successors[16];
  int count = ccw_ir_block_successors (
      ir, blk, successors,
      (int)(sizeof (successors) / sizeof (successors[0])));
  return idx >= 0 && idx < count
                 && idx < (int)(sizeof (successors) / sizeof (successors[0]))
             ? successors[idx]
             : 0;
}

int
ccw_ir_block_predecessor_count (const ccw_ir *ir, ccw_node blk)
{
  ccw_ir_node *block = ccw_ir_node_get_kind (ir, blk, CCW_NODE_BLOCK);
  ccw_ir_node *function
      = block ? ccw_ir_node_get_kind (ir, block->parent, CCW_NODE_FUNCTION)
              : NULL;
  if (function == NULL)
    return 0;
  int count = 0;
  for (int i = 0; i < function->children.count; i++)
    {
      ccw_node candidate = function->children.items[i];
      for (int j = 0; j < ccw_ir_block_successor_count (ir, candidate); j++)
        if (ccw_ir_block_successor_ref (ir, candidate, j) == blk)
          count++;
    }
  return count;
}

ccw_node
ccw_ir_block_predecessor_ref (const ccw_ir *ir, ccw_node blk, int idx)
{
  ccw_ir_node *block = ccw_ir_node_get_kind (ir, blk, CCW_NODE_BLOCK);
  ccw_ir_node *function
      = block ? ccw_ir_node_get_kind (ir, block->parent, CCW_NODE_FUNCTION)
              : NULL;
  if (function == NULL || idx < 0)
    return 0;
  int found = 0;
  for (int i = 0; i < function->children.count; i++)
    {
      ccw_node candidate = function->children.items[i];
      for (int j = 0; j < ccw_ir_block_successor_count (ir, candidate); j++)
        {
          if (ccw_ir_block_successor_ref (ir, candidate, j) == blk
              && found++ == idx)
            return candidate;
        }
    }
  return 0;
}

ccw_status
ccw_ir_block_delete (ccw_ir *ir, ccw_node blk)
{
  ccw_ir_node *block = ccw_ir_node_get_kind (ir, blk, CCW_NODE_BLOCK);
  ccw_ir_node *function
      = block ? ccw_ir_node_get_kind (ir, block->parent, CCW_NODE_FUNCTION)
              : NULL;
  if (block == NULL || function == NULL
      || ccw_ir_block_predecessor_count (ir, blk) != 0)
    return CCW_ERR_TYPE;
  int index = ccw_node_vec_index_of (&function->children, blk);
  if (index < 0)
    return CCW_ERR_TYPE;
  for (int i = 0; i < block->children.count; i++)
    {
      ccw_ir_node *instruction
          = ccw_ir_node_get (ir, block->children.items[i]);
      if (instruction != NULL)
        {
          instruction->attached = false;
          instruction->kind = CCW_NODE_DEAD;
        }
    }
  ccw_node_vec_remove_at (&function->children, index);
  block->kind = CCW_NODE_DEAD;
  return CCW_OK;
}

ccw_status
ccw_ir_block_merge (ccw_ir *ir, ccw_node first, ccw_node second)
{
  ccw_ir_node *a = ccw_ir_node_get_kind (ir, first, CCW_NODE_BLOCK);
  ccw_ir_node *b = ccw_ir_node_get_kind (ir, second, CCW_NODE_BLOCK);
  if (a == NULL || b == NULL || a->parent != b->parent
      || ccw_ir_block_successor_count (ir, first) != 1
      || ccw_ir_block_successor_ref (ir, first, 0) != second
      || ccw_ir_block_predecessor_count (ir, second) != 1)
    return CCW_ERR_TYPE;
  if (a->children.count > 0)
    {
      ccw_ir_node *jump
          = ccw_ir_node_get (ir, a->children.items[a->children.count - 1]);
      if (jump != NULL)
        {
          jump->attached = false;
          jump->kind = CCW_NODE_DEAD;
        }
      a->children.count--;
    }
  for (int i = 0; i < b->children.count; i++)
    {
      ccw_ir_node *ins = ccw_ir_node_get (ir, b->children.items[i]);
      if (ins != NULL)
        ins->parent = first;
      ccw_node_vec_push (&a->children, b->children.items[i]);
    }
  ccw_ir_node *fn = ccw_ir_node_get_kind (ir, a->parent, CCW_NODE_FUNCTION);
  ccw_node_vec_remove_at (&fn->children,
                          ccw_node_vec_index_of (&fn->children, second));
  b->kind = CCW_NODE_DEAD;
  return CCW_OK;
}

const char *
ccw_ir_instr_opcode (const ccw_ir *ir, ccw_node ins)
{
  ccw_ir_node *n = ccw_ir_node_get_kind (ir, ins, CCW_NODE_INSTR);
  return n ? n->opcode : NULL;
}

const char *
ccw_ir_instr_dest (const ccw_ir *ir, ccw_node ins)
{
  ccw_ir_node *n = ccw_ir_node_get_kind (ir, ins, CCW_NODE_INSTR);
  return n ? n->name : NULL;
}

ccw_ir_type
ccw_ir_instr_type (const ccw_ir *ir, ccw_node ins)
{
  ccw_ir_node *n = ccw_ir_node_get_kind (ir, ins, CCW_NODE_INSTR);
  return n ? n->type : CCW_TY_VOID;
}

int
ccw_ir_instr_operand_count (const ccw_ir *ir, ccw_node ins)
{
  ccw_ir_node *n = ccw_ir_node_get_kind (ir, ins, CCW_NODE_INSTR);
  return n ? n->children.count : 0;
}

ccw_node
ccw_ir_instr_operand (const ccw_ir *ir, ccw_node ins, int idx)
{
  ccw_ir_node *n = ccw_ir_node_get_kind (ir, ins, CCW_NODE_INSTR);
  if (n == NULL || idx < 0 || idx >= n->children.count)
    return 0;
  return n->children.items[idx];
}

ccw_node_kind
ccw_ir_node_kind (const ccw_ir *ir, ccw_node n)
{
  ccw_ir_node *x = ccw_ir_node_get (ir, n);
  return x ? x->kind : CCW_NODE_DEAD;
}

ccw_operand_kind
ccw_ir_operand_kind (const ccw_ir *ir, ccw_node n)
{
  ccw_ir_node *x = ccw_ir_node_get_kind (ir, n, CCW_NODE_OPERAND);
  return x ? x->okind : CCW_OPND_REG;
}

bool
ccw_ir_operand_is_const (const ccw_ir *ir, ccw_node n)
{
  ccw_ir_node *x = ccw_ir_node_get_kind (ir, n, CCW_NODE_OPERAND);
  if (x == NULL)
    return false;
  return x->okind == CCW_OPND_CONST_INT || x->okind == CCW_OPND_CONST_FLOAT;
}

ccw_status
ccw_ir_const_int_value (const ccw_ir *ir, ccw_node n, int64_t *out)
{
  ccw_ir_node *x = ccw_ir_node_get_kind (ir, n, CCW_NODE_OPERAND);
  if (x == NULL || x->okind != CCW_OPND_CONST_INT)
    return CCW_ERR_TYPE;
  if (out)
    *out = x->ival;
  return CCW_OK;
}

ccw_status
ccw_ir_const_float_value (const ccw_ir *ir, ccw_node n, double *out)
{
  ccw_ir_node *x = ccw_ir_node_get_kind (ir, n, CCW_NODE_OPERAND);
  if (x == NULL || x->okind != CCW_OPND_CONST_FLOAT)
    return CCW_ERR_TYPE;
  if (out)
    *out = x->fval;
  return CCW_OK;
}

const char *
ccw_ir_operand_name (const ccw_ir *ir, ccw_node n)
{
  ccw_ir_node *x = ccw_ir_node_get_kind (ir, n, CCW_NODE_OPERAND);
  return x ? x->name : NULL;
}

ccw_ir_type
ccw_ir_operand_type (const ccw_ir *ir, ccw_node n)
{
  ccw_ir_node *x = ccw_ir_node_get_kind (ir, n, CCW_NODE_OPERAND);
  return x ? x->type : CCW_TY_VOID;
}

/* ---------- mutation: replace / insert-before / delete ---------- */

ccw_status
ccw_ir_instr_replace (ccw_ir *ir, ccw_node old_ins, ccw_node new_ins)
{
  ccw_ir_node *o = ccw_ir_node_get_kind (ir, old_ins, CCW_NODE_INSTR);
  ccw_ir_node *n = ccw_ir_node_get_kind (ir, new_ins, CCW_NODE_INSTR);
  if (o == NULL || n == NULL || !o->attached || n->attached)
    return CCW_ERR_TYPE;
  ccw_ir_node *b = ccw_ir_node_get_kind (ir, o->parent, CCW_NODE_BLOCK);
  if (b == NULL)
    return CCW_ERR_TYPE;
  int idx = ccw_node_vec_index_of (&b->children, old_ins);
  if (idx < 0)
    return CCW_ERR_TYPE;
  /* A replacement with no dest inherits the old one, so uses stay valid. */
  if (n->name == NULL && o->name != NULL)
    n->name = ccw_ir_strdup (o->name);
  b->children.items[idx] = new_ins;
  n->parent = o->parent;
  n->attached = true;
  o->attached = false;
  o->parent = 0;
  return CCW_OK;
}

ccw_status
ccw_ir_instr_insert_before (ccw_ir *ir, ccw_node anchor, ccw_node new_ins)
{
  ccw_ir_node *a = ccw_ir_node_get_kind (ir, anchor, CCW_NODE_INSTR);
  ccw_ir_node *n = ccw_ir_node_get_kind (ir, new_ins, CCW_NODE_INSTR);
  if (a == NULL || n == NULL || !a->attached || n->attached)
    return CCW_ERR_TYPE;
  ccw_ir_node *b = ccw_ir_node_get_kind (ir, a->parent, CCW_NODE_BLOCK);
  if (b == NULL)
    return CCW_ERR_TYPE;
  int idx = ccw_node_vec_index_of (&b->children, anchor);
  if (idx < 0)
    return CCW_ERR_TYPE;
  ccw_node_vec_insert_at (&b->children, idx, new_ins);
  n->parent = a->parent;
  n->attached = true;
  return CCW_OK;
}

ccw_status
ccw_ir_instr_delete (ccw_ir *ir, ccw_node ins)
{
  ccw_ir_node *n = ccw_ir_node_get_kind (ir, ins, CCW_NODE_INSTR);
  if (n == NULL || !n->attached)
    return CCW_ERR_TYPE;
  ccw_ir_node *b = ccw_ir_node_get_kind (ir, n->parent, CCW_NODE_BLOCK);
  if (b == NULL)
    return CCW_ERR_TYPE;
  int idx = ccw_node_vec_index_of (&b->children, ins);
  if (idx < 0)
    return CCW_ERR_TYPE;
  ccw_node_vec_remove_at (&b->children, idx);
  n->attached = false;
  n->parent = 0;
  return CCW_OK;
}

/* ---------- structural equality (ids excluded) ---------- */

static bool
ccw_streq (const char *a, const char *b)
{
  if (a == NULL || b == NULL)
    return a == b;
  return strcmp (a, b) == 0;
}

static bool
ccw_attrs_equal (const ccw_attrs *a, const ccw_attrs *b)
{
  if (a->count != b->count)
    return false;
  for (int i = 0; i < a->count; i++)
    if (!ccw_streq (a->items[i].key, b->items[i].key)
        || !ccw_streq (a->items[i].value, b->items[i].value))
      return false;
  return true;
}

static bool
ccw_operand_equal (const ccw_ir *ia, ccw_node na, const ccw_ir *ib,
                   ccw_node nb)
{
  ccw_ir_node *a = ccw_ir_node_get_kind (ia, na, CCW_NODE_OPERAND);
  ccw_ir_node *b = ccw_ir_node_get_kind (ib, nb, CCW_NODE_OPERAND);
  if (a == NULL || b == NULL)
    return a == b;
  if (a->okind != b->okind || a->type != b->type)
    return false;
  if (a->okind == CCW_OPND_CONST_INT)
    return a->ival == b->ival;
  if (a->okind == CCW_OPND_CONST_FLOAT)
    return a->fval == b->fval;
  return ccw_streq (a->name, b->name);
}

static bool
ccw_instr_equal (const ccw_ir *ia, ccw_node na, const ccw_ir *ib, ccw_node nb)
{
  ccw_ir_node *a = ccw_ir_node_get_kind (ia, na, CCW_NODE_INSTR);
  ccw_ir_node *b = ccw_ir_node_get_kind (ib, nb, CCW_NODE_INSTR);
  if (a == NULL || b == NULL)
    return a == b;
  if (!ccw_streq (a->opcode, b->opcode) || !ccw_streq (a->name, b->name))
    return false;
  if (a->type != b->type)
    return false;
  if (a->children.count != b->children.count)
    return false;
  if (!ccw_attrs_equal (&a->attrs, &b->attrs))
    return false;
  for (int i = 0; i < a->children.count; i++)
    if (!ccw_operand_equal (ia, a->children.items[i], ib,
                            b->children.items[i]))
      return false;
  return true;
}

bool
ccw_ir_equal (const ccw_ir *a, const ccw_ir *b)
{
  if (a == NULL || b == NULL)
    return a == b;
  if (a->profile != b->profile || !ccw_streq (a->name, b->name))
    return false;
  if (!ccw_attrs_equal (&a->attrs, &b->attrs))
    return false;
  if (a->functions.count != b->functions.count)
    return false;
  for (int fi = 0; fi < a->functions.count; fi++)
    {
      ccw_ir_node *fa = ccw_ir_node_get (a, a->functions.items[fi]);
      ccw_ir_node *fb = ccw_ir_node_get (b, b->functions.items[fi]);
      if (fa == NULL || fb == NULL)
        return false;
      if (!ccw_streq (fa->name, fb->name) || fa->type != fb->type)
        return false;
      if (!ccw_attrs_equal (&fa->attrs, &fb->attrs))
        return false;
      if (fa->param_types.count != fb->param_types.count)
        return false;
      for (int pi = 0; pi < fa->param_types.count; pi++)
        if (!ccw_operand_equal (a, fa->param_types.items[pi], b,
                                fb->param_types.items[pi]))
          return false;
      if (fa->children.count != fb->children.count)
        return false;
      for (int bi = 0; bi < fa->children.count; bi++)
        {
          ccw_ir_node *ba = ccw_ir_node_get (a, fa->children.items[bi]);
          ccw_ir_node *bb = ccw_ir_node_get (b, fb->children.items[bi]);
          if (ba == NULL || bb == NULL)
            return false;
          if (!ccw_streq (ba->name, bb->name))
            return false;
          if (!ccw_attrs_equal (&ba->attrs, &bb->attrs))
            return false;
          if (ba->children.count != bb->children.count)
            return false;
          for (int ii = 0; ii < ba->children.count; ii++)
            if (!ccw_instr_equal (a, ba->children.items[ii], b,
                                  bb->children.items[ii]))
              return false;
        }
    }
  return true;
}
