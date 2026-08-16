/* Weave IR core: canonical in-memory representation and programmatic API. */

#include "ccw_ir_internal.h"

#include <stdlib.h>
#include <string.h>

/* ---------- small utilities ---------- */

char *ccw_ir_strdup(const char *s)
{
    if (s == NULL) return NULL;
    size_t n = strlen(s) + 1u;
    char *p = (char *)malloc(n);
    if (p != NULL) memcpy(p, s, n);
    return p;
}

void ccw_node_vec_push(ccw_node_vec *v, ccw_node n)
{
    if (v->count == v->cap) {
        int cap = v->cap ? v->cap * 2 : 4;
        ccw_node *items = (ccw_node *)realloc(v->items, (size_t)cap * sizeof(*items));
        if (items == NULL) return;
        v->items = items;
        v->cap = cap;
    }
    v->items[v->count++] = n;
}

static void ccw_node_vec_free(ccw_node_vec *v)
{
    free(v->items);
    v->items = NULL;
    v->count = v->cap = 0;
}

static void ccw_node_vec_remove_at(ccw_node_vec *v, int idx)
{
    if (idx < 0 || idx >= v->count) return;
    memmove(&v->items[idx], &v->items[idx + 1],
            (size_t)(v->count - idx - 1) * sizeof(*v->items));
    v->count--;
}

static void ccw_node_vec_insert_at(ccw_node_vec *v, int idx, ccw_node n)
{
    ccw_node_vec_push(v, 0);
    memmove(&v->items[idx + 1], &v->items[idx],
            (size_t)(v->count - idx - 1) * sizeof(*v->items));
    v->items[idx] = n;
}

static int ccw_node_vec_index_of(const ccw_node_vec *v, ccw_node n)
{
    for (int i = 0; i < v->count; i++)
        if (v->items[i] == n) return i;
    return -1;
}

static void ccw_attrs_free(ccw_attrs *a)
{
    for (int i = 0; i < a->count; i++) {
        free(a->items[i].key);
        free(a->items[i].value);
    }
    free(a->items);
    a->items = NULL;
    a->count = a->cap = 0;
}

static ccw_status ccw_attrs_set(ccw_attrs *a, const char *key, const char *value)
{
    if (key == NULL || value == NULL) return CCW_ERR_TYPE;
    for (int i = 0; i < a->count; i++) {
        if (strcmp(a->items[i].key, key) == 0) {
            char *v = ccw_ir_strdup(value);
            if (v == NULL) return CCW_ERR_OOM;
            free(a->items[i].value);
            a->items[i].value = v;
            return CCW_OK;
        }
    }
    if (a->count == a->cap) {
        int cap = a->cap ? a->cap * 2 : 4;
        ccw_attr *items = (ccw_attr *)realloc(a->items, (size_t)cap * sizeof(*items));
        if (items == NULL) return CCW_ERR_OOM;
        a->items = items;
        a->cap = cap;
    }
    char *k = ccw_ir_strdup(key);
    char *v = ccw_ir_strdup(value);
    if (k == NULL || v == NULL) { free(k); free(v); return CCW_ERR_OOM; }
    a->items[a->count].key = k;
    a->items[a->count].value = v;
    a->count++;
    return CCW_OK;
}

/* ---------- profiles and types ---------- */

const char *ccw_profile_name(ccw_profile p)
{
    return p == CCW_PROFILE_ON1X ? "on1x" : "tilly";
}

bool ccw_profile_parse(const char *name, ccw_profile *out)
{
    if (name == NULL) return false;
    if (strcmp(name, "tilly") == 0) { if (out) *out = CCW_PROFILE_TILLY; return true; }
    if (strcmp(name, "on1x") == 0)  { if (out) *out = CCW_PROFILE_ON1X;  return true; }
    return false;
}

static const char *const ccw_type_names[] = {
    "void", "i1", "i8", "i16", "i32", "i64", "f32", "f64", "ptr"
};

const char *ccw_ir_type_name(ccw_ir_type t)
{
    if ((int)t < 0 || (size_t)t >= sizeof(ccw_type_names) / sizeof(ccw_type_names[0]))
        return "void";
    return ccw_type_names[t];
}

bool ccw_ir_type_parse(const char *name, ccw_ir_type *out)
{
    if (name == NULL) return false;
    for (size_t i = 0; i < sizeof(ccw_type_names) / sizeof(ccw_type_names[0]); i++) {
        if (strcmp(name, ccw_type_names[i]) == 0) {
            if (out) *out = (ccw_ir_type)i;
            return true;
        }
    }
    return false;
}

/* ---------- node table ---------- */

ccw_ir_node *ccw_ir_node_get(const ccw_ir *ir, ccw_node id)
{
    if (ir == NULL || id == 0 || id >= ir->node_count) return NULL;
    ccw_ir_node *n = &ir->nodes[id];
    return n->kind == CCW_NODE_DEAD ? NULL : n;
}

ccw_ir_node *ccw_ir_node_get_kind(const ccw_ir *ir, ccw_node id, ccw_node_kind k)
{
    ccw_ir_node *n = ccw_ir_node_get(ir, id);
    return (n != NULL && n->kind == k) ? n : NULL;
}

static ccw_ir_node *ccw_ir_node_new(ccw_ir *ir, ccw_node_kind kind)
{
    if (ir->node_count == ir->node_cap) {
        size_t cap = ir->node_cap ? ir->node_cap * 2 : 16;
        ccw_ir_node *nodes = (ccw_ir_node *)realloc(ir->nodes, cap * sizeof(*nodes));
        if (nodes == NULL) return NULL;
        ir->nodes = nodes;
        ir->node_cap = cap;
    }
    ccw_ir_node *n = &ir->nodes[ir->node_count];
    memset(n, 0, sizeof(*n));
    n->kind = kind;
    n->id = (ccw_node)ir->node_count;
    ir->node_count++;
    return n;
}

/* ---------- module lifecycle ---------- */

ccw_ir *ccw_ir_module_create(const char *name, ccw_profile profile)
{
    ccw_ir *ir = (ccw_ir *)calloc(1, sizeof(*ir));
    if (ir == NULL) return NULL;
    ir->name = ccw_ir_strdup(name ? name : "module");
    ir->profile = profile;
    /* Id 0 is nil, so slot zero is permanently dead. */
    if (ir->name == NULL || ccw_ir_node_new(ir, CCW_NODE_DEAD) == NULL) {
        ccw_ir_module_destroy(ir);
        return NULL;
    }
    return ir;
}

void ccw_ir_module_destroy(ccw_ir *ir)
{
    if (ir == NULL) return;
    for (size_t i = 0; i < ir->node_count; i++) {
        ccw_ir_node *n = &ir->nodes[i];
        free(n->name);
        free(n->opcode);
        ccw_node_vec_free(&n->children);
        ccw_node_vec_free(&n->param_types);
        ccw_attrs_free(&n->attrs);
    }
    free(ir->nodes);
    ccw_node_vec_free(&ir->functions);
    ccw_attrs_free(&ir->attrs);
    free(ir->name);
    free(ir);
}

const char *ccw_ir_module_name(const ccw_ir *ir)
{
    return ir ? ir->name : NULL;
}

ccw_profile ccw_ir_module_profile(const ccw_ir *ir)
{
    return ir ? ir->profile : CCW_PROFILE_TILLY;
}

/* ---------- attributes ---------- */

static ccw_attrs *ccw_attrs_of(const ccw_ir *ir, ccw_node owner)
{
    if (ir == NULL) return NULL;
    if (owner == 0) return (ccw_attrs *)&ir->attrs;
    ccw_ir_node *n = ccw_ir_node_get(ir, owner);
    return n ? &n->attrs : NULL;
}

ccw_status ccw_ir_attr_set(ccw_ir *ir, ccw_node owner, const char *key, const char *value)
{
    ccw_attrs *a = ccw_attrs_of(ir, owner);
    if (a == NULL) return CCW_ERR_TYPE;
    return ccw_attrs_set(a, key, value);
}

int ccw_ir_attr_count(const ccw_ir *ir, ccw_node owner)
{
    ccw_attrs *a = ccw_attrs_of(ir, owner);
    return a ? a->count : 0;
}

const char *ccw_ir_attr_key(const ccw_ir *ir, ccw_node owner, int idx)
{
    ccw_attrs *a = ccw_attrs_of(ir, owner);
    if (a == NULL || idx < 0 || idx >= a->count) return NULL;
    return a->items[idx].key;
}

const char *ccw_ir_attr_value(const ccw_ir *ir, ccw_node owner, int idx)
{
    ccw_attrs *a = ccw_attrs_of(ir, owner);
    if (a == NULL || idx < 0 || idx >= a->count) return NULL;
    return a->items[idx].value;
}

const char *ccw_ir_attr_lookup(const ccw_ir *ir, ccw_node owner, const char *key)
{
    ccw_attrs *a = ccw_attrs_of(ir, owner);
    if (a == NULL || key == NULL) return NULL;
    for (int i = 0; i < a->count; i++)
        if (strcmp(a->items[i].key, key) == 0) return a->items[i].value;
    return NULL;
}

/* ---------- construction ---------- */

ccw_node ccw_ir_function_add(ccw_ir *ir, const char *name, ccw_ir_type result)
{
    if (ir == NULL || name == NULL) return 0;
    ccw_ir_node *n = ccw_ir_node_new(ir, CCW_NODE_FUNCTION);
    if (n == NULL) return 0;
    n->name = ccw_ir_strdup(name);
    n->type = result;
    ccw_node_vec_push(&ir->functions, n->id);
    return n->id;
}

ccw_status ccw_ir_function_add_param(ccw_ir *ir, ccw_node fn,
                                     ccw_ir_type type, const char *name)
{
    if (ccw_ir_node_get_kind(ir, fn, CCW_NODE_FUNCTION) == NULL || name == NULL)
        return CCW_ERR_TYPE;
    ccw_node p = ccw_ir_operand_reg(ir, name);
    if (p == 0) return CCW_ERR_OOM;
    /* Operand creation may realloc the node table; re-resolve. */
    ccw_ir_node *f = ccw_ir_node_get_kind(ir, fn, CCW_NODE_FUNCTION);
    ccw_ir_node *pn = ccw_ir_node_get(ir, p);
    if (f == NULL || pn == NULL) return CCW_ERR_TYPE;
    pn->type = type;
    ccw_node_vec_push(&f->param_types, p);
    return CCW_OK;
}

ccw_node ccw_ir_block_add(ccw_ir *ir, ccw_node fn, const char *name)
{
    if (ccw_ir_node_get_kind(ir, fn, CCW_NODE_FUNCTION) == NULL || name == NULL)
        return 0;
    ccw_ir_node *b = ccw_ir_node_new(ir, CCW_NODE_BLOCK);
    if (b == NULL) return 0;
    b->name = ccw_ir_strdup(name);
    b->parent = fn;
    ccw_node bid = b->id;
    ccw_ir_node *f = ccw_ir_node_get_kind(ir, fn, CCW_NODE_FUNCTION);
    ccw_node_vec_push(&f->children, bid);
    return bid;
}

ccw_node ccw_ir_instr_build(ccw_ir *ir, const char *opcode, ccw_ir_type type)
{
    if (ir == NULL || opcode == NULL) return 0;
    ccw_ir_node *n = ccw_ir_node_new(ir, CCW_NODE_INSTR);
    if (n == NULL) return 0;
    n->opcode = ccw_ir_strdup(opcode);
    n->type = type;
    n->attached = false;
    return n->id;
}

ccw_status ccw_ir_instr_set_dest(ccw_ir *ir, ccw_node ins, const char *dest)
{
    ccw_ir_node *n = ccw_ir_node_get_kind(ir, ins, CCW_NODE_INSTR);
    if (n == NULL) return CCW_ERR_TYPE;
    char *copy = dest ? ccw_ir_strdup(dest) : NULL;
    if (dest != NULL && copy == NULL) return CCW_ERR_OOM;
    free(n->name);
    n->name = copy;
    return CCW_OK;
}

ccw_status ccw_ir_instr_add_operand(ccw_ir *ir, ccw_node ins, ccw_node operand)
{
    ccw_ir_node *n = ccw_ir_node_get_kind(ir, ins, CCW_NODE_INSTR);
    ccw_ir_node *o = ccw_ir_node_get_kind(ir, operand, CCW_NODE_OPERAND);
    if (n == NULL || o == NULL) return CCW_ERR_TYPE;
    ccw_node_vec_push(&n->children, operand);
    return CCW_OK;
}

ccw_status ccw_ir_block_append_instr(ccw_ir *ir, ccw_node blk, ccw_node ins)
{
    ccw_ir_node *b = ccw_ir_node_get_kind(ir, blk, CCW_NODE_BLOCK);
    ccw_ir_node *n = ccw_ir_node_get_kind(ir, ins, CCW_NODE_INSTR);
    if (b == NULL || n == NULL || n->attached) return CCW_ERR_TYPE;
    n->parent = blk;
    n->attached = true;
    ccw_node_vec_push(&b->children, ins);
    return CCW_OK;
}

static ccw_node ccw_operand_new(ccw_ir *ir, ccw_operand_kind kind, const char *name)
{
    if (ir == NULL) return 0;
    ccw_ir_node *n = ccw_ir_node_new(ir, CCW_NODE_OPERAND);
    if (n == NULL) return 0;
    n->okind = kind;
    n->name = name ? ccw_ir_strdup(name) : NULL;
    return n->id;
}

ccw_node ccw_ir_operand_reg(ccw_ir *ir, const char *name)
{
    return name ? ccw_operand_new(ir, CCW_OPND_REG, name) : 0;
}

ccw_node ccw_ir_operand_func(ccw_ir *ir, const char *name)
{
    return name ? ccw_operand_new(ir, CCW_OPND_FUNC, name) : 0;
}

ccw_node ccw_ir_operand_block(ccw_ir *ir, const char *name)
{
    return name ? ccw_operand_new(ir, CCW_OPND_BLOCK, name) : 0;
}

ccw_node ccw_ir_operand_const_int(ccw_ir *ir, ccw_ir_type type, int64_t value)
{
    ccw_node id = ccw_operand_new(ir, CCW_OPND_CONST_INT, NULL);
    ccw_ir_node *n = ccw_ir_node_get(ir, id);
    if (n == NULL) return 0;
    n->type = type;
    n->ival = value;
    return id;
}

ccw_node ccw_ir_operand_const_float(ccw_ir *ir, ccw_ir_type type, double value)
{
    ccw_node id = ccw_operand_new(ir, CCW_OPND_CONST_FLOAT, NULL);
    ccw_ir_node *n = ccw_ir_node_get(ir, id);
    if (n == NULL) return 0;
    n->type = type;
    n->fval = value;
    return id;
}

/* ---------- navigation / inspection ---------- */

int ccw_ir_function_count(const ccw_ir *ir)
{
    return ir ? ir->functions.count : 0;
}

ccw_node ccw_ir_function_ref(const ccw_ir *ir, int idx)
{
    if (ir == NULL || idx < 0 || idx >= ir->functions.count) return 0;
    return ir->functions.items[idx];
}

const char *ccw_ir_function_name(const ccw_ir *ir, ccw_node fn)
{
    ccw_ir_node *n = ccw_ir_node_get_kind(ir, fn, CCW_NODE_FUNCTION);
    return n ? n->name : NULL;
}

ccw_ir_type ccw_ir_function_result(const ccw_ir *ir, ccw_node fn)
{
    ccw_ir_node *n = ccw_ir_node_get_kind(ir, fn, CCW_NODE_FUNCTION);
    return n ? n->type : CCW_TY_VOID;
}

int ccw_ir_function_param_count(const ccw_ir *ir, ccw_node fn)
{
    ccw_ir_node *n = ccw_ir_node_get_kind(ir, fn, CCW_NODE_FUNCTION);
    return n ? n->param_types.count : 0;
}

const char *ccw_ir_function_param_name(const ccw_ir *ir, ccw_node fn, int idx)
{
    ccw_ir_node *n = ccw_ir_node_get_kind(ir, fn, CCW_NODE_FUNCTION);
    if (n == NULL || idx < 0 || idx >= n->param_types.count) return NULL;
    ccw_ir_node *p = ccw_ir_node_get(ir, n->param_types.items[idx]);
    return p ? p->name : NULL;
}

ccw_ir_type ccw_ir_function_param_type(const ccw_ir *ir, ccw_node fn, int idx)
{
    ccw_ir_node *n = ccw_ir_node_get_kind(ir, fn, CCW_NODE_FUNCTION);
    if (n == NULL || idx < 0 || idx >= n->param_types.count) return CCW_TY_VOID;
    ccw_ir_node *p = ccw_ir_node_get(ir, n->param_types.items[idx]);
    return p ? p->type : CCW_TY_VOID;
}

int ccw_ir_function_block_count(const ccw_ir *ir, ccw_node fn)
{
    ccw_ir_node *n = ccw_ir_node_get_kind(ir, fn, CCW_NODE_FUNCTION);
    return n ? n->children.count : 0;
}

ccw_node ccw_ir_function_block_ref(const ccw_ir *ir, ccw_node fn, int idx)
{
    ccw_ir_node *n = ccw_ir_node_get_kind(ir, fn, CCW_NODE_FUNCTION);
    if (n == NULL || idx < 0 || idx >= n->children.count) return 0;
    return n->children.items[idx];
}

const char *ccw_ir_block_name(const ccw_ir *ir, ccw_node blk)
{
    ccw_ir_node *n = ccw_ir_node_get_kind(ir, blk, CCW_NODE_BLOCK);
    return n ? n->name : NULL;
}

int ccw_ir_block_instr_count(const ccw_ir *ir, ccw_node blk)
{
    ccw_ir_node *n = ccw_ir_node_get_kind(ir, blk, CCW_NODE_BLOCK);
    return n ? n->children.count : 0;
}

ccw_node ccw_ir_block_instr_ref(const ccw_ir *ir, ccw_node blk, int idx)
{
    ccw_ir_node *n = ccw_ir_node_get_kind(ir, blk, CCW_NODE_BLOCK);
    if (n == NULL || idx < 0 || idx >= n->children.count) return 0;
    return n->children.items[idx];
}

const char *ccw_ir_instr_opcode(const ccw_ir *ir, ccw_node ins)
{
    ccw_ir_node *n = ccw_ir_node_get_kind(ir, ins, CCW_NODE_INSTR);
    return n ? n->opcode : NULL;
}

const char *ccw_ir_instr_dest(const ccw_ir *ir, ccw_node ins)
{
    ccw_ir_node *n = ccw_ir_node_get_kind(ir, ins, CCW_NODE_INSTR);
    return n ? n->name : NULL;
}

ccw_ir_type ccw_ir_instr_type(const ccw_ir *ir, ccw_node ins)
{
    ccw_ir_node *n = ccw_ir_node_get_kind(ir, ins, CCW_NODE_INSTR);
    return n ? n->type : CCW_TY_VOID;
}

int ccw_ir_instr_operand_count(const ccw_ir *ir, ccw_node ins)
{
    ccw_ir_node *n = ccw_ir_node_get_kind(ir, ins, CCW_NODE_INSTR);
    return n ? n->children.count : 0;
}

ccw_node ccw_ir_instr_operand(const ccw_ir *ir, ccw_node ins, int idx)
{
    ccw_ir_node *n = ccw_ir_node_get_kind(ir, ins, CCW_NODE_INSTR);
    if (n == NULL || idx < 0 || idx >= n->children.count) return 0;
    return n->children.items[idx];
}

ccw_node_kind ccw_ir_node_kind(const ccw_ir *ir, ccw_node n)
{
    ccw_ir_node *x = ccw_ir_node_get(ir, n);
    return x ? x->kind : CCW_NODE_DEAD;
}

ccw_operand_kind ccw_ir_operand_kind(const ccw_ir *ir, ccw_node n)
{
    ccw_ir_node *x = ccw_ir_node_get_kind(ir, n, CCW_NODE_OPERAND);
    return x ? x->okind : CCW_OPND_REG;
}

bool ccw_ir_operand_is_const(const ccw_ir *ir, ccw_node n)
{
    ccw_ir_node *x = ccw_ir_node_get_kind(ir, n, CCW_NODE_OPERAND);
    if (x == NULL) return false;
    return x->okind == CCW_OPND_CONST_INT || x->okind == CCW_OPND_CONST_FLOAT;
}

ccw_status ccw_ir_const_int_value(const ccw_ir *ir, ccw_node n, int64_t *out)
{
    ccw_ir_node *x = ccw_ir_node_get_kind(ir, n, CCW_NODE_OPERAND);
    if (x == NULL || x->okind != CCW_OPND_CONST_INT) return CCW_ERR_TYPE;
    if (out) *out = x->ival;
    return CCW_OK;
}

ccw_status ccw_ir_const_float_value(const ccw_ir *ir, ccw_node n, double *out)
{
    ccw_ir_node *x = ccw_ir_node_get_kind(ir, n, CCW_NODE_OPERAND);
    if (x == NULL || x->okind != CCW_OPND_CONST_FLOAT) return CCW_ERR_TYPE;
    if (out) *out = x->fval;
    return CCW_OK;
}

const char *ccw_ir_operand_name(const ccw_ir *ir, ccw_node n)
{
    ccw_ir_node *x = ccw_ir_node_get_kind(ir, n, CCW_NODE_OPERAND);
    return x ? x->name : NULL;
}

ccw_ir_type ccw_ir_operand_type(const ccw_ir *ir, ccw_node n)
{
    ccw_ir_node *x = ccw_ir_node_get_kind(ir, n, CCW_NODE_OPERAND);
    return x ? x->type : CCW_TY_VOID;
}

/* ---------- mutation: replace / insert-before / delete ---------- */

ccw_status ccw_ir_instr_replace(ccw_ir *ir, ccw_node old_ins, ccw_node new_ins)
{
    ccw_ir_node *o = ccw_ir_node_get_kind(ir, old_ins, CCW_NODE_INSTR);
    ccw_ir_node *n = ccw_ir_node_get_kind(ir, new_ins, CCW_NODE_INSTR);
    if (o == NULL || n == NULL || !o->attached || n->attached) return CCW_ERR_TYPE;
    ccw_ir_node *b = ccw_ir_node_get_kind(ir, o->parent, CCW_NODE_BLOCK);
    if (b == NULL) return CCW_ERR_TYPE;
    int idx = ccw_node_vec_index_of(&b->children, old_ins);
    if (idx < 0) return CCW_ERR_TYPE;
    /* A replacement with no dest inherits the old one, so uses stay valid. */
    if (n->name == NULL && o->name != NULL) n->name = ccw_ir_strdup(o->name);
    b->children.items[idx] = new_ins;
    n->parent = o->parent;
    n->attached = true;
    o->attached = false;
    o->parent = 0;
    return CCW_OK;
}

ccw_status ccw_ir_instr_insert_before(ccw_ir *ir, ccw_node anchor, ccw_node new_ins)
{
    ccw_ir_node *a = ccw_ir_node_get_kind(ir, anchor, CCW_NODE_INSTR);
    ccw_ir_node *n = ccw_ir_node_get_kind(ir, new_ins, CCW_NODE_INSTR);
    if (a == NULL || n == NULL || !a->attached || n->attached) return CCW_ERR_TYPE;
    ccw_ir_node *b = ccw_ir_node_get_kind(ir, a->parent, CCW_NODE_BLOCK);
    if (b == NULL) return CCW_ERR_TYPE;
    int idx = ccw_node_vec_index_of(&b->children, anchor);
    if (idx < 0) return CCW_ERR_TYPE;
    ccw_node_vec_insert_at(&b->children, idx, new_ins);
    n->parent = a->parent;
    n->attached = true;
    return CCW_OK;
}

ccw_status ccw_ir_instr_delete(ccw_ir *ir, ccw_node ins)
{
    ccw_ir_node *n = ccw_ir_node_get_kind(ir, ins, CCW_NODE_INSTR);
    if (n == NULL || !n->attached) return CCW_ERR_TYPE;
    ccw_ir_node *b = ccw_ir_node_get_kind(ir, n->parent, CCW_NODE_BLOCK);
    if (b == NULL) return CCW_ERR_TYPE;
    int idx = ccw_node_vec_index_of(&b->children, ins);
    if (idx < 0) return CCW_ERR_TYPE;
    ccw_node_vec_remove_at(&b->children, idx);
    n->attached = false;
    n->parent = 0;
    return CCW_OK;
}

/* ---------- structural equality (ids excluded) ---------- */

static bool ccw_streq(const char *a, const char *b)
{
    if (a == NULL || b == NULL) return a == b;
    return strcmp(a, b) == 0;
}

static bool ccw_attrs_equal(const ccw_attrs *a, const ccw_attrs *b)
{
    if (a->count != b->count) return false;
    for (int i = 0; i < a->count; i++)
        if (!ccw_streq(a->items[i].key, b->items[i].key) ||
            !ccw_streq(a->items[i].value, b->items[i].value))
            return false;
    return true;
}

static bool ccw_operand_equal(const ccw_ir *ia, ccw_node na,
                              const ccw_ir *ib, ccw_node nb)
{
    ccw_ir_node *a = ccw_ir_node_get_kind(ia, na, CCW_NODE_OPERAND);
    ccw_ir_node *b = ccw_ir_node_get_kind(ib, nb, CCW_NODE_OPERAND);
    if (a == NULL || b == NULL) return a == b;
    if (a->okind != b->okind || a->type != b->type) return false;
    if (a->okind == CCW_OPND_CONST_INT) return a->ival == b->ival;
    if (a->okind == CCW_OPND_CONST_FLOAT) return a->fval == b->fval;
    return ccw_streq(a->name, b->name);
}

static bool ccw_instr_equal(const ccw_ir *ia, ccw_node na,
                            const ccw_ir *ib, ccw_node nb)
{
    ccw_ir_node *a = ccw_ir_node_get_kind(ia, na, CCW_NODE_INSTR);
    ccw_ir_node *b = ccw_ir_node_get_kind(ib, nb, CCW_NODE_INSTR);
    if (a == NULL || b == NULL) return a == b;
    if (!ccw_streq(a->opcode, b->opcode) || !ccw_streq(a->name, b->name)) return false;
    if (a->type != b->type) return false;
    if (a->children.count != b->children.count) return false;
    if (!ccw_attrs_equal(&a->attrs, &b->attrs)) return false;
    for (int i = 0; i < a->children.count; i++)
        if (!ccw_operand_equal(ia, a->children.items[i], ib, b->children.items[i]))
            return false;
    return true;
}

bool ccw_ir_equal(const ccw_ir *a, const ccw_ir *b)
{
    if (a == NULL || b == NULL) return a == b;
    if (a->profile != b->profile || !ccw_streq(a->name, b->name)) return false;
    if (!ccw_attrs_equal(&a->attrs, &b->attrs)) return false;
    if (a->functions.count != b->functions.count) return false;
    for (int fi = 0; fi < a->functions.count; fi++) {
        ccw_ir_node *fa = ccw_ir_node_get(a, a->functions.items[fi]);
        ccw_ir_node *fb = ccw_ir_node_get(b, b->functions.items[fi]);
        if (fa == NULL || fb == NULL) return false;
        if (!ccw_streq(fa->name, fb->name) || fa->type != fb->type) return false;
        if (!ccw_attrs_equal(&fa->attrs, &fb->attrs)) return false;
        if (fa->param_types.count != fb->param_types.count) return false;
        for (int pi = 0; pi < fa->param_types.count; pi++)
            if (!ccw_operand_equal(a, fa->param_types.items[pi],
                                   b, fb->param_types.items[pi]))
                return false;
        if (fa->children.count != fb->children.count) return false;
        for (int bi = 0; bi < fa->children.count; bi++) {
            ccw_ir_node *ba = ccw_ir_node_get(a, fa->children.items[bi]);
            ccw_ir_node *bb = ccw_ir_node_get(b, fb->children.items[bi]);
            if (ba == NULL || bb == NULL) return false;
            if (!ccw_streq(ba->name, bb->name)) return false;
            if (!ccw_attrs_equal(&ba->attrs, &bb->attrs)) return false;
            if (ba->children.count != bb->children.count) return false;
            for (int ii = 0; ii < ba->children.count; ii++)
                if (!ccw_instr_equal(a, ba->children.items[ii],
                                     b, bb->children.items[ii]))
                    return false;
        }
    }
    return true;
}
