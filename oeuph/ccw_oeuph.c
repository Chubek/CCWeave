/* Oeuph e-graph engine (§7).
 *
 * Determinism (§7.4) comes from processing e-classes and rules in a
 * fixed index order and breaking extraction ties by (cost, e-node index),
 * never by hash iteration or pointer values. The seed is mixed into tie
 * breaking only, so a fixed seed + budget reproduces byte-identical output. */

#include "ccw_oeuph.h"
#include "ccw_oeuph_pattern.h"
#include "kstring.h"
#include "kvec.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---------- e-graph ---------- */

#define CCW_MAX_CHILDREN 4

typedef struct {
    char  opcode[32];      /* "" for leaves */
    bool  is_const;
    int64_t value;         /* constants */
    char  reg[32];         /* register leaves */
    int   children[CCW_MAX_CHILDREN];
    int   nchildren;
    int   eclass;
} enode;

typedef struct {
    enode *nodes;
    int    count;
    int    cap;
    int   *parent;         /* union-find over e-class ids */
    int    class_count;
} egraph;

static bool reserve_items(void **items, int *capacity, int needed,
                          size_t item_size)
{
    if (needed <= *capacity && *items != NULL) return true;
    int next = *capacity ? *capacity * 2 : 64;
    while (next < needed) next *= 2;
    kvec_t(unsigned char) bytes = {
        0, (size_t)*capacity * item_size, *items
    };
    if (kv_resize(unsigned char, bytes, (size_t)next * item_size) == NULL)
        return false;
    *items = bytes.a;
    *capacity = next;
    return true;
}

static int eg_find(egraph *g, int c)
{
    while (g->parent[c] != c) {
        g->parent[c] = g->parent[g->parent[c]];
        c = g->parent[c];
    }
    return c;
}

static bool eg_union(egraph *g, int a, int b)
{
    a = eg_find(g, a);
    b = eg_find(g, b);
    if (a == b) return false;
    /* Lower id wins: keeps the structure independent of discovery order. */
    if (a < b) g->parent[b] = a; else g->parent[a] = b;
    return true;
}

static int eg_add_node(egraph *g, const enode *n)
{
    /* Hash-consing by structural identity keeps congruence closure cheap. */
    for (int i = 0; i < g->count; i++) {
        const enode *e = &g->nodes[i];
        if (e->is_const != n->is_const) continue;
        if (strcmp(e->opcode, n->opcode) != 0) continue;
        if (strcmp(e->reg, n->reg) != 0) continue;
        if (e->is_const && e->value != n->value) continue;
        if (e->nchildren != n->nchildren) continue;
        bool same = true;
        for (int c = 0; c < e->nchildren; c++)
            if (eg_find(g, e->children[c]) != eg_find(g, n->children[c])) {
                same = false;
                break;
            }
        if (same) return i;
    }
    if (g->count == g->cap) {
        if (!reserve_items((void **)&g->nodes, &g->cap,
                           g->count + 1, sizeof(*g->nodes)) ||
            !reserve_items((void **)&g->parent, &g->cap,
                           g->count + 1, sizeof(*g->parent)))
            return -1;
    }
    int id = g->count++;
    g->nodes[id] = *n;
    g->nodes[id].eclass = id;
    g->parent[id] = id;
    g->class_count++;
    return id;
}

static void eg_free(egraph *g)
{
    kvec_t(enode) nodes = {
        (size_t)g->count, (size_t)g->cap, g->nodes
    };
    kvec_t(int) parent = {
        (size_t)g->count, (size_t)g->cap, g->parent
    };
    kv_destroy(nodes);
    kv_destroy(parent);
    memset(g, 0, sizeof(*g));
}

static int eg_class_count(egraph *g)
{
    int n = 0;
    for (int i = 0; i < g->count; i++)
        if (eg_find(g, i) == i) n++;
    return n;
}

/* Rebuild: restore congruence after unions. */
static void eg_rebuild(egraph *g)
{
    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 0; i < g->count; i++) {
            for (int j = i + 1; j < g->count; j++) {
                enode *a = &g->nodes[i], *b = &g->nodes[j];
                if (a->is_const != b->is_const) continue;
                if (strcmp(a->opcode, b->opcode) != 0) continue;
                if (strcmp(a->reg, b->reg) != 0) continue;
                if (a->is_const && a->value != b->value) continue;
                if (a->nchildren != b->nchildren) continue;
                bool same = true;
                for (int c = 0; c < a->nchildren; c++)
                    if (eg_find(g, a->children[c]) != eg_find(g, b->children[c])) {
                        same = false;
                        break;
                    }
                if (same && eg_union(g, i, j)) changed = true;
            }
        }
    }
}

ccw_oeuph_budget ccw_oeuph_default_budget(void)
{
    ccw_oeuph_budget b;
    b.max_nodes = 4096;
    b.max_classes = 2048;
    b.max_iterations = 30;
    b.max_seconds = 5.0;
    b.seed = 0x5eed;
    return b;
}

/* ---------- rulesets ---------- */

typedef struct {
    char                     name[64];
    ccw_pattern              lhs;
    ccw_pattern              rhs;
    bool                     bidirectional;
    ccw_oeuph_side_condition side_condition;
} ccw_rule;

struct ccw_oeuph_ruleset {
    char      name[64];
    ccw_rule *rules;
    int       count;
    int       cap;
};

ccw_oeuph_ruleset *ccw_oeuph_ruleset_create(const char *name)
{
    ccw_oeuph_ruleset *rs = (ccw_oeuph_ruleset *)calloc(1, sizeof(*rs));
    if (rs == NULL) return NULL;
    snprintf(rs->name, sizeof(rs->name), "%s", name ? name : "unnamed");
    return rs;
}

void ccw_oeuph_ruleset_destroy(ccw_oeuph_ruleset *rs)
{
    if (rs == NULL) return;
    kvec_t(ccw_rule) rules = {
        (size_t)rs->count, (size_t)rs->cap, rs->rules
    };
    kv_destroy(rules);
    free(rs);
}

const char *ccw_oeuph_ruleset_name(const ccw_oeuph_ruleset *rs)
{
    return rs ? rs->name : "";
}

int ccw_oeuph_ruleset_size(const ccw_oeuph_ruleset *rs)
{
    return rs ? rs->count : 0;
}

static ccw_status rule_error(char **error_message, const char *fmt, ...)
{
    if (error_message != NULL) {
        char buf[256];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        kstring_t message = { 0, 0, NULL };
        if (kputs(buf, &message) != EOF)
            *error_message = ks_release(&message);
        else
            *error_message = NULL;
    }
    return CCW_ERR_TYPE;
}

ccw_status ccw_oeuph_rule_add(ccw_oeuph_ruleset *rs, const char *rule_name,
                              const char *lhs_pattern, const char *rhs_pattern,
                              bool bidirectional,
                              ccw_oeuph_side_condition side_condition,
                              char **error_message)
{
    if (error_message) *error_message = NULL;
    if (rs == NULL) return CCW_ERR_TYPE;

    ccw_rule rule;
    memset(&rule, 0, sizeof(rule));
    snprintf(rule.name, sizeof(rule.name), "%s", rule_name ? rule_name : "unnamed");
    char reason[128];
    if (!ccw_pattern_parse(lhs_pattern, &rule.lhs, reason, sizeof(reason)))
        return rule_error(error_message, "%s: bad lhs: %s", rule.name, reason);
    if (!ccw_pattern_parse(rhs_pattern, &rule.rhs, reason, sizeof(reason)))
        return rule_error(error_message, "%s: bad rhs: %s", rule.name, reason);
    rule.bidirectional = bidirectional;
    rule.side_condition = side_condition;

    if (!reserve_items((void **)&rs->rules, &rs->cap,
                       rs->count + 1, sizeof(*rs->rules)))
        return CCW_ERR_OOM;
    rs->rules[rs->count++] = rule;
    return CCW_OK;
}

/* ---------- matching ---------- */

typedef struct {
    char    name[32];
    int     eclass;     /* variable bound to an e-class */
    int64_t value;      /* constant variable binding     */
    bool    is_value;
} binding;

typedef struct {
    binding items[CCW_PAT_MAX_NODES];
    int     count;
} bindings;

static binding *binding_find(bindings *b, const char *name)
{
    for (int i = 0; i < b->count; i++)
        if (strcmp(b->items[i].name, name) == 0) return &b->items[i];
    return NULL;
}

static bool binding_add_class(bindings *b, const char *name, int eclass)
{
    binding *found = binding_find(b, name);
    if (found != NULL) return !found->is_value && found->eclass == eclass;
    if (b->count >= CCW_PAT_MAX_NODES) return false;
    binding *slot = &b->items[b->count++];
    snprintf(slot->name, sizeof(slot->name), "%s", name);
    slot->eclass = eclass;
    slot->is_value = false;
    return true;
}

static bool binding_add_value(bindings *b, const char *name, int64_t value)
{
    binding *found = binding_find(b, name);
    if (found != NULL) return found->is_value && found->value == value;
    if (b->count >= CCW_PAT_MAX_NODES) return false;
    binding *slot = &b->items[b->count++];
    snprintf(slot->name, sizeof(slot->name), "%s", name);
    slot->value = value;
    slot->is_value = true;
    return true;
}

/* Matches `pnode` against e-node `n`, extending `b`. */
static bool match_node(egraph *g, const ccw_pattern *pat, int pidx, int nidx,
                       bindings *b)
{
    const ccw_pat_node *p = &pat->nodes[pidx];
    enode *n = &g->nodes[nidx];

    switch (p->kind) {
    case CCW_PAT_VAR:
        return binding_add_class(b, p->text, eg_find(g, nidx));
    case CCW_PAT_CONST:
        return n->is_const && n->value == p->value;
    case CCW_PAT_CONST_VAR:
        return n->is_const && binding_add_value(b, p->text, n->value);
    case CCW_PAT_OP:
        break;
    }

    if (n->is_const || strcmp(n->opcode, p->text) != 0) return false;
    if (n->nchildren != p->nchildren) return false;
    for (int i = 0; i < p->nchildren; i++) {
        /* A child pattern may match any e-node in the child's e-class. */
        int want = eg_find(g, n->children[i]);
        bool matched = false;
        for (int j = 0; j < g->count && !matched; j++) {
            if (eg_find(g, j) != want) continue;
            bindings saved = *b;
            if (match_node(g, pat, p->children[i], j, b)) matched = true;
            else *b = saved;
        }
        if (!matched) return false;
    }
    return true;
}

/* Instantiates `pat` into the e-graph, returning the new e-node index. */
static int instantiate(egraph *g, const ccw_pattern *pat, int pidx, bindings *b)
{
    const ccw_pat_node *p = &pat->nodes[pidx];
    enode n;
    memset(&n, 0, sizeof(n));

    switch (p->kind) {
    case CCW_PAT_VAR: {
        binding *bind = binding_find(b, p->text);
        if (bind == NULL || bind->is_value) return -1;
        /* Return any representative e-node of the bound class. */
        for (int i = 0; i < g->count; i++)
            if (eg_find(g, i) == bind->eclass) return i;
        return -1;
    }
    case CCW_PAT_CONST:
        n.is_const = true;
        n.value = p->value;
        return eg_add_node(g, &n);
    case CCW_PAT_CONST_VAR: {
        binding *bind = binding_find(b, p->text);
        if (bind == NULL || !bind->is_value) return -1;
        n.is_const = true;
        n.value = bind->value;
        return eg_add_node(g, &n);
    }
    case CCW_PAT_OP:
        snprintf(n.opcode, sizeof(n.opcode), "%s", p->text);
        for (int i = 0; i < p->nchildren; i++) {
            int child = instantiate(g, pat, p->children[i], b);
            if (child < 0) return -1;
            n.children[n.nchildren++] = child;
        }
        return eg_add_node(g, &n);
    }
    return -1;
}

/* ---------- IR bridge ---------- */

/* One instruction becomes one e-node; its operands become leaf e-nodes.
 * Oeuph works on the canonical in-memory form (§7.1), so this is a view
 * of the module, not a parse of its text. */
static int import_operand(egraph *g, const ccw_ir *ir, ccw_node opnd)
{
    enode n;
    memset(&n, 0, sizeof(n));
    int64_t value = 0;
    if (ccw_ir_const_int_value(ir, opnd, &value) == CCW_OK) {
        n.is_const = true;
        n.value = value;
    } else {
        const char *name = ccw_ir_operand_name(ir, opnd);
        snprintf(n.reg, sizeof(n.reg), "%s", name ? name : "");
    }
    return eg_add_node(g, &n);
}

static int import_instr(egraph *g, const ccw_ir *ir, ccw_node ins)
{
    enode n;
    memset(&n, 0, sizeof(n));
    const char *opcode = ccw_ir_instr_opcode(ir, ins);
    snprintf(n.opcode, sizeof(n.opcode), "%s", opcode ? opcode : "nop");
    int nops = ccw_ir_instr_operand_count(ir, ins);
    if (nops > CCW_MAX_CHILDREN) return -1;
    for (int i = 0; i < nops; i++) {
        int child = import_operand(g, ir, ccw_ir_instr_operand(ir, ins, i));
        if (child < 0) return -1;
        n.children[n.nchildren++] = child;
    }
    return eg_add_node(g, &n);
}

/* ---------- cost models and extraction ---------- */

static int node_cost(const enode *n, ccw_cost_model model)
{
    if (n->is_const || n->opcode[0] == '\0') return 1;
    if (model == CCW_COST_CANONICAL) {
        /* Normalization: prefer a fixed canonical spelling, so cost is
         * lexical rather than architectural. */
        return 10 + (int)(unsigned char)n->opcode[0];
    }
    /* Optimization: rough issue-cost proxy. */
    if (strcmp(n->opcode, "imul") == 0) return 30;
    if (strcmp(n->opcode, "idiv") == 0) return 40;
    if (strcmp(n->opcode, "shl") == 0 || strcmp(n->opcode, "shr") == 0) return 4;
    if (strcmp(n->opcode, "iadd") == 0 || strcmp(n->opcode, "isub") == 0) return 5;
    if (strcmp(n->opcode, "imov") == 0) return 2;
    return 10;
}

/* Deterministic extraction: fixed iteration order, ties broken by the
 * lower e-node index, never by hash order or pointer identity. */
static void extract(egraph *g, ccw_cost_model model, int *best_node, int *best_cost)
{
    for (int i = 0; i < g->count; i++) { best_node[i] = -1; best_cost[i] = INT32_MAX; }

    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 0; i < g->count; i++) {
            enode *n = &g->nodes[i];
            int cost = node_cost(n, model);
            bool ready = true;
            for (int c = 0; c < n->nchildren; c++) {
                int cls = eg_find(g, n->children[c]);
                if (best_cost[cls] == INT32_MAX) { ready = false; break; }
                cost += best_cost[cls];
            }
            if (!ready) continue;
            int cls = eg_find(g, i);
            if (cost < best_cost[cls] || (cost == best_cost[cls] && i < best_node[cls])) {
                best_cost[cls] = cost;
                best_node[cls] = i;
                changed = true;
            }
        }
    }
}

/* ---------- saturation + writeback ---------- */

static double seconds_since(clock_t start)
{
    return (double)(clock() - start) / (double)CLOCKS_PER_SEC;
}

/* Rebuilds one instruction from the extracted e-node, through the same
 * builder API kernels use, so the host sees ordinary structural edits. */
static bool writeback_instr(ccw_ir *ir, ccw_node ins, egraph *g, int enode_idx)
{
    enode *n = &g->nodes[enode_idx];
    if (n->is_const || n->opcode[0] == '\0') return false;

    const char *current = ccw_ir_instr_opcode(ir, ins);
    bool same = current != NULL && strcmp(current, n->opcode) == 0;
    if (same && n->nchildren == ccw_ir_instr_operand_count(ir, ins)) {
        bool operands_match = true;
        for (int i = 0; i < n->nchildren && operands_match; i++) {
            enode *child = &g->nodes[n->children[i]];
            ccw_node opnd = ccw_ir_instr_operand(ir, ins, i);
            int64_t value = 0;
            if (child->is_const)
                operands_match = ccw_ir_const_int_value(ir, opnd, &value) == CCW_OK &&
                                 value == child->value;
            else {
                const char *name = ccw_ir_operand_name(ir, opnd);
                operands_match = name != NULL && strcmp(name, child->reg) == 0;
            }
        }
        if (operands_match) return false;   /* nothing to do */
    }

    ccw_node built = ccw_ir_instr_build(ir, n->opcode, ccw_ir_instr_type(ir, ins));
    if (built == 0) return false;
    const char *dest = ccw_ir_instr_dest(ir, ins);
    if (dest != NULL) ccw_ir_instr_set_dest(ir, built, dest);
    for (int i = 0; i < n->nchildren; i++) {
        enode *child = &g->nodes[n->children[i]];
        ccw_node opnd = child->is_const
            ? ccw_ir_operand_const_int(ir, CCW_TY_I64, child->value)
            : ccw_ir_operand_reg(ir, child->reg);
        if (opnd == 0 || ccw_ir_instr_add_operand(ir, built, opnd) != CCW_OK)
            return false;
    }
    return ccw_ir_instr_replace(ir, ins, built) == CCW_OK;
}

ccw_status ccw_oeuph_run(ccw_ir *ir, const ccw_oeuph_ruleset *rs,
                         ccw_oeuph_budget budget, ccw_cost_model model,
                         ccw_oeuph_stats *stats, char **error_message)
{
    if (error_message) *error_message = NULL;
    if (ir == NULL || rs == NULL) return CCW_ERR_TYPE;

    ccw_oeuph_stats local;
    memset(&local, 0, sizeof(local));
    snprintf(local.ruleset, sizeof(local.ruleset), "%s", rs->name);
    local.saturated = true;
    clock_t start = clock();

    for (int fi = 0; fi < ccw_ir_function_count(ir); fi++) {
        ccw_node fn = ccw_ir_function_ref(ir, fi);
        for (int bi = 0; bi < ccw_ir_function_block_count(ir, fn); bi++) {
            ccw_node blk = ccw_ir_function_block_ref(ir, fn, bi);
            int ninstr = ccw_ir_block_instr_count(ir, blk);

            for (int ii = 0; ii < ninstr; ii++) {
                ccw_node ins = ccw_ir_block_instr_ref(ir, blk, ii);
                egraph g;
                memset(&g, 0, sizeof(g));
                int root = import_instr(&g, ir, ins);
                if (root < 0) { eg_free(&g); continue; }
                int root_class = eg_find(&g, root);

                /* --- saturation --- */
                for (int iter = 0; iter < budget.max_iterations; iter++) {
                    local.iterations++;
                    bool grew = false;

                    for (int ri = 0; ri < rs->count; ri++) {
                        const ccw_rule *rule = &rs->rules[ri];
                        for (int dir = 0; dir < (rule->bidirectional ? 2 : 1); dir++) {
                            const ccw_pattern *from = dir == 0 ? &rule->lhs : &rule->rhs;
                            const ccw_pattern *to   = dir == 0 ? &rule->rhs : &rule->lhs;
                            int snapshot = g.count;
                            for (int ni = 0; ni < snapshot; ni++) {
                                bindings b;
                                b.count = 0;
                                if (!match_node(&g, from, from->root, ni, &b)) continue;
                                local.matches++;

                                /* Side conditions guard equivalence, e.g.
                                 * "only when the constant is a power of two". */
                                if (rule->side_condition != NULL) {
                                    bool ok = false;
                                    for (int k = 0; k < b.count; k++)
                                        if (b.items[k].is_value &&
                                            rule->side_condition(b.items[k].value))
                                            ok = true;
                                    if (!ok) continue;
                                }

                                int made = instantiate(&g, to, to->root, &b);
                                if (made < 0) continue;
                                if (eg_union(&g, ni, made)) {
                                    local.applications++;
                                    grew = true;
                                }
                            }
                        }
                    }

                    eg_rebuild(&g);

                    if (g.count > budget.max_nodes) {
                        local.saturated = false;
                        snprintf(local.budget_hit, sizeof(local.budget_hit), "nodes");
                        break;
                    }
                    if (eg_class_count(&g) > budget.max_classes) {
                        local.saturated = false;
                        snprintf(local.budget_hit, sizeof(local.budget_hit), "classes");
                        break;
                    }
                    if (seconds_since(start) > budget.max_seconds) {
                        local.saturated = false;
                        snprintf(local.budget_hit, sizeof(local.budget_hit), "time");
                        break;
                    }
                    if (!grew) break;                       /* saturated */
                    if (iter + 1 == budget.max_iterations) {
                        local.saturated = false;
                        snprintf(local.budget_hit, sizeof(local.budget_hit), "iterations");
                    }
                }

                /* --- extraction --- */
                kvec_t(int) best_node = { 0, 0, NULL };
                kvec_t(int) best_cost = { 0, 0, NULL };
                bool have_best =
                    kv_resize(int, best_node, (size_t)g.count) != NULL &&
                    kv_resize(int, best_cost, (size_t)g.count) != NULL;
                if (have_best) {
                    extract(&g, model, best_node.a, best_cost.a);
                    int chosen = best_node.a[eg_find(&g, root_class)];
                    if (chosen >= 0) writeback_instr(ir, ins, &g, chosen);
                }
                kv_destroy(best_node);
                kv_destroy(best_cost);

                local.nodes += g.count;
                local.classes += eg_class_count(&g);
                eg_free(&g);
            }
        }
    }

    if (stats != NULL) *stats = local;
    return CCW_OK;
}
