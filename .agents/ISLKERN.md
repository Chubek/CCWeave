# ISLKERN.md — Integrating the Integer Set Library (ISL) into CCWeave

Status: PROPOSAL
Scope: Kernel/Capability taxonomy, `Rewrite-salvo.yaml`, `GlueSTD.h`, S7 Executor, IR
Prereq reading: `Kernel.yaml` (loop kernels, lines 357–398), `Capabilities.yaml`
(lines 21–22, 140–145), `Rewrite-salvo.yaml` (no loop rules as of 218-line revision)

---

## 1. Why ISL, and what it affords us

The current loop tier is purely structural and CFG-level:

- `loop-detect` (`analysis.loops`) — back-edge facts only
- `licm` (`opt.licm`) — constant-only invariance
- `loop-fusion` (`opt.loop-fusion`) — adjacent self-loop pattern match
- `loop-unroll` (`opt.loop-unroll`) — conservative factor-two on self-loops

None of these can answer the questions that unlock real loop optimization:
*which iterations touch which memory*, *which iterations depend on which*, and
*which reorderings are legal*. ISL affords exactly that:

1. **Affine iteration domains and access relations.** Exact integer-set
   representation of loop bounds and array subscripts, including piecewise and
   parametric cases (`isl_set`, `isl_map`, `isl_union_map`).
2. **Exact dependence analysis.** Flow/anti/output dependence polyhedra via
   `isl_union_access_info` / dataflow analysis — a legality oracle that replaces
   the "adjacent self-loop" and "factor-two" heuristics with proofs.
3. **Schedule trees and rescheduling.** The Pluto-style scheduler
   (`isl_schedule_constraints_compute_schedule`) gives tiling, interchange,
   skewing, and fusion/distribution decisions as a single schedule object.
4. **Code generation.** `isl_ast_build` turns a schedule back into a loop AST,
   which we lower to IR — enabling transformations far beyond what
   pattern-rewrites in `Rewrite-salvo.yaml` can express.
5. **Exact counting** (`isl_pw_qpolynomial` / barvinok-style cardinality) for
   cost models: trip counts, footprint sizes, reuse distances.

Downstream payoff: legal cache tiling, general (non-adjacent) fusion,
vectorization legality facts, parametric unroll factors, and dependence-aware
scheduling for the Moonix T1/T2 JIT tiers.

## 2. Determinism constraints (non-negotiable)

ISL is deterministic for fixed inputs and fixed options. To keep the
byte-for-byte reproducibility gates:

- Pin the ISL version and build flags; record both in `.note.ccw` provenance
  alongside the manifest hashes.
- Fix all `isl_options_*` (scheduler algorithm, fuse strategy, max
  coefficients/constants) in a checked-in options manifest; forbid environment
  overrides in the sandbox.
- Sort all fact emission (as elsewhere) and run the CI double-run diff over the
  new kernels' outputs.

## 3. Taxonomy additions

### 3.1 New capabilities (`Capabilities.yaml`)

- `analysis.affine` — extraction of affine iteration domains and access
  relations from IR loops.
- `analysis.dependence` — dependence polyhedra computed from `analysis.affine`
  facts.
- `analysis.tripcount` — exact/parametric trip-count and footprint facts.
- `opt.schedule` — schedule-tree computation (fusion, interchange, skewing).
- `opt.tiling` — tiling candidates derived from schedule bands.

The existing `opt.loop-fusion` / `opt.loop-unroll` remain as the cheap
structural tier; the polyhedral tier coexists and is preferred when
`analysis.affine` facts are available for a region.

### 3.2 New kernels (`Kernel-(2).yaml` successor)

- `affine-extract` → publishes domain/access facts (capability
  `analysis.affine`). Fails soft: regions that are not affine publish an
  explicit `nonaffine` fact so downstream kernels skip them deterministically.
- `dep-poly` → consumes `affine-extract` facts, publishes dependence relations
  (`analysis.dependence`).
- `isl-schedule` → consumes dependence facts, publishes schedule trees and
  band metadata (`opt.schedule`).
- `tile-plan` → consumes schedule bands, publishes tile-size candidates
  (`opt.tiling`), sizes from a pinned cost table (no runtime probing).

### 3.3 `Rewrite-salvo.yaml` consumers

Add rules keyed on the new facts — schedule-apply (materialize an
`isl-schedule` result through AST regeneration), tile-apply, and a
dependence-gated upgrade of the structural fusion/unroll rules (they may fire
only when `analysis.dependence` proves legality or is absent for the region).
Without these consumers the kernels are dead weight; ship them together.

## 4. Directives to the implementing model

### 4.1 Update `GlueSTD.h`

- Add an `ccw_isl` binding section: opaque handle typedefs for
  `ccw_isl_ctx`, `ccw_isl_uset`, `ccw_isl_umap`, `ccw_isl_schedule`, plus
  create/free and serialize/parse entry points. All objects must round-trip
  through the canonical ISL string form so facts stay textual and diffable.
- Expose a single `ccw_isl_ctx_new_pinned(void)` constructor that applies the
  checked-in options manifest; do **not** expose raw option setters.
- Add error taxonomy entries (`CCW_E_ISL_NONAFFINE`, `CCW_E_ISL_QUOTA`) and a
  deterministic computation quota (max operations) so scheduler blow-ups fail
  identically on every host.

### 4.2 Update the S7 Executor

- Register the four new kernels in the executor's kernel table with their
  capability requirements; scheduling stays Sched-orchestrated like the
  GC/deopt invariant plans.
- Facts produced by ISL kernels must be published in canonical sorted order;
  route them through the existing fact store with the ISL string form as the
  payload.
- Enforce the sandbox: ISL runs with the pinned context only, no allocator or
  option leakage; the computation quota aborts to a `quota` fact, never a
  nondeterministic partial result.
- Extend the CI double-assembly/diff gate to cover ISL fact output and any IR
  regenerated from schedules.

### 4.3 Update the IR

- Add region metadata slots for: iteration-domain reference, access-relation
  references, and a `schedule-applied` provenance tag (schedule hash + ISL
  version), so re-runs can verify rather than recompute.
- Add a structured loop-region form (or annotate the existing CFG loop facts)
  sufficient for `affine-extract` to recover bounds and steps without
  heuristic pattern matching; non-recoverable regions get the `nonaffine`
  marker.
- Ensure AST-regenerated loops carry back-mapping to original statement IDs so
  Moonix deopt metadata and GC safepoint insertion (`vm.deopt-points`,
  `vm.safepoint-insertion`) survive rescheduling.

## 5. Rollout order

1. `GlueSTD.h` bindings + pinned context + options manifest.
2. `affine-extract` and `dep-poly` kernels with new capabilities; CI diff gate.
3. `isl-schedule` / `tile-plan` kernels.
4. `Rewrite-salvo.yaml` consumers and IR schedule-apply path.
5. Gate the structural fusion/unroll rules behind dependence facts.

Adopt ISL only as this complete slice; as established previously, the current
manifests contain no consumer for it, so partial adoption adds a heavyweight
dependency that serves nothing.
`

