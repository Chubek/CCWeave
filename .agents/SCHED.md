# Sched Subsystem Overview

This document specifies the Sched subsystem, implemented in the `sched` directory. The job of Sched is to provide a *scheduler* and *orchestrator* for mixing kernels (under `kernels`) and rewriters (under `stdrewrite`) using Lua 5.5 (provided under `third_party/lua`), using a DAG. Using the Sched subsystem, compilers and interpreters can create DAGs made up of kernels and rewriters, and make use of them.

The Sched subsystem can optionally *probe* kernels and rewriters for capabilities and features, so it can create the DAG based on the feature and/or capability instead of hard-wiring specific kernels/rewriters.

This document is **normative**. Key words MUST, MUST NOT, SHOULD, and MAY are to be interpreted as in RFC 2119.

## 1. Position in the stack

Sched is **host-side** code (C11, same conformance rules as the Cephyr host per `AGENTS.md`) embedding **Lua 5.5**. Sched itself never touches IR:

- Kernels remain R7RS Scheme libraries invoked through the GlueSTD ABI v1. All IR crosses the boundary as `uint64_t` node IDs (0 = nil), and all structural mutation remains builder-based (`instr-build`, `instr-replace!`, ...).
- Rewriters remain Stdrewrite rulesets, subject to the equivalence-only rule (D-0007).
- Sched's sole authority is **ordering and selection**: it decides *which* kernels/rulesets run and *in what order*. It MUST NOT provide any Lua API that reads or mutates IR nodes.

A Sched script therefore cannot violate IR invariants; the worst a malformed script can do is produce an invalid plan, which is rejected before anything runs.

## 2. Terminology

- **Script** — a Lua file executed by Sched to construct a plan. Scripts are *user code*, not generated artifacts; they are exempt from the `ccw-manifest --check` regime.
- **Plan** — the sealed, immutable DAG produced by a script. Plans ARE artifacts: they are serializable, hashable, and checkable.
- **Node** — a unit of work in the plan: a kernel instance, a ruleset batch, or a barrier.
- **Edge** — a happens-before constraint between two nodes.
- **Probe** — a query against the manifests that resolves a capability/feature to a concrete kernel or ruleset, or reports absence.

## 3. Directory layout

```
sched/
  sched.c  sched.h        -- planner core, DAG validation, plan serialization
  sched_lua.c             -- Lua 5.5 embedding and the `sched` table
  sched_probe.c           -- manifest-backed probing
  plans/                  -- serialized plans (generated; ignored per .gitignore)
```
## 4. The Lua environment

Scripts run in a **sandboxed** Lua 5.5 state:

- Lua is provided under `third_party/lua`. It must be added to the build machinery.
- Available: base library minus `dofile`/`loadfile`/`load`, plus `string`, `table`, `math` (with `math.random` removed).
- Unavailable: `io`, `os`, `require`, `package`, `debug`, coroutines.
- The single entry point is the global `sched` table.
- Script execution MUST be deterministic: given identical manifests and an identical script, the resulting plan (and its hash) MUST be byte-identical. Iteration over probe results is defined to be in manifest order for this reason.

## 5. API

### 5.1 Construction

- `sched.new(name)` — returns a scheduler object `S` for a named pipeline. The name identifies the plan in diagnostics and in the serialized artifact; it carries no semantics.

### 5.2 Selection and probing

- `S:require { kernel = NAME }` — resolve a kernel by manifest `name`. Errors if absent.
- `S:require { capability = CAP }` — resolve by capability (e.g. `"opt.gvn"`). Errors if no kernel in `Kernel.yaml` declares it; errors if ambiguous, unless `prefer = NAME` is given.
- `S:probe { ... }` — same query forms as `require`, but returns `nil` instead of erroring. This is the mechanism for capability-based fallback.
- `S:rewrite(PATTERN)` — select Stdrewrite rulesets by glob against ruleset names in `Stdrewrite.yaml` (e.g. `"arith.*"`). Returns one batch node; an empty match is an error.

All resolution is performed **exclusively** against the generated manifests (`Kernel.yaml`, `Stdrewrite.yaml`, `Capabilities.yaml`). Sched MUST NOT scan the filesystem; a kernel that exists on disk but not in the manifest does not exist for Sched.

### 5.3 Ordering

- `S:edge(a, b)` — node `a` happens-before node `b`.
- `S:barrier(label)` — returns a barrier node. Every node added before the barrier's creation is implicitly ordered before it; every node added after is implicitly ordered after it.

Nodes with no path between them are **unordered**, and the host MAY run them concurrently. Two unordered nodes that both mutate IR are legal — the ABI's host-interposition point serializes builder transactions — but a plan checker SHOULD warn when unordered mutating nodes share a capability domain.

### 5.5 Sealing

- `S:seal()` — validates and freezes the plan; the script MUST return its result. After sealing, all mutating methods on `S` error. Validation enforces: acyclicity, all edges refer to nodes of this plan, at least one non-barrier node, and no dangling probe handles (a `nil` from `probe` passed to `edge` is an error at the `edge` call, not at seal time).

## 6. Execution model

The host consumes a sealed plan and runs nodes in any order consistent with the edges:

1. Kernel nodes are invoked through GlueSTD ABI v1. A kernel's builder transactions may be interposed or **rejected** by the host; rejection of a transaction does not fail the node.
2. Ruleset batch nodes apply their rulesets to fixpoint or to the host's rewrite budget, whichever comes first.
3. Barrier nodes perform no work.

A node that signals an error fails the plan; downstream nodes do not run, and unordered in-flight nodes run to completion. Partial results are kept — equivalence-only rewriting (D-0007) and builder-based mutation guarantee the IR is valid at every commit point, so a failed plan leaves valid (if partially processed) IR.

## 7. Plan artifacts and conformance

- `ccw-sched --check PLAN` validates a serialized plan against the current manifests: every referenced kernel/ruleset must still exist and declare the capabilities recorded at seal time. Plans are invalidated by manifest drift, not silently re-resolved.
- `ccw-sched --hash PLAN` prints the canonical plan hash. CI SHOULD pin plan hashes for release pipelines.
- Serialized plans live in `sched/plans/` and are build artifacts (excluded from version control per the current `.gitignore` policy).

## 8. Example

```lua
S = sched.new "Reg"

-- Probe: prefer the graph-coloring allocator, fall back to linear scan.
local ra = S:probe   { capability = "codegen.regalloc-graph" }
        or S:require { capability = "codegen.regalloc-linear" }

-- Analyses and the passes that consume their facts.
local escape  = S:require { capability = "analysis.escape" }
local memdep  = S:require { capability = "analysis.memdep" }
local m2r     = S:require { kernel = "mem2reg" }        -- consumes analysis.escape
local sink    = S:require { capability = "opt.sink" }   -- consumes analysis.memdep
local gvn     = S:require { capability = "opt.gvn" }

-- Equivalence-only cleanup between the SSA passes and codegen.
local arith   = S:rewrite "arith.*"
local bitwise = S:rewrite "bitwise.*"

S:edge(escape, m2r)
S:edge(memdep, sink)
S:edge(m2r, gvn)
S:edge(sink, gvn)
S:edge(gvn, arith)
S:edge(gvn, bitwise)      -- arith and bitwise are unordered; host may parallelize

local pre_ra = S:barrier "pre-regalloc"
S:edge(pre_ra, ra)

return S:seal()
```

## 9. Open items / candidate decisions

- **D-0016 (proposed)** — Lua 5.5 as the sole scripting surface for Sched; no alternative frontends.
- **D-0017 (proposed)** — plans are re-validated, never re-resolved: manifest drift invalidates a sealed plan.
- **D-0018 (proposed)** — dependence edges between analyses and their consumers (e.g. `analysis.escape` → `mem2reg`) are the script author's responsibility in v0.1. If `Capabilities.yaml` gains a `requires:` field, `seal()` SHOULD auto-insert these edges and this section will be revised.
`

