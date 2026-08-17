# Moonix Specification v0.1

Moonix is a Lua implementation built as the first **On1x-profile** consumer of the CCWeave
infrastructure. It lives under `interpreters/moonix/` and MUST NOT fork or shadow any CCWeave
component: it uses Swaff for parsing (the Lua adapter at `swaff/adapters/ccw_swaff_lua.c`),
Kliche for stereotype lowering, Weave IR with the **On1x profile**, Oeuph/`stdrewrite` for
expression rewriting, Kernels via Glue, and the **Sched** subsystem to orchestrate tier plans.

Moonix is Cephyr's dual: where Cephyr is a static AOT compiler forbidden from touching On1x
constructs (D-0011), Moonix is a tiered dynamic runtime for which On1x constructs — inline
caches, deopt metadata, safepoints, GC barriers — are the whole point.

> **Naming note.** As in the Cephyr spec, *Sched* (capitalized) is the orchestrator subsystem;
> the `sched.list` kernel is the backend instruction scheduler and is unrelated.

## 1. Scope and goals

1. Moonix implements **Lua 5.4** semantics (integer/float subtypes, integer division `//`,
   bitwise operators, `goto`, to-be-closed variables), excluding features listed per phase (§3).
2. Moonix is a **tiered** implementation:
   - **T0** — a bytecode interpreter, Moonix-owned C11 host code. Always present; the semantic
     reference for the JIT tiers.
   - **T1** — baseline JIT: method-at-a-time, no speculation beyond inline caches.
   - **T2** — optimizing JIT: type-feedback-driven speculation with guards and deoptimization
     back to T0.
3. Moonix uses the On1x profile exclusively in T1/T2. Tilly-only constructs (static relocations,
   link sections, layout directives) are forbidden in Moonix plans; there is no AOT mode at v0.1.
4. JIT targets: `codegen.x86-64` first; `codegen.aarch64` at v0.2 (both capabilities are listed
   in Capabilities.yaml). Interpreter-only operation MUST work on any host, including targets
   with no codegen kernel.
5. Moonix embeds: it ships `libmoonix` with a `lua.h`-compatible C API subset (§9) plus the
   `moonix` standalone CLI.

**Note**: any runtime operation needing IR support (a new guard kind, a new IC shape) must be
defined as a kernel under `kernels/` first, with `ir` and `glue` updated accordingly — same
rule as Cephyr. Moonix host code never mutates IR directly.

## 2. Position in the CCWeave stack

```
source.lua
  → Swaff frontend        (swaff/adapters/ccw_swaff_lua.c, tree-sitter-lua)
  → scoped AST + resolve  (Moonix-owned: locals, upvalues, goto/label, attribs)
  → Moonix bytecode       (T0 executes this; also the deopt target)
       │ hot-count / type feedback
  → Kliche functional stereotype (closures) + imperative stereotype (bodies)
  → Weave IR (core)  ─┐
  → Weave IR (On1x)  ─┴─ orchestrated by sealed Sched plans (per tier):
                         Oeuph/stdrewrite, normalize.*, transform.ssa-construct,
                         opt.*, vm.inline-cache, vm.deopt-points, vm.deopt-metadata,
                         vm.safepoint-insertion, vm.gc-barrier-insertion,
                         isel.*, sched.list, regalloc.*, codegen.x86-64
  → executable code in the Moonix code cache (no assembler/linker; JIT emission)
```
Moonix-owned stages, and why:

- **Resolution.** Lua scoping (lexical locals, upvalue capture, `goto` visibility rules,
  `<close>`/`<const>` attributes) is language-specific and pre-IR, so it cannot be a kernel.
  It lives in Moonix as C11 host code over the Swaff AST.
- **The bytecode tier.** T0 is the deoptimization target: every T2 deopt point maps to a
  bytecode PC plus a value-location table (materialized via `vm.deopt-metadata`). The bytecode
  format is therefore a Moonix-frozen ABI between the runtime and the kernels' metadata, versioned
  independently of the Lua surface.
- **The object model and GC.** NaN-boxed values, table shapes/hidden layouts, string interning,
  and an incremental mark-sweep GC are runtime host code. Kernels only *interface* with them:
  `vm.gc-barrier-insertion` inserts the write barriers the GC declares, and
  `vm.safepoint-insertion` inserts the polls the GC requires.

## 3. Conformance phases

| Phase | Adds | Explicitly excluded until later |
|---|---|---|
| v0.1 | Full expression/statement core; tables, metatables, closures, varargs; integer/float arithmetic with 5.4 coercions; `pcall`/`error`; string/table/math stdlib core; T0 + T1 | Coroutines, `goto` into T1+ code (interpreter-only), `<close>`, weak tables, `__gc`, string pattern library completeness, T2 |
| v0.2 | Coroutines (T0-resident), `<close>`, weak tables + `__gc`, full pattern library, T2 speculation, aarch64 | `require`/full package library, os/io completeness |
| v0.3 | Full stdlib, package library, finalizer ordering guarantees | — |

Every excluded feature MUST produce a clear "not supported in this phase" error, never a
mis-execution. Tier exclusions degrade to T0, never error.

## 4. Frontend

Moonix uses Swaff's Lua adapter (`swaff/adapters/ccw_swaff_lua.c`) exclusively. Tree-sitter is
confined to Swaff; no Moonix component includes tree-sitter headers. Unlike Cephyr, there is no
preprocessor stage: the adapter consumes raw Lua source, and chunk names/line info come from the
Swaff tree and survive into bytecode line tables and `vm.deopt-metadata`.

## 5. Semantics and lowering

1. **Values.** Lua values lower to a single boxed scalar IR type (NaN-boxed 64-bit word). Only
   scalars cross the kernel ABI, consistent with D-0004; tables and strings are heap handles.
2. **Arithmetic.** T1 lowers `+`/`-`/`*` on numbers as boxed calls or IC-dispatched fast paths.
   T2 speculates: guarded unbox → native integer ops. Because Weave IR integer arithmetic is
   wrapping and Lua 5.4 integer arithmetic is also wrapping two's-complement, **all `arith.*`,
   `bitwise.*`, `divmod.*`, and `cmp.*` stdrewrite equivalences apply unchanged** on unboxed
   lanes. The `arith.overflow-guarded` ruleset (side-conditioned, per Stdrewrite.yaml) is used
   on T2 speculative lanes where overflow guards exist. Float ops use `float.*` rules only where
   Lua's IEEE semantics hold (no fast-math).
3. **Integer/float distinction** follows Lua 5.4 exactly: `//` and `%` on integers use floor
   semantics — lowered via `divmod.*`-compatible sequences with explicit floor adjustment;
   division `/` always produces float.
4. **Metatables.** Every dynamic dispatch site (`__index`, `__add`, calls, method calls) is an
   IC site allocated by `vm.inline-cache` (kernel `on1x-inline-cache`, Kernel.yaml:277). IC
   misses call into the runtime; IC shapes are versioned against table shape epochs.
5. **Speculation and deopt (T2 only).** Type feedback recorded by T0/T1 drives guard insertion.
   Guards and deopt points are placed by `vm.deopt-points` (kernel `on1x-guard-deopt`,
   Kernel.yaml:200); reconstruction maps by `vm.deopt-metadata`. Deopt always resumes in T0 at
   an exact bytecode PC. **Speculation may never change observable semantics** — Oeuph's
   equivalence-only regime (D-0007) is preserved because guards make speculative facts locally
   true, and `mem.load-store` no-alias side conditions are discharged only by `analysis.alias`,
   exactly as in Cephyr (D-0015 applies verbatim).
6. **GC interface.** `vm.gc-barrier-insertion` (kernel `on1x-write-barrier`, Kernel.yaml:263)
   inserts write barriers on all heap stores; `vm.safepoint-insertion` (kernel `on1x-safepoint`,
   Kernel.yaml:550) inserts polls at loop back-edges and call boundaries. Compiled code without
   these passes MUST NOT be admitted to the code cache — enforced structurally by the plan (§6.4).
7. **Lowering path** is fixed: bytecode → Kliche **functional** stereotype for closure/upvalue
   structure (`lower.closure-conversion` capability), then **imperative** stereotype for bodies →
   Weave IR core. The OOP stereotype is unused
.

## 6. JIT driver and tiered plans

1. **Hierarchy.** The driver is `interpreters/moonix/runtime/jit.c`, managing tiers:
   - **T0** — interprets bytecode using a computed goto dispatch.
   - **T1** — Sched plan `T1.lua`: core + On1x ICs + safepoints + barriers + codegen. No guards.
   - **T2** — Sched plan `T2.lua`: T1 + guards + deopt + intensive core opt + stdrewrite.

2. **Sched scripts.** Optimization plans are **hand-authored Lua scripts** in
   `interpreters/moonix/sched/T{1,2}.lua`. Like Cephyr's plans (D-0014), they are configuration
   and exempt from the generated-manifest regime, but subject to `ccw-sched --check` and hash-pinning.

3. **Capability-based selection.** Plans MUST use `S:require` for On1x profile capabilities
   (`vm.inline-cache`, `vm.gc-barrier-insertion`, `vm.safepoint-insertion`) to ensure memory and
   execution safety. Target-specific codegen is `S:require`d based on the runtime's detected host.

4. **Structural invariants.** Every Moonix plan MUST place a `S:barrier "on1x-complete"` such
   that all `vm.*` barrier/safepoint/metadata nodes precede it and the codegen backend follows
   it. This ensures that every instruction-selection run works on IR where GC and deopt
   invariants have been fully materialized.

5. **Resolution.** The runtime driver resolves plans exactly as Sched specifies, using the
   manifests `Kernel.yaml` and `Stdrewrite.yaml` (D-0017). A mismatched or broken manifest at
   runtime is a fatal initialization error. Sealed plans for T1 and T2 are serialized and
   pinned in CI via `ccw-sched --hash`.

## 7. Memory and GC

1. Moonix uses a **moving incremental collector**. All object handles in IR are therefore
   "handle-logical" until finalized at codegen time.
2. The runtime exports the `vm.gc-barrier-check` function to kernels; the barrier-insertion
   kernel calls this to determine if a store requires a barrier.
3. The Moonix allocator is thread-local; all VM states are isolated (shared-nothing at v0.1).

## 8. Layout and testing
```
interpreters/moonix/
  runtime/      # moonix.h, T0 bytecode interpreter, GC, object model
  frontend/     # swaff adapter, scope resolution, bytecode compiler
  sched/        # T1.lua, T2.lua (hand-authored Sched scripts)
    plans/      # serialized sealed plans (generated; git-ignored)
  stdlib/       # Lua-authored and C-authored standard libraries
  cli/          # moonix driver, REPL
  tests/
```

Testing requirements, all CI-gated and ASan-clean:

1. **Bytecode goldens**: verify Swaff AST → Moonix bytecode compiler.
2. **Interpreter tests**: T0 execution with expected output/exit codes.
3. **Plan tests**: `T1.lua` and `T2.lua` seal successfully and meet the `on1x-complete` barrier
   ordering.
4. **JIT execution tests**: T1 and T2 execution of the same T0 programs. Output MUST match T0.
5. **Deopt tests**: specifically exercise type-feedback loops and guard failures; verify T2 → T0
   reconstruction.
6. **Differential tests**: same programs against Lua 5.4.1 reference.

## 9. Decisions to append to `DECISIONS.md`

- **D-0020**: Moonix uses the On1x profile exclusively for JIT tiers (T1/T2); it is the
  primary consumer of `vm.*` capabilities.
- **D-0021**: The semantic reference is T0 (C11 interpreter); T1 and T2 must remain bit-for-bit
  compatible with T0 output, governed by Oeuph's equivalence-only rule (D-0007).
- **D-0022**: The Moonix bytecode format is the internal deopt ABI and is version-locked to the
  runtime/kernel versions.
- **D-0023**: Moonix JIT tiers extend the pipeline with mandatory On1x barriers and safepoints;
  a plan that omits them for a moving-GC target is invalid at seal-time.
- **D-0024**: Lowering uses the `functional` stereotype for closures (`lower.closure-conversion`)
  and the `imperative` stereotype for code bodies. OOP constructs are not used.
