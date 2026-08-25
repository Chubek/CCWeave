# Specs for FrontML

I wish to do away with `swaff` and `kliche`, remove them altogether, and instead, implement `frontmx`. FrontMX folds the task of Swaff and Kliche into one: an AST-minded GLR parser, and our `.fmx` files are inside the `fmx-salvo` directory. FrontMX also encodes attributes such as types, limitations, semantic actions under `sema-salvo`, rewrites under `rewrite-salvo`, and so on.

`.fmx` files are S-Expression, parsed with SFSExp.

- **fmx-salvo** Directory
```
Cephyr.fmx
Moonix.fmx
Delphia.fmx
SML-Parthia.fmx
```

- **frontmx** Directory
```
ccw-frontmx.c
ccw-frontmx.h
src/
  sppf.c        # forest for parsed constructs
  gss.c         # graph-structured stack
  parser.c      # parses S-Expression .fmx files
  grammar.c     # the grammar in memory
  ast.c         # the AST in memory
  attr.c        # the attributes in memory
  diamb.c       # methods to disambiguate SPPF
  rewrite.c     # methods to rewrite and optimize the grammar
  generate.c    # generate parser, AST, attributes, walker
```

Example of a `.fmx` file (*very stub*):

```lisp
; Moonix.fmx — a GLR grammar for the Moonix expression language.
; This file is an S-Expression, parsed by SFSExp. It names the
; grammar only; types, limitations, and semantic actions live in
; sema-salvo (Stdsema.yaml), rewrites in rewrite-salvo (Stdrewrite.yaml),
; and are referenced here by rule name.

(frontmx
  (language Moonix)
  (grammar-version 1)
  (provenance
    (source "third_party/moonix/spec.md")
    (sha256 "63b4c2a1..."))

  ;; terminals — each may carry attributes, decoded by attr.c
  (terminals
    (int    (regex "[0-9]+")                (attr (type i64) (signed false)))
    (ident  (regex "[A-Za-z_][A-Za-z0-9_]*"))
    (plus   (regex "\+"))
    (star   (regex "\*"))
    (lparen (regex "\("))
    (rparen (regex "\)")))

  ;; productions — ambiguous alternatives are retained, not resolved
  ;; here; diamb.c decides after the SPPF is built.
  (productions
    (rule
      (name expr.add)
      (lhs expr)
      (rhs (expr $l plus expr $r))
      (attr (type i64) (width 64)))
    (rule
      (name expr.mul)
      (lhs expr)
      (rhs (expr $l star expr $r))
      (attr (type i64) (width 64)))
    (rule
      (name expr.int)
      (lhs expr)
      (rhs (int $v))
      (attr (type i64) (width 64)))
    (rule
      (name expr.group)
      (lhs expr)
      (rhs (lparen expr $e rparen))
      (attr (type-of $e))))

  ;; a limitation: a constraint that must hold on the parsed tree.
  ;; It maps to a sema rule in sema-salvo; a provable violation is a
  ;; compile-time error (see sema.range.int below).
  (limitations
    (limitation
      (name frontmx.limit.int-width)
      (where (match (int $v)))
      (that (in-range $v i64))
      (violation error)))

  ;; semantic actions, named exactly as in Stdsema.yaml
  (semantics
    (action (use sema.type.expr-plus)  (on (expr $l plus $r)))
    (action (use sema.range.int)       (on (int $v)))
    (action (use sema.wasi.mem)        (on (ident $id))))

  ;; rewrites, named exactly as in Stdrewrite.yaml
  (rewrites
    (use rewrite.add-zero)
    (use rewrite.mul-by-one)
    (use rewrite.assoc-add))

  (entry (node expr)))

;; The GLR engine (parser.c) produces an SPPF (sppf.c) over these
;; productions; diamb.c resolves the expr.add / expr.mul ambiguity on
;; operator precedence; attr.c materialises `(type i64)` etc.; generate.c
;; emits the parser, AST, attribute table, and walker.
```

---

## 1. Model

FrontMX is the front end of `ccweave`. It replaces two older tools:

- **`swaff`** — the old parser generator. Its role is absorbed by the SPPF/GSS pipeline.
- **`kliche`** — the old attribute/action binder. Its role is absorbed by `attr.c` plus the `sema-salvo` / `rewrite-salvo` directories.

One `.fmx` file is a **grammar description**, not a program. It declares terminals, productions, limitations, and *names* the semantic actions and rewrites it needs. The actual semantics are not inline in the `.fmx`; they live in the salvo directories and are looked up by rule name.

The house rule set carries through unchanged: a **sema rule** and a **rewrite rule** both have exactly the fields `name`, `description`, `trigger`, `target`, `gating`, so a `.fmx` reference is a bare name:

```yaml
name: sema.type.expr-plus
description: "Addend types agree on (expr $l + $r)"
trigger: (expr $l plus $r)
target: (require (type-eq (type-of $l) (type-of $r)))
gating: true
```

The trigger is an S-Expression in the grammar, the target is the action (`assert` / `require` / `error` / `warn`).

---

## 2. Files and their contract

| File | Contract |
|---|---|
| `ccw-frontmx.h` | Public surface: `frontmx_parse()`, `frontmx_generate()`, the `FMX` handle and the node/attr/cursor types. Only this header is importable by other `ccweave` components. |
| `ccw-frontmx.c` | Driver. Reads the `.fmx`, runs the SPPF pipeline, checks limitations, dispatches sema/rewrite lookups, writes the generator outputs. |
| `src/parser.c` | Reads `.fmx` via **SFSExp**, converts the S-Expressions into the in-memory grammar. This is the *grammar* front, not the tokeniser for the target language. |
| `src/sppf.c` | Shared Packed Parse Forest. One node per parsed construct; ambiguity is retained as packed alternatives. |
| `src/gss.c` | Graph-Structured Stack. The GLR algorithm stack — O(1) merging of ambiguous parse states. |
| `src/grammar.c` | The grammar in memory: terminals, productions, precedence, associative declarations. |
| `src/diamb.c` | Disambiguation of the SPPF: precedence, associativity, and semantic predicates (attributes carried up the AST). |
| `src/ast.c` | The AST in memory: typed nodes, child order, source spans, ownership. |
| `src/attr.c` | Attribute table in memory: constant `(type i64)`, computed `(type-of $e)`; resolves a node's attribute by walking the SPPF. |
| `src/rewrite.c` | Applies `rewrite-salvo` rules to the *grammar* (not the user's AST): eliminating useless productions, refactoring `expr` chains, folding constant shapes. |
| `src/generate.c` | Emits the generated parser, AST definition, attribute accessors, and walker for the target. |

The pipeline is strictly one-way:
```
.fmx ──parser.c──> grammar.c ──> sppf.c / gss.c ──> diamb.c ──> ast.c ──> attr.c
                                                                  |
                        rewrite.c (rewrite-salvo) <──────────────┘
                                                                  |
                                                     generate.c ──> frontend artifacts


---
```
## 3. The `.fmx` schema (SFSExp)

A `.fmx` is a single top-level `(frontmx ...)` form. SFSExp parses it into a document; `parser.c` validates it against the schema below. Unknown or misspelled keys are errors, never silently ignored.

| Key | Card. | Meaning |
|---|---|---|
| `language` | `1` | The target language name (e.g. `Moonix`). Must match the file basename. |
| `grammar-version` | `1` | Integer schema version for the `.fmx` format. |
| `provenance` | `0..1` | `source` + `sha256` for the upstream language spec. |
| `terminals` | `0..1` | `(name (regex "..."))`, optional `(attr ...)` and `(limitation ...)`. |
| `productions` | `1` | List of `rule`s. Each has `name`, `lhs`, `rhs`, optional `attr`, optional `precedence`/`assoc`. |
| `limitations` | `0..1` | `limitation`s: `(where (match ...))`, `(that <form>)`, `(violation error|warn)`. |
| `semantics` | `0..1` | `(action (use <sema-rule-name>) (on <pattern>))`. |
| `rewrites` | `0..1` | `(use <rewrite-rule-name>)`. |
| `entry` | `1` | `(node <nonterminal>)` — the start symbol. |

Notes:

- Ambiguity is **retained**, not resolved, in the `.fmx`. The `expr.add` / `expr.mul` overlap in the example above is legal; `diamb.c` decides it with precedence.
- `limitations` are a separate concept from `sema` rules. A limitation is *structural* — it pins a constraint to a production. A sema rule is *semantic* and lives in `sema-salvo`. The two are linked by name in `(action (use ...))`.
- Rewrites apply to the **grammar**, so `rewrite-salvo` triggers still match S-Expr shapes in the productions, exactly as they do over IR today.

---

## 4. Integration with the manifests

FrontMX is registered the same way any other `ccweave` kernel is: a kernel entry plus a capability list.

Kernel entry (following the `Kernel.yaml` schema of `path / library / name / version / description / capabilities`):

```yaml
- path: kernels/frontmx-parse.scm
  library: (ccweave kernel frontmx-parse)
  name: frontmx-parse
  version: 0.1.0
  description: Parses .fmx S-Expression grammars with SFSExp and builds the in-memory grammar.
  capabilities:
    - front.parse

- path: kernels/frontmx-diamb.scm
  library: (ccweave kernel frontmx-diamb)
  name: frontmx-diamb
  version: 0.1.0
  description: Disambiguates the SPPF using precedence and semantic attributes.
  capabilities:
    - front.diamb

- path: kernels/frontmx-generate.scm
  library: (ccweave kernel frontmx-generate)
  name: frontmx-generate
  version: 0.1.0
  description: Emits the parser, AST, attribute table, and walker for the target frontend.
  capabilities:
    - front.generate
```

Capability declarations (the `Capabilities.yaml` mapping — name → kernel paths):

```yaml
front.parse:
  - kernels/frontmx-parse.scm
front.diamb:
  - kernels/frontmx-diamb.scm
front.ast:
  - kernels/frontmx-ast.scm
front.generate:
  - kernels/frontmx-generate.scm
```

The `capabilities:` lists above name only the kernels that are *new* for FrontMX. `front.ast` is provided by `kernels/frontmx-ast.scm`; if you prefer to fold AST building into `frontmx-diamb.scm`, drop the third kernel and merge its capability into `front.diamb`.

---

## 5. What gets removed

- `swaff` and `kliche` — both deleted. No legacy `.swaff`/`.kliche` files, no registered capabilities, no kernel entries. Any `Capabilities.yaml` or `Kernel.yaml` reference to them is removed.
- Their old outputs are replaced by the `generate.c` products.

`swaff`, `kliche`, `frontmx`, and `parser` do not appear in the current `Kernel.yaml`, `Kernel.yaml`, `Capabilities.yaml`, or `Stdrewrite.yaml` — confirming nothing depends on them yet, so this is a clean add.

---

If you'd like, I can next write out the **full 41-rule grammar rewrite set** for an example language (mirroring `Stdrewrite.yaml`), or flesh out `Moonh one is a real, working grammar rather than a placeholder. Which would you prefer?
ix.fmx` into a real, working grammar.

***

### 5. `rewrite-salvo/Moonix-rewrite.yaml` (Sample)

Following the `Stdrewrite.yaml` contract, these rules target the grammar productions themselves, allowing `rewrite.c` to optimize the SPPF before disambiguation.

```yaml
- name: rewrite.add-zero
  description: "Simplify identity addition: expr.add(x, 0) -> x"
  trigger: (rule (name expr.add) (rhs (expr $x plus 0)))
  target: (replace-rhs $x)
  gating: true

- name: rewrite.mul-by-one
  description: "Simplify identity multiplication: expr.mul(x, 1) -> x"
  trigger: (rule (name expr.mul) (rhs (expr $x star 1)))
  target: (replace-rhs $x)
  gating: true

- name: rewrite.assoc-add
  description: "Right-associate addition: (a + b) + c -> a + (b + c)"
  trigger: (rule (name expr.add) (rhs (expr (expr $a plus $b) plus $c)))
  target: (rule (name expr.add) (rhs (expr $a plus (expr $b plus $c))))
  gating: true

- name: rewrite.fold-constants
  description: "Constant folding: 1 + 1 -> 2"
  trigger: (rule (name expr.add) (rhs (expr (int 1) plus (int 1))))
  target: (rule (name expr.int) (rhs (int 2)))
  gating: (eval-const)

# ... (Additional 38 rules continue for complex transformations)
```

### 6. `fmx-salvo/Moonix.fmx` (Refined)

To move `Moonix.fmx` from placeholder to functional grammar, we define the precedence and associativity explicitly, which `diamb.c` will use to collapse the ambiguous `expr` SPPF.

```lisp
(frontmx
  (language Moonix)
  (grammar-version 1)
  
  (terminals
    (int    (regex "[0-9]+")                (attr (type i64)))
    (ident  (regex "[A-Za-z_][A-Za-z0-9_]*"))
    (plus   (regex "\+"))
    (star   (regex "\*"))
    (lparen (regex "\("))
    (rparen (regex "\)")))

  (productions
    ;; Precedence: mul (20) > add (10)
    (rule
      (name expr.add)
      (lhs expr)
      (rhs (expr $l plus expr $r))
      (precedence 10) (assoc left))
    (rule
      (name expr.mul)
      (lhs expr)
      (rhs (expr $l star expr $r))
      (precedence 20) (assoc left))
    (rule
      (name expr.int)
      (lhs expr)
      (rhs (int $v)))
    (rule
      (name expr.group)
      (lhs expr)
      (rhs (lparen expr $e rparen))
      (precedence 30)))

  (limitations
    (limitation
      (name frontmx.limit.int-width)
      (where (match (int $v)))
      (that (in-range $v i64))
      (violation error)))

  (semantics
    (action (use sema.type.expr-plus)  (on (expr $l plus $r)))
    (action (use sema.range.int)       (on (int $v))))

  (rewrites
    (use rewrite.add-zero)
    (use rewrite.mul-by-one)
    (use rewrite.assoc-add))

  (entry (node expr)))
```

This configuration allows `parser.c` to construct a fully ambiguous forest for `1 + 2 * 3` and enables `diamb.c` to resolve it into `1 + (2 * 3)` by checking the precedence values `10` and `20` associated with the nodes. The `rewrite` pass runs *before* the forest is fully realized, effectively cleaning the grammar surface before the GLR engine consumes it.
