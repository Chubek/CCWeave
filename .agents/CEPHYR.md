# Cephyr Specification v0.1

Cephyr is a C compiler built as the first consumer of the CCWeave infrastructure. It lives under `compilers/cephyr/` and MUST NOT fork or shadow any CCWeave component: it uses Swaff for parsing, Kliche for stereotype lowering, Weave IR with the **Tilly profile**, Oeuph/`rewrite-salvo` for expression rewriting, Kernels via Glue for everything from SSA construction to code generation, and the **Sched** subsystem to orchestrate them into an ordered plan.

> **Naming note.** Throughout this document, *Sched* (capitalized) refers to the CCWeave scheduler/orchestrator subsystem under `sched/` that builds DAGs of kernels and rulesets. The `sched.list` 
> **kernel** is the instruction-scheduling capability in the backend (§7); it is unrelated to the Sched subsystem and is always written in lowercase with its capability prefix.

## 1. Scope and goals

1. Cephyr targets **ISO C17** (ISO/IEC 9899:2018), hosted implementation, in phases (§3).
2. Cephyr is a static ahead-of-time compiler. It uses the Tilly profile exclusively; On1x constructs (inline caches, deopt metadata, safepoints) are forbidden anywhere in a Cephyr pipeline.
3. Cephyr targets all the `codegen-*` kernels.
4. Cephyr assembles its optimization and codegen pipeline as a **sealed Sched plan** (§8). It MUST NOT invoke kernels or rulesets directly outside a plan; the plan is the single authority on which capabilities run and in what order.
5. Cephyr has a plugin interface. This interface is declared in `compilers/cephyr/include/cephyr-module.h`. The plugin interface allows for all types of extensions, e.g. GNU C-style `__attribute__`'s, LTO, and new optimizations and/or semantic analyses. We define a standard plugin bundle in `compilers/cephyr/stdmodule`. Among these standard plugins is the whole host of GNU C's `__attribute__`'s, a basic LTO, and so on.

**Note**: all the operations, e.g. `__attribute__`'s or LTO, must be defined via kernel under `kernels` directory. Any operation that we need to add to Cephyr wherein it lacks a kernel, we must first implement the kernel for it (and update `ir` and `glue` accordingly). Plugins that add pipeline stages do so by contributing Sched script fragments (§8.5), never by mutating IR from host code.

## 2. Position in the CCWeave stack
```
source.c
  → cephyr-cpp        (translation phases 1–6, Cephyr-owned, C11 host code)
  → Swaff frontend    (tree-sitter-c grammar + Cephyr lowering adapter)
  → typed AST + sema  (Cephyr-owned)
  → Kliche imperative stereotype
  → Weave IR (core)   ─┐
  → Weave IR (Tilly)  ─┴─ orchestrated by a sealed Sched plan:
                          Oeuph/rewrite-salvo, norm.*, ssa.*, opt.*,
                          regalloc.*, isel.*, sched.list, codegen.x86-64
  → .s → system as/ld
```

Two stages are Cephyr-owned rather than CCWeave-owned, and the spec must be explicit about why:

- **The preprocessor.** Tree-sitter's C grammar parses unpreprocessed syntax structurally but does not implement macro expansion, `#include` resolution, or conditional compilation. Cephyr therefore ships `third_party/ucpp`, a C11 implementation of translation phases 1–6, producing a token stream plus a **line map** that survives the whole pipeline so diagnostics always report original file/line/column. Also, if the environment variable `$CEPHYR_CPP` is defined, Cephyr will use that as the preprocessor. Therefore, users can run it e.g. `CEPHYR_CPP="gcc -E" cephyr foo.c`. The Swaff frontend consumes preprocessed output, never raw source.
- **Semantic analysis.** C type checking, integer promotions, and usual arithmetic conversions are language-specific and happen before any IR exists, so they cannot be Kernels (Kernels operate on Weave IR handles via Glue). Sema lives in Cephyr as C11 host code over a typed AST. Some of these facilities, however, are relegated to `rewrite-salvo`. The compiler can define C-specific rewrite rules under `rewrite-salvo/cephyr` that are used specifically for Cephyr, and they can be used for future compilers as well, e.g. for a C++ or Pascal compiler, when we get to it.

The preprocessor and sema run in Cephyr host code *before* the Sched plan executes; the plan's first node consumes core Weave IR emitted by lowering (§6).

## 3. Conformance phases

| Phase | Adds | Explicitly excluded until later |
|---|---|---|
| v0.1 | Full expression/statement/declaration core; structs, unions, enums; pointers, arrays, function pointers; `_Generic`; `static`/`inline`; designated initializers | Bit-fields, VLAs, `_Complex`, `_Atomic`, `<threads.h>`, `_Alignas` beyond natural alignment |
| v0.2 | Bit-fields, `_Alignas`, `_Static_assert` edge cases, full initializer semantics | VLAs, `_Complex`, atomics |
| v0.3 | VLAs, `_Atomic` (SC only), `<threads.h>` via pthreads shim | `_Complex` |

Every excluded feature MUST produce a clear "not supported in this phase" error, never a mis-compile.

## 4. Frontend

For the frontend, we use Swaff's C adapter.

## 5. Sema and the typed AST

1. Sema output is a typed AST in which **every implicit conversion is materialized as an explicit cast node**: integer promotions, usual arithmetic conversions, array-to-pointer and function-to-pointer decay, null pointer constant conversion. Lowering may therefore assume all operands of an operation have identical, explicit types.
2. Constant expressions (array bounds, `case` labels, static initializers, enum values) are evaluated in sema by a dedicated interpreter. This evaluator is the *only* constant folder allowed to use C source-level semantics; all later folding happens in `rewrite-salvo`/kernels over IR semantics.
3. Sema assigns every declaration a mangling-free linker name (C has no mangling) and records linkage, storage duration, and alignment as attributes that survive into Tilly's link-section directives.

## 6. Lowering and IR semantics

This section contains the decisions that determine whether the existing `rewrite-salvo` rules are sound for Cephyr.

1. **Signed integer arithmetic lowers to two's-complement wrapping IR operations.** Weave IR's `add`/`sub`/`mul` are defined as wrapping; therefore every `rewrite-salvo` equivalence holds literally, and Cephyr at v0.1 does **not** exploit signed-overflow UB. UB-exploiting optimization is deferred to a future `nowrap` operand annotation plus side-conditioned rules (the `arith.overflow-guarded` ruleset is already shaped for this). This is deliberately the conservative choice: correctness of the equivalence-only regime (D-0007) is preserved without any per-rule C-semantics reasoning.
2. Other UB (out-of-bounds access, invalid pointer arithmetic, strict aliasing) is lowered *as written*; Cephyr claims no latitude from it in v0.1. `mem.load-store` no-alias side conditions are discharged only by the `analysis.alias` kernel, never assumed from C type-based aliasing rules.
3. Lowering path is fixed: typed AST → Kliche **imperative** stereotype → Weave IR core. The functional and OOP stereotypes are unused; a Cephyr build MUST NOT link them in.
4. `struct`/`union` values lower to memory operations (no first-class aggregates — consistent with the scalar-only ABI discipline of D-0004); scalarization is `lower.mem2reg`'s job afterward.
5. Tilly-profile constructs (relocations, link sections, layout directives) are attached only after all core-IR optimization completes. In the Sched plan this boundary is a named **barrier** (§8.2), so it is a checkable structural property of the plan rather than an implicit convention.

## 7. Backend and emission

1. Instruction selection via `isel.tree-matching` + `isel.legalization`, instruction scheduling via the `sched.list` kernel, register allocation via `regalloc.linear-scan` at `-O0`/`-O1` and `regalloc.graph-coloring` at `-O2`.
2. Calling convention: System V AMD64 ABI, implemented in the legalization kernel's target configuration, not in Cephyr host code.
3. It supports all the `codegen-*` kernels.
4. Assembling/linking delegates to the system driver (`cc`) found at configure time. Cephyr does not vendor an assembler or linker at v0.1.

## 8. Driver and pipelines

1. The CLI is `cephyr`, with conventional flags: `-c`, `-S`, `-E`, `-o`, `-I`, `-D`, `-U`, `-O0|-O1|-O2`, `-std=c17`, `-W...`. `-E` stops after `cephyr-cpp`; `-S` stops after codegen.

2. **Optimization levels are defined as Sched scripts**, not YAML: `compilers/cephyr/sched/O0.lua`, `O1.lua`, `O2.lua`. Each script constructs a plan for a single `-O` level using the Sched API (`sched.new`, `S:require`, `S:probe`, `S:rewrite`, `S:edge`, `S:barrier`, `S:seal`) and returns the sealed plan. Oeuph runs are added via `S:rewrite` against `rewrite-salvo` rulesets, and each ruleset batch carries an explicit rewrite budget per D-0008. Like the former YAML pipelines, these scripts are **hand-authored, not generated** — they are configuration, not derived facts (D-0014, revised in §11) — and are therefore exempt from the D-0003 manifest regime and from `ccw-manifest --check`. They are still subject to `ccw-sched --check` and `ccw-sched --hash`.

3. **Capability-based selection.** Cephyr scripts SHOULD select passes by capability, not by kernel name, using the Optional-returning `S:probe` for anything that may be absent and `S:require` for anything mandatory. `-O`-level differences are expressed with the sanctioned fallback idiom, e.g.:

```lua
-- O2.lua: prefer graph coloring, but tolerate a build without it.
local ra = S:probe   { capability = "codegen.regalloc-graph" }
or S:require { capability = "codegen.regalloc-linear" }
```

```lua
-- O0.lua / O1.lua: linear scan is mandatory.
local ra = S:require { capability = "codegen.regalloc-linear" }
```

4. **The core → Tilly boundary is a barrier.** Every Cephyr script MUST place a `S:barrier "pre-tilly"` such that all `norm.*`, `ssa.*`, and `opt.*` nodes precede it and all `isel.*`, `sched.list`, `regalloc.*`, and `codegen.*` nodes follow it. This makes §6.5 a validated property of the sealed plan.

5. **Plugin contributions.** A plugin that adds pipeline stages exposes a Sched script fragment (a function taking the live `S` and the resolved barriers). The driver applies enabled plugin fragments in a deterministic, manifest-ordered sequence before `S:seal()`, so the resulting plan hash is reproducible for a fixed plugin set. A fragment MAY only add nodes and edges; it MUST NOT reach into IR.

6. **Resolution and validation.** All `require`/`probe`/`rewrite` resolution goes through `manifests/Kernel.yaml` and `manifests/Stdrewrite.yaml` exactly as specified for Sched (no filesystem scanning). The driver refuses to start if any required capability is unlisted, if `S:seal()` fails validation, or if any resolved kernel's `glue_abi` differs from the executor's. Sealed plans are serialized under `compilers/cephyr/sched/plans/` (build artifacts, git-ignored); release builds SHOULD pin their plan hashes in CI via `ccw-sched --hash`.

## 9. Diagnostics

1. Every diagnostic carries an original source location via the line map, a stable ID (`CE####`), and severity. Errors never abort the process (consistent with the non-aborting library rule in `AGENTS.md`); the driver collects them and exits nonzero.
2. Kernel errors (`CCW_ERR_KERNEL` etc.) are internal-compiler-error class: reported with the kernel name, capability, and IR function under transformation, and always a Cephyr bug, never attributed to user code.
3. Sched-level failures — an unresolvable required capability, a cyclic plan, or seal-time validation errors — are reported before execution with the offending script path and the capability/kernel at fault. Because plans are re-validated and never re-resolved (D-0017), manifest drift that invalidates a committed plan hash is reported as a distinct, actionable error rather than a silent re-selection.

## 10. Layout and testing

```
compilers/cephyr/
  cpp/          # cephyr-cpp, phases 1–6
  frontend/     # Swaff adapter (Tree-sitter confined here)
  sema/         # typed AST, type checker, constant evaluator
  lower/        # typed AST → Kliche → Weave IR core
  sched/        # O0.lua, O1.lua, O2.lua (hand-authored Sched scripts)
    plans/      # serialized sealed plans (generated; git-ignored)
  stdmodule/    # standard plugin bundle (attributes, LTO, ...)
  driver/       # CLI, toolchain discovery, plugin/fragment application
  tests/
```
Testing requirements, all CI-gated and ASan-clean:

1. **Preprocessor tests**: token-stream goldens including line-map assertions.
2. **Sema tests**: type-check accept/reject cases per conformance phase.
3. **IR goldens**: textual Weave IR snapshots after lowering, exercising round-trip per D-0006.
4. **Plan tests**: each of `O0.lua`/`O1.lua`/`O2.lua` seals successfully, satisfies the `pre-tilly` barrier ordering (§8.4), and produces a stable `ccw-sched --hash` against the pinned manifests.
5. **Execution tests**: compile-and-run programs with expected output/exit codes, at every `-O` level.
6. **Differential tests**: same programs compiled with a pinned reference compiler; outputs must match. Fuzzing (Csmith-style) is v0.2.

## 11. Decisions to append to `DECISIONS.md`

- **D-0011**: Cephyr uses the Tilly profile exclusively; On1x constructs are forbidden in its pipelines.
- **D-0012**: Signed integer arithmetic lowers to wrapping IR ops; no UB-exploiting rewrites at v0.1 (`nowrap` annotations deferred).
- **D-0013**: The preprocessor is Cephyr-owned C11 code; Swaff frontends consume preprocessed token streams, never raw C source.
- **D-0014** *(revised)*: Cephyr optimization levels are **hand-authored Sched scripts** (`sched/O{0,1,2}.lua`) that build sealed plans. They are configuration, not derived facts, and are exempt from the D-0003 generated-manifest regime; they remain subject to `ccw-sched --check`/`--hash`. *(Supersedes the original YAML-pipeline formulation.)*
- **D-0015**: Type-based (strict-aliasing) alias assumptions are not used; `mem.load-store` guards are discharged only by `analysis.alias`.
- **D-0019** *(new)*: Cephyr never invokes kernels or rulesets outside a sealed Sched plan, and the core→Tilly transition is expressed as the mandatory `pre-tilly` barrier so §6.5 is structurally checkable. Plugins extend the pipeline only via Sched script fragments that add nodes/edges and never touch IR.


