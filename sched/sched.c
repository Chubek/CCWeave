#define _POSIX_C_SOURCE 200809L
#include "sched.h"
#include "khash.h"
#include "kstring.h"
#include "kvec.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { NODE_KERNEL = 1, NODE_REWRITE = 2, NODE_BARRIER = 3 };
typedef struct { char *name, *path; char **caps; size_t nc, cc; } kernel;
typedef struct { char *name, *path; } ruleset;
typedef struct { unsigned id, kind; char *name; char **members; size_t nm; } node;
typedef struct { unsigned from, to; } edge;
typedef struct { size_t *items, n, c; } kernel_matches;

/* SCHED §5.2: indexes accelerate manifest resolution; arrays preserve order. */
KHASH_MAP_INIT_STR(sched_kernel_by_name, size_t)
KHASH_MAP_INIT_STR(sched_capability_index, kernel_matches)
KHASH_MAP_INIT_STR(sched_ruleset_by_name, size_t)
KHASH_MAP_INIT_INT64(sched_edge_set, char)

struct ccw_sched {
  char *name, *manifest_dir;
  kernel *kernels; size_t nk, ck;
  ruleset *rules; size_t nr, cr;
  node *nodes; size_t nn, cn;
  edge *edges; size_t ne, ce;
  khash_t(sched_kernel_by_name) *kernel_by_name;
  khash_t(sched_capability_index) *capability_index;
  khash_t(sched_ruleset_by_name) *ruleset_by_name;
  khash_t(sched_edge_set) *edge_set;
  int sealed;
};
struct ccw_plan { char *text; };

static void fail(ccw_sched_error *e, int code, const char *message) {
  if (e) { e->code = code; snprintf(e->message, sizeof(e->message), "%s", message); }
}
static char *xstrdup(const char *s) {
  kstring_t copy = { 0, 0, NULL };
  if (!s || kputs(s, &copy) == EOF) return NULL;
  return ks_release(&copy);
}
static int reserve(void **p, size_t *capacity, size_t need, size_t item_size) {
  size_t n;
  kvec_t(unsigned char) bytes;
  if (need <= *capacity && *p != NULL) return 1;
  n = *capacity ? *capacity * 2 : 16; while (n < need) n *= 2;
  bytes.n = 0;
  bytes.m = *capacity * item_size;
  bytes.a = (unsigned char *)*p;
  if (kv_resize(unsigned char, bytes, n * item_size) == NULL) return 0;
  *p = bytes.a;
  *capacity = n;
  return 1;
}
static char *strip(char *s) {
  char *end;
  while (isspace((unsigned char)*s)) ++s;
  end = s + strlen(s); while (end > s && isspace((unsigned char)end[-1])) --end; *end = '\0';
  return s;
}
static int clean_scalar(const char *s) {
  return s && *s && !strchr(s, '\n') && !strchr(s, '\r') && !strchr(s, ',');
}
static int add_cap(kernel *k, const char *cap) {
  if (!reserve((void **)&k->caps, &k->cc, k->nc + 1, sizeof(*k->caps))) return 0;
  k->caps[k->nc++] = xstrdup(cap); return k->caps[k->nc - 1] != NULL;
}
static int append_kernel_match(kernel_matches *matches, size_t kernel_index) {
  size_t i;
  for (i = 0; i < matches->n; ++i) if (matches->items[i] == kernel_index) return 1;
  if (!reserve((void **)&matches->items, &matches->c, matches->n + 1, sizeof(*matches->items))) return 0;
  matches->items[matches->n++] = kernel_index;
  return 1;
}
static int index_kernel(ccw_sched *s, size_t kernel_index) {
  kernel *k = &s->kernels[kernel_index];
  khint_t slot;
  int ret;
  slot = kh_put(sched_kernel_by_name, s->kernel_by_name, k->name, &ret);
  if (ret < 0) return 0;
  if (ret != 0) kh_value(s->kernel_by_name, slot) = kernel_index;
  for (size_t i = 0; i < k->nc; ++i) {
    slot = kh_get(sched_capability_index, s->capability_index, k->caps[i]);
    if (slot == kh_end(s->capability_index)) {
      slot = kh_put(sched_capability_index, s->capability_index, k->caps[i], &ret);
      if (ret < 0) return 0;
      if (ret != 0) kh_value(s->capability_index, slot) = (kernel_matches){0};
    }
    if (!append_kernel_match(&kh_value(s->capability_index, slot), kernel_index)) return 0;
  }
  return 1;
}
static int index_ruleset(ccw_sched *s, size_t ruleset_index) {
  khint_t slot;
  int ret;
  slot = kh_put(sched_ruleset_by_name, s->ruleset_by_name,
                s->rules[ruleset_index].name, &ret);
  if (ret < 0) return 0;
  if (ret != 0) kh_value(s->ruleset_by_name, slot) = ruleset_index;
  return 1;
}
static int finish_kernel(ccw_sched *s, kernel *pending) {
  size_t index;
  if (!pending->name) return 1;
  if (!pending->path || !clean_scalar(pending->name)) return 0;
  if (!reserve((void **)&s->kernels, &s->ck, s->nk + 1, sizeof(*s->kernels))) return 0;
  index = s->nk;
  s->kernels[s->nk++] = *pending;
  memset(pending, 0, sizeof(*pending));
  return index_kernel(s, index);
}
static int load_kernels(ccw_sched *s, ccw_sched_error *e) {
  char file[1024], line[2048]; FILE *f; kernel current = {0}; int in_caps = 0;
  snprintf(file, sizeof(file), "%s/Kernel.yaml", s->manifest_dir);
  f = fopen(file, "r"); if (!f) { fail(e, 2, "cannot read Kernel.yaml"); return 0; }
  while (fgets(line, sizeof(line), f)) {
    char *p = strip(line);
    if (!strncmp(p, "- path:", 7)) {
      if (!finish_kernel(s, &current)) goto bad;
      current.path = xstrdup(strip(p + 7)); in_caps = 0;
    } else if (current.path && !strncmp(p, "name:", 5)) current.name = xstrdup(strip(p + 5));
    else if (current.path && !strcmp(p, "capabilities:")) in_caps = 1;
    else if (in_caps && !strncmp(p, "- ", 2)) { if (!add_cap(&current, strip(p + 2))) goto bad; }
    else if (*p && p[0] != '#') in_caps = 0;
  }
  if (!finish_kernel(s, &current)) goto bad;
  fclose(f); if (!s->nk) { fail(e, 2, "Kernel.yaml has no kernels"); return 0; } return 1;
bad:
  fclose(f); free(current.name); free(current.path); for (size_t i = 0; i < current.nc; ++i) free(current.caps[i]); free(current.caps);
  fail(e, 2, "malformed Kernel.yaml"); return 0;
}
static int load_rules(ccw_sched *s, ccw_sched_error *e) {
  char file[1024], line[2048]; FILE *f; char *name = NULL, *path = NULL;
  snprintf(file, sizeof(file), "%s/Stdrewrite.yaml", s->manifest_dir);
  f = fopen(file, "r"); if (!f) { fail(e, 2, "cannot read Stdrewrite.yaml"); return 0; }
  while (fgets(line, sizeof(line), f)) {
    char *p = strip(line);
    if (!strncmp(p, "- path:", 7)) {
      free(name); free(path);
      name = NULL; path = xstrdup(strip(p + 7));
      if (!path || !clean_scalar(path)) goto bad;
    }
    else if (!strncmp(p, "name:", 5)) {
      if (!path) goto bad;
      name = xstrdup(strip(p + 5)); if (!name || !clean_scalar(name) ||
          !reserve((void **)&s->rules, &s->cr, s->nr + 1, sizeof(*s->rules))) goto bad;
      s->rules[s->nr].name = name;
      s->rules[s->nr].path = path;
      name = NULL;
      path = NULL;
      ++s->nr;
      if (!index_ruleset(s, s->nr - 1)) goto bad;
    }
  }
  free(name); free(path); fclose(f); if (!s->nr) {
    fail(e, 2, "Stdrewrite.yaml has no rulesets"); return 0;
  }
  return 1;
bad: free(name); free(path); fclose(f); fail(e, 2, "malformed Stdrewrite.yaml"); return 0;
}
static int has_cap(const kernel *k, const char *cap) {
  for (size_t i = 0; i < k->nc; ++i) if (!strcmp(k->caps[i], cap)) return 1;
  return 0;
}
static int wildcard(const char *pattern, const char *text) {
  if (!*pattern) return !*text;
  if (*pattern == '*') return wildcard(pattern + 1, text) || (*text && wildcard(pattern, text + 1));
  return *text && *pattern == *text && wildcard(pattern + 1, text + 1);
}
static int can_mutate(ccw_sched *s, ccw_sched_error *e) {
  if (!s || s->sealed) { fail(e, 3, "scheduler is sealed"); return 0; } return 1;
}
static int add_edge_raw(ccw_sched *s, unsigned from, unsigned to) {
  khint64_t key = ((khint64_t)from << 32) | (khint64_t)to;
  khint_t slot;
  int ret;
  if (!s->edge_set) return 0;
  slot = kh_get(sched_edge_set, s->edge_set, key);
  if (slot != kh_end(s->edge_set)) return 1;
  if (!reserve((void **)&s->edges, &s->ce, s->ne + 1, sizeof(*s->edges))) return 0;
  slot = kh_put(sched_edge_set, s->edge_set, key, &ret);
  if (ret < 0) return 0;
  if (ret == 0) return 1;
  kh_value(s->edge_set, slot) = 1;
  s->edges[s->ne++] = (edge){from, to}; return 1;
}
static int add_node(ccw_sched *s, unsigned kind, const char *name, char **members, size_t nm, uint32_t *out, ccw_sched_error *e) {
  node *n;
  if (!can_mutate(s, e) || !clean_scalar(name) || !reserve((void **)&s->nodes, &s->cn, s->nn + 1, sizeof(*s->nodes))) { fail(e, 1, "out of memory or invalid node name"); return 0; }
  n = &s->nodes[s->nn]; memset(n, 0, sizeof(*n)); n->id = (unsigned)s->nn + 1; n->kind = kind; n->name = xstrdup(name); n->members = members; n->nm = nm;
  if (!n->name) { fail(e, 1, "out of memory"); return 0; }
  if (kind != NODE_BARRIER) for (size_t i = 0; i < s->nn; ++i) if (s->nodes[i].kind == NODE_BARRIER && !add_edge_raw(s, s->nodes[i].id, n->id)) { fail(e, 1, "out of memory"); return 0; }
  ++s->nn; *out = n->id; return 1;
}
static char **copy_strings(char **items, size_t n) {
  kvec_t(char *) values = { 0, 0, NULL };
  if (kv_resize(char *, values, n ? n : 1) == NULL) return NULL;
  char **copy = values.a;
  memset(copy, 0, (n ? n : 1) * sizeof(*copy));
  if (!copy) return NULL;
  for (size_t i = 0; i < n; ++i) {
    copy[i] = xstrdup(items[i]);
    if (!copy[i]) { while (i) free(copy[--i]); free(copy); return NULL; }
  }
  return copy;
}

ccw_sched *ccw_sched_new(const char *name, const char *manifest_dir, ccw_sched_error *e) {
  ccw_sched *s;
  if (!clean_scalar(name)) { fail(e, 1, "invalid pipeline name"); return NULL; }
  s = calloc(1, sizeof(*s)); if (!s) { fail(e, 1, "out of memory"); return NULL; }
  s->name = xstrdup(name); s->manifest_dir = xstrdup(manifest_dir ? manifest_dir : "manifests");
  s->kernel_by_name = kh_init(sched_kernel_by_name);
  s->capability_index = kh_init(sched_capability_index);
  s->ruleset_by_name = kh_init(sched_ruleset_by_name);
  s->edge_set = kh_init(sched_edge_set);
  if (!s->name || !s->manifest_dir || !s->kernel_by_name ||
      !s->capability_index || !s->ruleset_by_name || !s->edge_set ||
      !load_kernels(s, e) || !load_rules(s, e)) {
    if (!s->name || !s->manifest_dir || !s->kernel_by_name ||
        !s->capability_index || !s->ruleset_by_name || !s->edge_set)
      fail(e, 1, "out of memory");
    ccw_sched_free(s);
    return NULL;
  }
  return s;
}
void ccw_sched_free(ccw_sched *s) {
  if (!s) return;
  for (size_t i=0;i<s->nk;i++) { free(s->kernels[i].name); free(s->kernels[i].path); for(size_t j=0;j<s->kernels[i].nc;j++) free(s->kernels[i].caps[j]); free(s->kernels[i].caps); }
  for (size_t i=0;i<s->nr;i++) { free(s->rules[i].name); free(s->rules[i].path); }
  for (size_t i=0;i<s->nn;i++) { free(s->nodes[i].name); for(size_t j=0;j<s->nodes[i].nm;j++) free(s->nodes[i].members[j]); free(s->nodes[i].members); }
  if (s->capability_index) {
    for (khint_t i = kh_begin(s->capability_index); i != kh_end(s->capability_index); ++i)
      if (kh_exist(s->capability_index, i)) free(kh_value(s->capability_index, i).items);
  }
  kh_destroy(sched_kernel_by_name, s->kernel_by_name);
  kh_destroy(sched_capability_index, s->capability_index);
  kh_destroy(sched_ruleset_by_name, s->ruleset_by_name);
  kh_destroy(sched_edge_set, s->edge_set);
  free(s->kernels); free(s->rules); free(s->nodes); free(s->edges); free(s->name); free(s->manifest_dir); free(s);
}
int ccw_sched_require_kernel(ccw_sched *s, const char *name, uint32_t *out, ccw_sched_error *e) {
  khint_t slot;
  size_t index;
  if (!s || !name || !out || !s->kernel_by_name) { fail(e, 1, "invalid require"); return 0; }
  slot = kh_get(sched_kernel_by_name, s->kernel_by_name, name);
  if (slot != kh_end(s->kernel_by_name)) {
    index = kh_value(s->kernel_by_name, slot);
    char **caps = copy_strings(s->kernels[index].caps, s->kernels[index].nc);
    if (!caps) { fail(e,1,"out of memory"); return 0; }
    return add_node(s,NODE_KERNEL,s->kernels[index].name,caps,s->kernels[index].nc,out,e);
  }
  fail(e, 4, "kernel is absent from Kernel.yaml"); return 0;
}
int ccw_sched_require_capability(ccw_sched *s, const char *cap, const char *prefer, uint32_t *out, ccw_sched_error *e) {
  const kernel *match = NULL;
  khint_t slot;
  kernel_matches *matches;
  size_t found, i;
  if (!s || !cap || !out || !s->capability_index) { fail(e, 1, "invalid capability query"); return 0; }
  slot = kh_get(sched_capability_index, s->capability_index, cap);
  if (slot == kh_end(s->capability_index)) { fail(e,4,"capability is absent from Kernel.yaml"); return 0; }
  matches = &kh_value(s->capability_index, slot);
  found = matches->n;
  for (i = 0; i < found; ++i) {
    const kernel *candidate = &s->kernels[matches->items[i]];
    if (!match || (prefer && !strcmp(prefer, candidate->name))) match = candidate;
  }
  if (!found) { fail(e,4,"capability is absent from Kernel.yaml"); return 0; }
  if (prefer && (!match || strcmp(match->name,prefer))) { fail(e,4,"preferred kernel does not provide capability"); return 0; }
  if (!prefer && found != 1) { fail(e,4,"capability is ambiguous; supply prefer"); return 0; }
  {
    char **caps = copy_strings(match->caps, match->nc);
    if (!caps) { fail(e,1,"out of memory"); return 0; }
    return add_node(s,NODE_KERNEL,match->name,caps,match->nc,out,e);
  }
}
int ccw_sched_probe_capability(ccw_sched *s,const char *cap,const char *prefer,uint32_t *out) { ccw_sched_error e; return ccw_sched_require_capability(s,cap,prefer,out,&e); }
int ccw_sched_rewrite(ccw_sched *s,const char *pattern,uint32_t *out,ccw_sched_error *e) {
  char **members=NULL; size_t n=0,cap=0;
  if(!s||!pattern||!out||!can_mutate(s,e)){free(members);return 0;}
  for(size_t i=0;i<s->nr;i++) if(wildcard(pattern,s->rules[i].name)) { if(!reserve((void**)&members,&cap,n+1,sizeof(*members))){fail(e,1,"out of memory");goto no;} members[n++]=xstrdup(s->rules[i].name); }
  if(!n){fail(e,4,"rewrite pattern matched no rulesets");goto no;}
  if(!add_node(s,NODE_REWRITE,pattern,members,n,out,e)) goto no;
  return 1;
no: for(size_t i=0;i<n;i++)free(members[i]);free(members);return 0;
}
int ccw_sched_edge(ccw_sched *s,uint32_t a,uint32_t b,ccw_sched_error *e) {
  if(!can_mutate(s,e)||!a||!b||a>s->nn||b>s->nn){fail(e,5,"edge references a node outside this plan");return 0;}
  if(!add_edge_raw(s,a,b)){fail(e,1,"out of memory");return 0;}return 1;
}
int ccw_sched_barrier(ccw_sched *s,const char *label,uint32_t *out,ccw_sched_error *e) {
  unsigned id; if(!add_node(s,NODE_BARRIER,label?label:"barrier",NULL,0,out,e))return 0; id=*out;
  for(unsigned i=1;i<id;i++) {
    if(!add_edge_raw(s,i,id)) { fail(e,1,"out of memory"); return 0; }
  }
  return 1;
}
static int acyclic(const ccw_sched *s) {
  unsigned *in=calloc(s->nn,sizeof(*in)), seen=0; if(!in)return 0;
  for(size_t i=0;i<s->ne;i++)++in[s->edges[i].to-1];
  for(;;){unsigned id=0;for(size_t i=0;i<s->nn;i++)if(in[i]==0){id=(unsigned)i+1;in[i]=UINT_MAX;break;}if(!id)break;++seen;for(size_t j=0;j<s->ne;j++)if(s->edges[j].from==id&&in[s->edges[j].to-1]!=UINT_MAX)--in[s->edges[j].to-1];}
  free(in);return seen==s->nn;
}
int ccw_sched_seal(ccw_sched *s,ccw_plan **out,ccw_sched_error *e) {
  kstring_t text = { 0, 0, NULL };
  int useful=0;
  if(!can_mutate(s,e)||!out) return 0;
  for(size_t i=0;i<s->nn;i++) if(s->nodes[i].kind!=NODE_BARRIER) useful=1;
  if(!useful||!acyclic(s)){fail(e,6,useful?"plan contains a cycle":"plan contains no work nodes");return 0;}
  if (ksprintf(&text, "CCW-SCHED-PLAN 1\nname %s\n", s->name) < 0)
    goto oom;
  for(size_t i=0;i<s->nn;i++){
    node*n=&s->nodes[i];
    if (ksprintf(&text, "node %u %u %s", n->id, n->kind, n->name) < 0)
      goto oom;
    for(size_t j=0;j<n->nm;j++)
      if (ksprintf(&text, " %s", n->members[j]) < 0) goto oom;
    if (kputc('\n', &text) == EOF) goto oom;
  }
  for(size_t i=0;i<s->ne;i++)
    if (ksprintf(&text, "edge %u %u\n", s->edges[i].from, s->edges[i].to) < 0)
      goto oom;
  *out=malloc(sizeof(**out));
  if(!*out){free(text.s);fail(e,1,"out of memory");return 0;}
  (*out)->text=ks_release(&text);
  s->sealed=1;
  return 1;
oom:
  free(text.s);
  fail(e,1,"out of memory");
  return 0;
}
void ccw_plan_free(ccw_plan *p){if(p){free(p->text);free(p);}}
const char *ccw_plan_text(const ccw_plan *p){return p?p->text:"";}
ccw_plan *ccw_plan_from_text(const char *text){ccw_plan*p=malloc(sizeof(*p));if(p)p->text=xstrdup(text?text:"");return p;}
int ccw_plan_write(const ccw_plan*p,const char*path,ccw_sched_error*e){FILE*f;if(!p||!path){fail(e,1,"invalid plan path");return 0;}f=fopen(path,"wb");if(!f){fail(e,7,"cannot write plan");return 0;}if(fputs(p->text,f)<0||fclose(f)){fail(e,7,"cannot write plan");return 0;}return 1;}

static void free_node_array(node *nodes, size_t count)
{
  if (nodes == NULL) return;
  for (size_t i = 0; i < count; ++i) {
    free(nodes[i].name);
    for (size_t j = 0; j < nodes[i].nm; ++j) free(nodes[i].members[j]);
    free(nodes[i].members);
  }
  free(nodes);
}

/* Plan text is intentionally small and scalar-only, so execution can
 * rehydrate it without adding another serialized artifact format. */
static int parse_plan_text(const char *text, node **nodes_out, size_t *nn_out,
                           edge **edges_out, size_t *ne_out,
                           ccw_sched_error *e)
{
  char *copy = NULL, *save = NULL, *line;
  node *nodes = NULL;
  edge *edges = NULL;
  size_t nn = 0, cn = 0, ne = 0, ce = 0;
  int ok = 0;

  if (nodes_out) *nodes_out = NULL;
  if (nn_out) *nn_out = 0;
  if (edges_out) *edges_out = NULL;
  if (ne_out) *ne_out = 0;
  if (text == NULL || nodes_out == NULL || nn_out == NULL ||
      edges_out == NULL || ne_out == NULL) {
    fail(e, 1, "invalid plan");
    return 0;
  }

  copy = xstrdup(text);
  if (copy == NULL) {
    fail(e, 1, "out of memory");
    return 0;
  }
  line = strtok_r(copy, "\n", &save);
  if (line == NULL || strcmp(line, "CCW-SCHED-PLAN 1") != 0) {
    fail(e, 8, "invalid plan header");
    goto done;
  }

  while ((line = strtok_r(NULL, "\n", &save)) != NULL) {
    char *item_save = NULL;
    char *item = strtok_r(line, " ", &item_save);
    if (item == NULL) continue;

    if (!strcmp(item, "name")) {
      if (strtok_r(NULL, " ", &item_save) == NULL ||
          strtok_r(NULL, " ", &item_save) != NULL) {
        fail(e, 8, "malformed plan name");
        goto done;
      }
    } else if (!strcmp(item, "node")) {
      char *id_text = strtok_r(NULL, " ", &item_save);
      char *kind_text = strtok_r(NULL, " ", &item_save);
      char *name = strtok_r(NULL, " ", &item_save);
      char *extra;
      unsigned id, kind;
      node parsed;
      char **members = NULL;
      size_t nm = 0, cm = 0;

      if (id_text == NULL || kind_text == NULL || name == NULL ||
          sscanf(id_text, "%u", &id) != 1 ||
          sscanf(kind_text, "%u", &kind) != 1 ||
          id != nn + 1 || (kind != NODE_KERNEL && kind != NODE_REWRITE &&
                           kind != NODE_BARRIER) || !clean_scalar(name)) {
        fail(e, 8, "malformed plan node");
        goto done;
      }
      memset(&parsed, 0, sizeof(parsed));
      parsed.id = id;
      parsed.kind = kind;
      parsed.name = xstrdup(name);
      if (parsed.name == NULL) {
        fail(e, 1, "out of memory");
        goto done;
      }
      while ((extra = strtok_r(NULL, " ", &item_save)) != NULL) {
        if (!clean_scalar(extra) ||
            !reserve((void **)&members, &cm, nm + 1, sizeof(*members))) {
          free(parsed.name);
          for (size_t i = 0; i < nm; ++i) free(members[i]);
          free(members);
          fail(e, !clean_scalar(extra) ? 8 : 1,
               !clean_scalar(extra) ? "malformed plan member" : "out of memory");
          goto done;
        }
        members[nm] = xstrdup(extra);
        if (members[nm] == NULL) {
          free(parsed.name);
          for (size_t i = 0; i < nm; ++i) free(members[i]);
          free(members);
          fail(e, 1, "out of memory");
          goto done;
        }
        ++nm;
      }
      if ((kind == NODE_REWRITE && nm == 0) ||
          (kind == NODE_BARRIER && nm != 0)) {
        free(parsed.name);
        for (size_t i = 0; i < nm; ++i) free(members[i]);
        free(members);
        fail(e, 8, "invalid plan node members");
        goto done;
      }
      parsed.members = members;
      parsed.nm = nm;
      if (!reserve((void **)&nodes, &cn, nn + 1, sizeof(*nodes))) {
        free(parsed.name);
        for (size_t i = 0; i < nm; ++i) free(members[i]);
        free(members);
        fail(e, 1, "out of memory");
        goto done;
      }
      nodes[nn++] = parsed;
    } else if (!strcmp(item, "edge")) {
      char *from_text = strtok_r(NULL, " ", &item_save);
      char *to_text = strtok_r(NULL, " ", &item_save);
      unsigned from, to;
      int have_space;
      have_space = reserve((void **)&edges, &ce, ne + 1, sizeof(*edges));
      if (from_text == NULL || to_text == NULL ||
          strtok_r(NULL, " ", &item_save) != NULL ||
          sscanf(from_text, "%u", &from) != 1 ||
          sscanf(to_text, "%u", &to) != 1 || from == 0 || to == 0 ||
          !have_space) {
        fail(e, !have_space ? 1 : 8,
             "malformed plan edge");
        goto done;
      }
      edges[ne++] = (edge){from, to};
    } else {
      fail(e, 8, "unknown plan record");
      goto done;
    }
  }

  for (size_t i = 0; i < ne; ++i) {
    if (edges[i].from > nn || edges[i].to > nn) {
      fail(e, 8, "plan edge references an unknown node");
      goto done;
    }
  }
  if (nn == 0) {
    fail(e, 8, "plan contains no nodes");
    goto done;
  }

  *nodes_out = nodes;
  *nn_out = nn;
  *edges_out = edges;
  *ne_out = ne;
  nodes = NULL;
  edges = NULL;
  ok = 1;
done:
  free(copy);
  free_node_array(nodes, nn);
  free(edges);
  return ok;
}

static char *path_join3(const char *a, const char *b, const char *c)
{
  kstring_t out = { 0, 0, NULL };
  if (a == NULL || b == NULL || c == NULL) return NULL;
  if (ksprintf(&out, "%s%s%s", a, (a[0] && a[strlen(a) - 1] == '/') ? "" : "/", b) < 0 ||
      ksprintf(&out, "%s%s", (out.l && out.s[out.l - 1] == '/') ? "" : "/", c) < 0) {
    free(out.s);
    return NULL;
  }
  return ks_release(&out);
}

static char *manifest_parent(const char *manifest_dir)
{
  char *parent, *slash;
  size_t n;
  if (manifest_dir == NULL || !*manifest_dir) return NULL;
  parent = xstrdup(manifest_dir);
  if (parent == NULL) return NULL;
  n = strlen(parent);
  while (n > 1 && parent[n - 1] == '/') parent[--n] = '\0';
  slash = strrchr(parent, '/');
  if (slash == NULL) {
    strcpy(parent, ".");
  } else if (slash == parent) {
    parent[1] = '\0';
  } else {
    *slash = '\0';
  }
  return parent;
}

static char *ruleset_file_path(const char *manifest_dir, const char *ruleset_path)
{
  char *candidate, *parent;
  FILE *probe;
  if (ruleset_path == NULL || !*ruleset_path) return NULL;
  if (ruleset_path[0] == '/') {
    candidate = path_join3("", ruleset_path, "rules.scm");
    if (candidate == NULL) return NULL;
    probe = fopen(candidate, "r");
    if (probe != NULL) {
      fclose(probe);
      return candidate;
    }
    free(candidate);
    return NULL;
  }

  /* Generated paths are repository-root relative, while manifest_dir names
   * the manifests/ directory.  Resolve exactly that manifest-declared path. */
  parent = manifest_parent(manifest_dir);
  if (parent == NULL) return NULL;
  candidate = path_join3(parent, ruleset_path, "rules.scm");
  free(parent);
  if (candidate == NULL) return NULL;
  probe = fopen(candidate, "r");
  if (probe == NULL) {
    free(candidate);
    return NULL;
  }
  fclose(probe);
  return candidate;
}

static int find_node_rule(const ccw_sched *s, const char *name, size_t *index)
{
  khint_t slot;
  if (s == NULL || name == NULL || index == NULL) return 0;
  slot = kh_get(sched_ruleset_by_name, s->ruleset_by_name, name);
  if (slot == kh_end(s->ruleset_by_name)) return 0;
  *index = kh_value(s->ruleset_by_name, slot);
  return 1;
}

int ccw_plan_apply_rewrites(const ccw_plan *plan, ccw_ir *ir,
                            const char *manifest_dir,
                            ccw_oeuph_budget budget, ccw_cost_model model,
                            ccw_oeuph_stats *stats, size_t stats_capacity,
                            size_t *stats_count, ccw_sched_error *e)
{
  ccw_sched *manifest = NULL;
  node *nodes = NULL;
  edge *edges = NULL;
  size_t nn = 0, ne = 0, required = 0, written = 0;
  bool *done = NULL;
  ccw_sched_error local_error;

  if (stats_count != NULL) *stats_count = 0;
  if (plan == NULL || ir == NULL || manifest_dir == NULL ||
      (stats == NULL && stats_capacity != 0)) {
    fail(e, 1, "invalid rewrite execution request");
    return 0;
  }
  if (!parse_plan_text(plan->text, &nodes, &nn, &edges, &ne, e)) return 0;

  manifest = ccw_sched_new("rewrite-execution", manifest_dir, &local_error);
  if (manifest == NULL) {
    if (e) *e = local_error;
    free_node_array(nodes, nn);
    free(edges);
    return 0;
  }
  for (size_t i = 0; i < nn; ++i)
    if (nodes[i].kind == NODE_REWRITE) required += nodes[i].nm;
  if (stats_count != NULL) *stats_count = required;
  if (stats != NULL && required > stats_capacity) {
    fail(e, 1, "rewrite stats buffer is too small");
    ccw_sched_free(manifest);
    free_node_array(nodes, nn);
    free(edges);
    return 0;
  }

  done = (bool *)calloc(nn, sizeof(*done));
  if (done == NULL) {
    fail(e, 1, "out of memory");
    ccw_sched_free(manifest);
    free_node_array(nodes, nn);
    free(edges);
    return 0;
  }

  for (size_t completed = 0; completed < nn; ++completed) {
    size_t selected = nn;
    for (size_t i = 0; i < nn; ++i) {
      bool ready = !done[i];
      if (!ready) continue;
      for (size_t j = 0; j < ne; ++j)
        if (edges[j].to == nodes[i].id && !done[edges[j].from - 1]) {
          ready = false;
          break;
        }
      if (ready) {
        selected = i;
        break; /* node ids are the deterministic tie-breaker */
      }
    }
    if (selected == nn) {
      fail(e, 6, "plan contains a cycle");
      free(done);
      ccw_sched_free(manifest);
      free_node_array(nodes, nn);
      free(edges);
      return 0;
    }

    if (nodes[selected].kind == NODE_REWRITE) {
      for (size_t j = 0; j < nodes[selected].nm; ++j) {
        size_t ruleset_index;
        char *path;
        char *load_error = NULL;
        ccw_oeuph_ruleset *ruleset;
        ccw_oeuph_stats local_stats;
        ccw_status status;

        if (!find_node_rule(manifest, nodes[selected].members[j],
                            &ruleset_index)) {
          fail(e, 8, "plan references a ruleset absent from Stdrewrite.yaml");
          free(done);
          ccw_sched_free(manifest);
          free_node_array(nodes, nn);
          free(edges);
          return 0;
        }
        path = ruleset_file_path(manifest_dir,
                                 manifest->rules[ruleset_index].path);
        if (path == NULL) {
          fail(e, 2, "ruleset path from Stdrewrite.yaml is unreadable");
          free(done);
          ccw_sched_free(manifest);
          free_node_array(nodes, nn);
          free(edges);
          return 0;
        }
        ruleset = ccw_oeuph_ruleset_load(path, &load_error);
        free(path);
        if (ruleset == NULL) {
          if (load_error != NULL) {
            snprintf(e ? e->message : local_error.message,
                     e ? sizeof(e->message) : sizeof(local_error.message),
                     "cannot load ruleset %s: %s",
                     nodes[selected].members[j], load_error);
            if (e) e->code = 2;
            free(load_error);
          } else {
            fail(e, 2, "cannot load selected ruleset");
          }
          free(done);
          ccw_sched_free(manifest);
          free_node_array(nodes, nn);
          free(edges);
          return 0;
        }
        if (strcmp(ccw_oeuph_ruleset_name(ruleset),
                   nodes[selected].members[j]) != 0) {
          ccw_oeuph_ruleset_destroy(ruleset);
          fail(e, 8, "ruleset name disagrees with Stdrewrite.yaml");
          free(done);
          ccw_sched_free(manifest);
          free_node_array(nodes, nn);
          free(edges);
          return 0;
        }
        memset(&local_stats, 0, sizeof(local_stats));
        status = ccw_oeuph_run(ir, ruleset, budget, model, &local_stats,
                               &load_error);
        ccw_oeuph_ruleset_destroy(ruleset);
        if (status != CCW_OK) {
          if (load_error != NULL) {
            fail(e, (int)status, load_error);
            free(load_error);
          } else {
            fail(e, (int)status, "Oeuph rewrite failed");
          }
          free(done);
          ccw_sched_free(manifest);
          free_node_array(nodes, nn);
          free(edges);
          return 0;
        }
        if (stats != NULL) stats[written] = local_stats;
        ++written;
        free(load_error);
      }
    }
    done[selected] = true;
  }

  if (stats_count != NULL) *stats_count = written;
  free(done);
  ccw_sched_free(manifest);
  free_node_array(nodes, nn);
  free(edges);
  return 1;
}

/* SHA-256 keeps release-pinned plan hashes portable without a host dependency. */
static unsigned rotr(unsigned x, unsigned n) { return (x >> n) | (x << (32 - n)); }
static void sha256_block(unsigned state[8], const unsigned char block[64]) {
  static const unsigned k[64]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2}; unsigned w[64],a,b,c,d,e,f,g,h,t1,t2;
  for(unsigned i=0;i<16;i++)w[i]=((unsigned)block[i*4]<<24)|((unsigned)block[i*4+1]<<16)|((unsigned)block[i*4+2]<<8)|block[i*4+3];
  for(unsigned i=16;i<64;i++){unsigned s0=rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3),s1=rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+s0+w[i-7]+s1;}
  a=state[0];b=state[1];c=state[2];d=state[3];e=state[4];f=state[5];g=state[6];h=state[7];
  for(unsigned i=0;i<64;i++){unsigned s1=rotr(e,6)^rotr(e,11)^rotr(e,25),ch=(e&f)^((~e)&g),s0=rotr(a,2)^rotr(a,13)^rotr(a,22),maj=(a&b)^(a&c)^(b&c);t1=h+s1+ch+k[i]+w[i];t2=s0+maj;h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
  state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
}
int ccw_plan_hash(const ccw_plan*p,char out[65]) {
  unsigned state[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};unsigned char block[64];size_t n,full,i;uint64_t bits;
  if(!p||!out) return 0;
  n=strlen(p->text); full=n/64;
  for(i=0;i<full;i++) sha256_block(state,(const unsigned char*)p->text+i*64);
  n-=full*64; memset(block,0,sizeof(block)); memcpy(block,p->text+full*64,n); block[n]=0x80;
  if(n>=56){sha256_block(state,block);memset(block,0,sizeof(block));}
  bits=(uint64_t)strlen(p->text)*8;
  for(i=0;i<8;i++) block[63-i]=(unsigned char)(bits>>(i*8));
  sha256_block(state,block);
  for(i=0;i<8;i++) sprintf(out+i*8,"%08x",state[i]);
  out[64]=0; return 1;
}
int ccw_plan_check(const char *path,const char *dir,ccw_sched_error *e) {
  FILE*f;char line[4096];ccw_sched*s;int ok=1;
  f=fopen(path,"r");if(!f){fail(e,7,"cannot read plan");return 0;}if(!fgets(line,sizeof line,f)||strcmp(line,"CCW-SCHED-PLAN 1\n")){fclose(f);fail(e,8,"invalid plan header");return 0;}
  s=ccw_sched_new("check",dir,e);if(!s){fclose(f);return 0;}
  while(fgets(line,sizeof line,f)) {
    char kind[16], name[2048]; unsigned id,type;
    if (sscanf(line,"node %u %u %2047s",&id,&type,name)==3) {
      char *members = strstr(line, name); size_t index;
      if (!members) { ok = 0; continue; }
      members += strlen(name);
      if(type==NODE_KERNEL) {
        khint_t slot = kh_get(sched_kernel_by_name, s->kernel_by_name, name);
        if (slot == kh_end(s->kernel_by_name)) ok=0;
        else index = kh_value(s->kernel_by_name, slot);
        while(ok && *members) { char cap[512]; if(sscanf(members," %511s",cap)!=1)break; if(!has_cap(&s->kernels[index],cap))ok=0; members=strchr(members+1,' '); if(!members)break; }
      } else if(type==NODE_REWRITE) {
        while(ok && *members) {
          char rule[512];
          if (sscanf(members, " %511s", rule) != 1) break;
          if (kh_get(sched_ruleset_by_name, s->ruleset_by_name, rule) == kh_end(s->ruleset_by_name)) ok = 0;
          members = strchr(members + 1, ' ');
          if (!members) break;
        }
      } else if(type!=NODE_BARRIER) ok=0;
    } else if (sscanf(line, "%15s", kind) != 1 ||
               (strcmp(kind, "name") && strcmp(kind, "edge"))) {
      ok = 0;
    }
  }
  fclose(f);ccw_sched_free(s);if(!ok)fail(e,8,"plan is invalidated by manifest drift");return ok;
}
