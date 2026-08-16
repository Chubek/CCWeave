/* Pattern parser for Oeuph rules. */

#include "ccw_oeuph_pattern.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char  *p;
    ccw_pattern *pat;
    char        *reason;
    size_t       reason_size;
    bool         failed;
} pstate;

static void pfail(pstate *s, const char *msg)
{
    if (!s->failed && s->reason != NULL)
        snprintf(s->reason, s->reason_size, "%s", msg);
    s->failed = true;
}

static void skip_ws(pstate *s)
{
    while (*s->p != '\0' && isspace((unsigned char)*s->p)) s->p++;
}

static int alloc_node(pstate *s)
{
    if (s->pat->count >= CCW_PAT_MAX_NODES) {
        pfail(s, "pattern too large");
        return -1;
    }
    int id = s->pat->count++;
    memset(&s->pat->nodes[id], 0, sizeof(s->pat->nodes[id]));
    return id;
}

static int parse_node(pstate *s);

static int parse_atom(pstate *s)
{
    char token[32];
    size_t n = 0;
    while (*s->p != '\0' && !isspace((unsigned char)*s->p) &&
           *s->p != '(' && *s->p != ')') {
        if (n + 1 < sizeof(token)) token[n++] = *s->p;
        s->p++;
    }
    token[n] = '\0';
    if (n == 0) { pfail(s, "empty atom"); return -1; }

    int id = alloc_node(s);
    if (id < 0) return -1;
    ccw_pat_node *node = &s->pat->nodes[id];
    if (token[0] == '?') {
        node->kind = CCW_PAT_VAR;
        snprintf(node->text, sizeof(node->text), "%s", token);
    } else {
        /* A bare integer is shorthand for a constant leaf. */
        char *end = NULL;
        long long v = strtoll(token, &end, 10);
        if (end != NULL && *end == '\0') {
            node->kind = CCW_PAT_CONST;
            node->value = (int64_t)v;
        } else {
            node->kind = CCW_PAT_OP;
            snprintf(node->text, sizeof(node->text), "%s", token);
        }
    }
    return id;
}

static int parse_list(pstate *s)
{
    s->p++;                  /* '(' */
    skip_ws(s);

    char head[32];
    size_t n = 0;
    while (*s->p != '\0' && !isspace((unsigned char)*s->p) &&
           *s->p != '(' && *s->p != ')') {
        if (n + 1 < sizeof(head)) head[n++] = *s->p;
        s->p++;
    }
    head[n] = '\0';
    if (n == 0) { pfail(s, "list must start with an opcode"); return -1; }

    int id = alloc_node(s);
    if (id < 0) return -1;

    if (strcmp(head, "iconst") == 0) {
        skip_ws(s);
        if (*s->p == '?') {
            char var[32];
            size_t m = 0;
            while (*s->p != '\0' && !isspace((unsigned char)*s->p) && *s->p != ')') {
                if (m + 1 < sizeof(var)) var[m++] = *s->p;
                s->p++;
            }
            var[m] = '\0';
            s->pat->nodes[id].kind = CCW_PAT_CONST_VAR;
            snprintf(s->pat->nodes[id].text, sizeof(s->pat->nodes[id].text), "%s", var);
        } else {
            char *end = NULL;
            long long v = strtoll(s->p, &end, 10);
            if (end == s->p) { pfail(s, "iconst needs an integer or ?var"); return -1; }
            s->p = end;
            s->pat->nodes[id].kind = CCW_PAT_CONST;
            s->pat->nodes[id].value = (int64_t)v;
        }
        skip_ws(s);
        if (*s->p != ')') { pfail(s, "unterminated iconst"); return -1; }
        s->p++;
        return id;
    }

    s->pat->nodes[id].kind = CCW_PAT_OP;
    snprintf(s->pat->nodes[id].text, sizeof(s->pat->nodes[id].text), "%s", head);
    for (;;) {
        skip_ws(s);
        if (*s->p == '\0') { pfail(s, "unterminated list"); return -1; }
        if (*s->p == ')') { s->p++; break; }
        int child = parse_node(s);
        if (child < 0 || s->failed) return -1;
        ccw_pat_node *node = &s->pat->nodes[id];
        if (node->nchildren >= CCW_PAT_MAX_CHILDREN) {
            pfail(s, "too many operands in pattern");
            return -1;
        }
        node->children[node->nchildren++] = child;
    }
    return id;
}

static int parse_node(pstate *s)
{
    skip_ws(s);
    if (*s->p == '(') return parse_list(s);
    return parse_atom(s);
}

bool ccw_pattern_parse(const char *text, ccw_pattern *out,
                       char *reason, size_t reason_size)
{
    if (text == NULL || out == NULL) return false;
    memset(out, 0, sizeof(*out));
    pstate s = { text, out, reason, reason_size, false };
    int root = parse_node(&s);
    if (s.failed || root < 0) return false;
    skip_ws(&s);
    if (*s.p != '\0') {
        if (reason != NULL) snprintf(reason, reason_size, "trailing text in pattern");
        return false;
    }
    out->root = root;
    return true;
}
