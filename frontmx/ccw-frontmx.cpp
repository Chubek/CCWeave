#include "ccw-frontmx.h"

#include <re2/re2.h>
#include <sexp.h>

#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <climits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

struct FMXNode {
  std::string name, lhs, regex, rhs, attr, pattern, reference, violation;
  std::unique_ptr<RE2> compiled;
};
struct FMX {
  std::string language, entry, last_error;
  int version = 0;
  std::vector<FMXNode> terminals, productions, limitations, semantics,
      rewrites;
};

static void set_error(fmx_error *e, const std::string &msg, size_t off = 0) {
  if (e) {
    char *p = static_cast<char *>(std::malloc(msg.size() + 1));
    if (p) {
      std::memcpy(p, msg.c_str(), msg.size() + 1);
      e->message = p;
      e->offset = off;
    } else {
      e->message = "out of memory";
      e->offset = off;
    }
  }
}
static std::string atom(const sexp_t *s) {
  if (!s || s->ty != SEXP_VALUE || !s->val) return {};
  size_t n = s->val_used;
  while (n && s->val[n - 1] == '\0') --n;
  return std::string(s->val, n);
}
static const sexp_t *nth(const sexp_t *s, size_t n) {
  for (; s && n; s = s->next, --n) {}
  return s;
}
static const sexp_t *head(const sexp_t *s) {
  return s && s->ty == SEXP_LIST ? s->list : nullptr;
}
static const sexp_t *field(const sexp_t *list, const char *key) {
  for (const sexp_t *p = list; p; p = p->next) {
    const sexp_t *h = head(p);
    if (h && atom(h) == key) return p;
  }
  return nullptr;
}
static std::string field_atom(const sexp_t *list, const char *key) {
  const sexp_t *f = field(list, key);
  return f ? atom(nth(head(f), 1)) : std::string();
}
static std::string print_form(const sexp_t *s) {
  if (!s) return {};
  int n = print_sexp(nullptr, 0, s);
  if (n <= 0) return {};
  std::string out(static_cast<size_t>(n) + 1, '\0');
  print_sexp(out.data(), out.size(), s);
  out.resize(std::strlen(out.c_str()));
  return out;
}
static bool is_list_named(const sexp_t *s, const char *name) {
  const sexp_t *h = head(s);
  return h && atom(h) == name;
}

static bool parse_named_list(const sexp_t *container, const char *name,
                             std::vector<FMXNode> &out, std::string &why) {
  const sexp_t *items = nth(head(container), 1);
  for (const sexp_t *p = items; p; p = p->next) {
    if (!is_list_named(p, name)) {
      why = std::string("unknown item in ") + name + " list";
      return false;
    }
    const sexp_t *body = nth(head(p), 1);
    FMXNode n;
    if (std::strcmp(name, "rule") == 0) {
      n.name = field_atom(body, "name");
      n.lhs = field_atom(body, "lhs");
      const sexp_t *rhs = field(body, "rhs");
      n.rhs = rhs ? print_form(rhs) : "";
      n.attr = print_form(field(body, "attr"));
      if (n.name.empty() || n.lhs.empty() || !rhs) {
        why = "production requires name, lhs and rhs";
        return false;
      }
    } else if (std::strcmp(name, "limitation") == 0) {
      n.name = field_atom(body, "name");
      const sexp_t *w = field(body, "where");
      const sexp_t *t = field(body, "that");
      n.pattern = w ? print_form(w) : "";
      n.attr = t ? print_form(t) : "";
      n.violation = field_atom(body, "violation");
      if (n.name.empty() || !w || !t ||
          (n.violation != "error" && n.violation != "warn")) {
        why = "limitation requires name, where, that and violation error|warn";
        return false;
      }
    }
    out.push_back(std::move(n));
  }
  return true;
}
static bool allowed_top_key(const std::string &key) {
  return key == "language" || key == "grammar-version" || key == "provenance" ||
         key == "terminals" || key == "productions" || key == "limitations" ||
         key == "semantics" || key == "rewrites" || key == "entry";
}
static size_t count_key(const sexp_t *list, const char *key) {
  size_t n = 0;
  for (const sexp_t *p = list; p; p = p->next) {
    const sexp_t *h = head(p);
    if (h && atom(h) == key) ++n;
  }
  return n;
}

extern "C" FMX *frontmx_parse(const char *source, size_t length,
                               fmx_error *error) {
  if (error) *error = {nullptr, 0};
  if (!source) {
    set_error(error, "source is null");
    return nullptr;
  }
  std::string input(source, length);
  sexp_t *doc = parse_sexp(input.data(), input.size());
  if (!doc) {
    set_error(error, "invalid S-expression");
    return nullptr;
  }
  const sexp_t *top = head(doc);
  if (!top || atom(top) != "frontmx") {
    destroy_sexp(doc);
    set_error(error, std::string("document must contain exactly one (frontmx ...) form; got '") +
                       (top ? atom(top) : "<null>") + "'");
    return nullptr;
  }
  const sexp_t *body = top->next;
  for (const sexp_t *p = body; p; p = p->next) {
    const sexp_t *h = head(p);
    if (!h || !allowed_top_key(atom(h))) {
      destroy_sexp(doc);
      set_error(error, "unknown key in frontmx document");
      return nullptr;
    }
  }
  const char *required[] = {"language", "grammar-version", "productions", "entry"};
  for (const char *key : required) {
    if (count_key(body, key) != 1) {
      destroy_sexp(doc);
      set_error(error, std::string("required key must occur exactly once: ") + key);
      return nullptr;
    }
  }
  const char *optional[] = {"provenance", "terminals", "limitations", "semantics",
                            "rewrites"};
  for (const char *key : optional) {
    if (count_key(body, key) > 1) {
      destroy_sexp(doc);
      set_error(error, std::string("key occurs more than once: ") + key);
      return nullptr;
    }
  }
  std::unique_ptr<FMX> f(new FMX);
  f->language = field_atom(body, "language");
  std::string ver = field_atom(body, "grammar-version");
  const sexp_t *entry_field = field(body, "entry");
  const sexp_t *entry_body = entry_field ? nth(head(entry_field), 1) : nullptr;
  f->entry = entry_body ? field_atom(entry_body, "node") : "";
  if (f->language.empty() || ver.empty() || f->entry.empty()) {
    destroy_sexp(doc);
    set_error(error, "language, grammar-version and entry are required");
    return nullptr;
  }
  char *end = nullptr;
  long v = std::strtol(ver.c_str(), &end, 10);
  if (*end || v < 1 || v > INT_MAX) {
    destroy_sexp(doc);
    set_error(error, "grammar-version must be a positive integer");
    return nullptr;
  }
  f->version = static_cast<int>(v);

  std::string why;
  const sexp_t *terms = field(body, "terminals");
  if (terms) {
    for (const sexp_t *p = nth(head(terms), 1); p; p = p->next) {
      const sexp_t *h = head(p);
      if (!h || h->next == nullptr) {
        destroy_sexp(doc); set_error(error, "invalid terminal declaration");
        return nullptr;
      }
      FMXNode n; n.name = atom(h);
      const sexp_t *tb = head(h->next);
      if (!tb || atom(tb) != "regex") {
        destroy_sexp(doc); set_error(error, "terminal requires regex");
        return nullptr;
      }
      n.regex = atom(nth(tb, 1));
      if (n.name.empty() || n.regex.empty()) {
        destroy_sexp(doc); set_error(error, "terminal name and regex are required");
        return nullptr;
      }
      n.attr = print_form(field(h->next, "attr"));
      n.compiled.reset(new RE2(n.regex));
      if (!n.compiled->ok()) {
        std::string msg = "invalid terminal regex: " + n.compiled->error();
        destroy_sexp(doc); set_error(error, msg); return nullptr;
      }
      f->terminals.push_back(std::move(n));
    }
  }
  const sexp_t *prod = field(body, "productions");
  if (!prod || !parse_named_list(prod, "rule", f->productions, why)) {
    destroy_sexp(doc); set_error(error, why.empty() ? "productions are required" : why);
    return nullptr;
  }
  const sexp_t *lims = field(body, "limitations");
  if (lims && !parse_named_list(lims, "limitation", f->limitations, why)) {
    destroy_sexp(doc); set_error(error, why); return nullptr;
  }
  const sexp_t *semas = field(body, "semantics");
  if (semas) for (const sexp_t *p = nth(head(semas), 1); p; p = p->next) {
    if (!is_list_named(p, "action")) { why = "unknown item in semantics list"; break; }
    const sexp_t *b = nth(head(p), 1); FMXNode n;
    n.reference = field_atom(b, "use"); const sexp_t *on = field(b, "on");
    n.pattern = on ? print_form(on) : "";
    if (n.reference.empty() || !on) { why = "semantic action requires use and on"; break; }
    f->semantics.push_back(std::move(n));
  }
  if (!why.empty()) { destroy_sexp(doc); set_error(error, why); return nullptr; }
  const sexp_t *rews = field(body, "rewrites");
  if (rews) for (const sexp_t *p = nth(head(rews), 1); p; p = p->next) {
    if (!is_list_named(p, "use")) { destroy_sexp(doc); set_error(error, "rewrites accepts only (use name)"); return nullptr; }
    FMXNode n; n.reference = atom(nth(head(p), 1));
    if (n.reference.empty()) { destroy_sexp(doc); set_error(error, "rewrite name is required"); return nullptr; }
    f->rewrites.push_back(std::move(n));
  }
  destroy_sexp(doc);
  return f.release();
}

extern "C" FMX *frontmx_parse_file(const char *path, fmx_error *error) {
  if (!path) { set_error(error, "path is null"); return nullptr; }
  std::ifstream in(path, std::ios::binary);
  if (!in) { set_error(error, std::strerror(errno)); return nullptr; }
  std::ostringstream ss; ss << in.rdbuf(); std::string s = ss.str();
  FMX *f = frontmx_parse(s.data(), s.size(), error);
  if (f) {
    std::string p(path);
    size_t slash = p.find_last_of("/\\");
    size_t dot = p.find_last_of('.');
    std::string stem = p.substr(slash == std::string::npos ? 0 : slash + 1,
                                (dot == std::string::npos ? p.size() : dot) -
                                (slash == std::string::npos ? 0 : slash + 1));
    if (stem != f->language) {
      set_error(error, "language does not match .fmx basename");
      delete f;
      return nullptr;
    }
  }
  return f;
}
extern "C" void frontmx_error_free(fmx_error *e) {
  if (e && e->message) {
    std::free(const_cast<char *>(e->message));
    e->message = nullptr;
    e->offset = 0;
  }
}
extern "C" void frontmx_free(FMX *fmx) { delete fmx; }
extern "C" const char *frontmx_language(const FMX *f) { return f ? f->language.c_str() : nullptr; }
extern "C" int frontmx_grammar_version(const FMX *f) { return f ? f->version : 0; }
extern "C" const char *frontmx_entry(const FMX *f) { return f ? f->entry.c_str() : nullptr; }
extern "C" size_t frontmx_terminal_count(const FMX *f) { return f ? f->terminals.size() : 0; }
extern "C" size_t frontmx_production_count(const FMX *f) { return f ? f->productions.size() : 0; }
extern "C" size_t frontmx_limitation_count(const FMX *f) { return f ? f->limitations.size() : 0; }
extern "C" size_t frontmx_semantic_count(const FMX *f) { return f ? f->semantics.size() : 0; }
extern "C" size_t frontmx_rewrite_count(const FMX *f) { return f ? f->rewrites.size() : 0; }
#define FMX_REF(kind, field) extern "C" const FMXNode *frontmx_##kind(const FMX *f, size_t i) { return f && i < f->field.size() ? &f->field[i] : nullptr; }
FMX_REF(terminal, terminals) FMX_REF(production, productions) FMX_REF(limitation, limitations) FMX_REF(semantic, semantics) FMX_REF(rewrite, rewrites)
extern "C" const char *frontmx_node_name(const FMXNode *n) { return n ? n->name.c_str() : nullptr; }
extern "C" const char *frontmx_node_lhs(const FMXNode *n) { return n ? n->lhs.c_str() : nullptr; }
extern "C" const char *frontmx_node_regex(const FMXNode *n) { return n ? n->regex.c_str() : nullptr; }
extern "C" const char *frontmx_node_rhs(const FMXNode *n) { return n ? n->rhs.c_str() : nullptr; }
extern "C" const char *frontmx_node_attr(const FMXNode *n) { return n ? n->attr.c_str() : nullptr; }
extern "C" const char *frontmx_node_pattern(const FMXNode *n) { return n ? n->pattern.c_str() : nullptr; }
extern "C" const char *frontmx_node_reference(const FMXNode *n) { return n ? n->reference.c_str() : nullptr; }
extern "C" const char *frontmx_node_violation(const FMXNode *n) { return n ? n->violation.c_str() : nullptr; }
extern "C" int frontmx_terminal_matches(const FMXNode *n, const char *text, size_t length) {
  if (!n || !n->compiled || !text) return 0;
  return RE2::FullMatch(re2::StringPiece(text, length), *n->compiled) ? 1 : 0;
}
extern "C" const char *frontmx_last_error(const FMX *f) { return f ? f->last_error.c_str() : nullptr; }

extern "C" fmx_status frontmx_generate(const FMX *f, const char *dir,
                                         fmx_error *error) {
  if (!f || !dir) { set_error(error, "fmx and output directory are required"); return FMX_INVALID_ARGUMENT; }
  std::string base(dir);
  if (!base.empty() && base.back() != '/') base += '/';
  std::string stem = f->language;
  std::string guard = stem;
  for (char &ch : guard)
    if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_')) ch = '_';
  std::ofstream h(base + stem + ".h"), c(base + stem + ".c");
  if (!h || !c) { set_error(error, "cannot create generated frontend artifacts"); return FMX_IO_ERROR; }
  h << "/* Generated by FrontMX; do not edit. */\n#ifndef FMX_GENERATED_" << guard << "_H\n#define FMX_GENERATED_" << guard << "_H\n";
  h << "#define FMX_LANGUAGE \"" << stem << "\"\n#define FMX_ENTRY \"" << f->entry << "\"\n#define FMX_TERMINAL_COUNT " << f->terminals.size() << "\n#define FMX_PRODUCTION_COUNT " << f->productions.size() << "\n#endif\n";
  c << "/* Generated by FrontMX; grammar metadata only. */\n#include \"" << stem << ".h\"\n";
  std::ofstream parser(base + "parser.c"), ast(base + "ast.h"),
      attrs(base + "attrs.h"), walker(base + "walker.c");
  if (!parser || !ast || !attrs || !walker) {
    set_error(error, "cannot create complete generated frontend artifacts");
    return FMX_IO_ERROR;
  }
  parser << "/* Generated parser entry point for " << stem << ". */\n";
  ast << "/* Generated AST definition for " << stem << ". */\n";
  attrs << "/* Generated attribute accessors for " << stem << ". */\n";
  walker << "/* Generated AST walker for " << stem << ". */\n";
  return FMX_OK;
}
