# PARTHIA.md — Specification for `interpreters/sml-parthia`

Status: PROPOSAL
Component: `interpreters/sml-parthia/` — library name `sml/parthia`, a
Standard ML ('97) compiler on CCWeave
Decisions: D-0046–D-0053
Grounding: `Kernel.yaml` (lines 32–38, 45–51, 60–73, 120–126, 300–306,
348–354, 526–549, 605–611), `Capabilities.yaml` (lines 71–88, 134–135,
168–169, 182–185), `Stdrewrite.yaml` (218-line revision, no
functional-keyed rules)

---

## 1. Scope and manifest ground truth

Parthia compiles SML '97 ahead-of-time and just-in-time to native code through the CCWeave
pipeline (`ccwas`/`ccwld` terminal). Constraints established by manifest
inspection:

1. **Kliche's functional stereotype is not a manifest entity** — like `oop`
   before it (D-0040), it exists only as a kernel family. Parthia formalizes
   it as the second `stereotypes:` bundle (§3).
2. **No `frontend.*` capability exists yet for SML**, and no Swaff adapter
   for it; both are new work (§2, §6). `frontend.sml` becomes the second
   entry in the tier opened by `frontend.delphi`.
3. **`Stdrewrite.yaml` has no rules keyed on closure/tail-call/pattern-match
   facts**; consumers ship with the compiler or the opt kernels are dead
   weight (§5).
4. **`lambda-lift` verifies rather than transforms** (`Kernel.yaml`
   348–354: "Verifies and records canonical top-level function placement").
   The pipeline must therefore already be fully lifted when it runs;
   `closure-convert` (32–38) carries that obligation.

## 2. Frontend: `swaff/adapters/ccw_swaff_sml.c` + elaborator

- **D-0046**: The Swaff adapter is parse-only, per the Moonix and Dephia
  precedent: a single C11 translation unit producing the surface AST for the
  full SML '97 grammar (including `infix` declarations resolved during the
  parse with a deterministic fixity environment). Everything semantic —
  Hindley–Milner inference, signature matching, overloading resolution,
  equality-type admissibility — lives in Parthia's elaborator, not the
  adapter, so the adapter stays reusable for other ML-family consumers.
- The elaborator **defunctorizes**: functor applications are expanded
  statically at elaboration time and signatures are erased, so the module
  system never reaches the kernel tier and needs no manifest support.
  Generated instance names are derived from a sorted application path, not a
  gensym counter, to keep output byte-stable.
- Elaboration output is a typed core-ML IR; type information is retained as
  facts (needed by the representation decisions in §4 and by the inliner's
  cost model) and emitted in sorted order.

## 3. Kliche `functional` stereotype — formalization

- **D-0047**: Second instance of the `stereotypes:` schema introduced by
  D-0040:
```yaml
stereotypes:
  functional:
version: 0.1.0
kernels:
- pattern-match-lower        # lower.pattern-match        (Kernel-(2).yaml 526–532)
- functional-pipeline-lower  # lower.functional-pipeline  (67–73)
- inline                     # opt.inline                 (300–306)
- closure-convert            # lower.closure-conversion   (32–38)
- lambda-lift                # lower.lambda-lifting       (348–354, verify-only)
- tail-call-opt              # opt.tail-call              (605–611)
order:
- [pattern-match-lower, inline]      # match trees expose call sites first
- [inline, closure-convert]          # inline before environments are frozen
- [closure-convert, lambda-lift]     # lift, then verify placement
- [lambda-lift, tail-call-opt]       # jumps rewritten on canonical top-levels

- **D-0048**: Parthia compiles **direct-style**. `cps-convert`
  (`lower.cps-conversion`, 45–51) is deliberately excluded from the bundle:
  SML '97 has no first-class continuations (`callcc` is an SML/NJ
  extension, out of scope), and `opt.tail-call` already covers the
  recursion-as-loop requirement. The kernel remains individually
  requestable for a future dialect gate; the stereotype does not hide it.
```
## 4. Lowering SML semantics onto the stereotype

| SML construct | Capability | Notes |
|---|---|---|
| `case`/`fn` match rules, exhaustiveness | `lower.pattern-match` | Elaborator emits redundancy/exhaustiveness diagnostics before lowering |
| `andalso`, `orelse`, `o`-composition chains | `lower.functional-pipeline` | Short-circuit lowering per the kernel's contract |
| Nested `fun`/`fn`, free variables | `lower.closure-conversion` | Produces fully lifted top-levels; `lambda-lift` gates the result |
| Self/mutual tail recursion | `opt.tail-call` | Required for correctness-by-convention (SML idioms assume it), not just optimization |
| `raise`/`handle` | `lower.exceptions` | Reuses `exception-lower` (`Kernel-(2).yaml` 60–66); generative `exception` declarations get deterministic tags from sorted declaration paths |
| Allocation, mutable `ref`/`array` stores | `vm.gc-barrier-insertion`, `vm.safepoint-insertion` | See D-0050 |

- **D-0049**: Uniform representation — every value is a tagged machine word
  (immediate small ints, boxed everything else), decided in the elaborator's
  lowering to IR. No monomorphization and no new representation kernel;
  polymorphic equality compiles to a runtime structural-walk routine, with
  the elaborator substituting direct primitive comparisons where the
  equality type is statically monomorphic.
- **D-0050**: Parthia is the second consumer of the On1x GC capabilities
  after Moonix, but AOT-only: it requests `vm.gc-barrier-insertion`
  (120–126) and `vm.safepoint-insertion` (543–549) and **not** the JIT-tier
  capabilities (`vm.inline-cache`, `vm.deopt-*`) — there are no tiers and no
  deoptimization. The runtime ships a precise GC whose stack maps are
  emitted as sorted facts alongside the safepoint polls.

## 5. `Stdrewrite.yaml` consumers

- **D-0051**: Ship with the compiler, in the same change: a tail-call-apply
  rule keyed on `opt.tail-call` facts (call → jump materialization), a
  match-chain-fold rule keyed on `lower.pattern-match` facts (merges
  adjacent decision-tree arms with identical targets), and a
  closure-env-trim rule keyed on `lower.closure-conversion` facts (drops
  environment slots proven dead after inlining). Rule keys use the sorted
  fact forms; the CI double-run diff gate extends over their output.

## 6. Required manifest additions

- `frontend.sml` (`Capabilities.yaml`) — provided by the Swaff adapter +
  elaborator; second entry of the `frontend.*` tier.
- `stereotypes.functional` bundle in the kernel manifest (§3).
- No new kernels. Codegen consumes the existing tier unchanged:
  `codegen.isel-*`, `codegen.regalloc-ssa` (default), `codegen.x86_64` /
  `codegen.aarch64`, then `ccwas` assembly and `ccwld` link plans per
  D-0025–D-0038.

## 7. Determinism

- **D-0052**: Standard gates: byte-for-byte reproducibility CI over the full
  compile, sorted emission of all facts (type facts, exception tags, functor
  instance names, closure layouts, stack maps), no host-dependent iteration
  in the adapter or elaborator, fixed inference-variable numbering derived
  from source order.
- **D-0053**: `.note.ccw` records adapter version, elaborator version,
  `functional` stereotype version, GC configuration, and manifest hashes,
  alongside the existing toolchain provenance.

## 8. Rollout order

1. `ccw_swaff_sml.c` adapter + `frontend.sml`; parse-and-print CI gate.
2. Elaborator (inference, defunctorization) emitting typed core-ML facts.
3. `functional` stereotype bundle (D-0047) and pipeline bring-up through
   `tail-call-opt`; lambda-lift verification wired as a hard gate.
4. `Stdrewrite.yaml` consumers (D-0051) with CI diff gates.
5. Runtime: GC barriers/safepoints (D-0050), exceptions, polymorphic
   equality; full-suite reproducibility gate.
`

