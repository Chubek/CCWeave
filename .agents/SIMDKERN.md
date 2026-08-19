# `SIMDKERN.md` — SIMDe-backed vectorization tier

## Scope and library boundary

- Library: SIMDe under `third_party/simd-everywhere`, header-only, used **only** through `GlueSTD.h` wrappers. Kernels (Scheme, run by the S7 executor) never see intrinsics; they emit and consume canonical vector facts and lowered vector ops. C code in the glue layer is the only place SIMDe headers are included.
- SIMDe's role is portability, not policy: one portable vector-op surface that compiles natively on x86-64 (SSE/AVX), AArch64 (NEON), and falls back to scalar emulation on WASM32 where a mapping is missing. Cost decisions stay in the kernels; SIMDe never silently changes semantics.

## Capabilities (new, plus one existing)

| Capability | Kind | Kernel |
|---|---|---|
| `analysis.vector-width` | analysis | `kernels/vec-width.scm` |
| `analysis.vectorizable` | analysis | `kernels/vec-legality.scm` |
| `opt.loop-vectorize` | opt | `kernels/loop-vectorize.scm` |
| `opt.slp-vectorize` | opt | existing, `kernels/slp-vectorize.scm` |
| `opt.vector-reduction` | opt | `kernels/vec-reduce.scm` |
| `lower.vector-simde` | lower | `kernels/vec-lower-simde.scm` |

## Kernel entries (proposed manifest text)

Placement: the two analysis kernels join the loop tier immediately after `loop-unroll` (after line 398 in `Kernel-(2).yaml`); the opt/lower kernels sit adjacent to `slp-vectorize` at line 574. All v0.1.0.

1. **`vec-width`** — publishes, per target, the canonical vector register width and per-element-type lane counts as facts (e.g. `(vec-width f32 4)`), sourced from `.agents/CPU-ISA.jsonl` so it stays consistent with CCWas ISA validation. Deterministic and target-keyed; no probing at compile time.
2. **`vec-legality`** — consumes `analysis.loops` facts from `loop-detect` and publishes per-loop vectorization-legality facts: unit-stride affine accesses, absence of loop-carried dependences within the candidate width, no non-speculatable calls, reduction-pattern identification. Conservative: any unproven property yields a negative fact with a reason code. If the ISL tier lands, `analysis.dependence` supersedes the built-in dependence check; until then legality is restricted to self-evident single-block loops, matching the conservatism of the existing loop tier.
3. **`loop-vectorize`** — consumes legality facts plus `vec-width`; publishes a vectorization plan per accepted loop: vector factor (VF), scalar epilogue trip handling, and reduction slots. It rewrites the loop body to width-explicit vector ops in the IR (`vadd.f32x4`-style typed ops) and marks provenance `(vectorized loop-id VF)`. Interacts with `loop-unroll`: a loop selected for vectorization is excluded from the factor-two unroll candidates to avoid double transformation.
4. **`vec-reduce`** — recognizes add/mul/min/max/and/or reduction chains inside vectorized loops and rewrites them to a vector accumulator plus a single ordered horizontal-reduce op at the exit. Floating-point reassociation is opt-in via a fact flag; default preserves strict IEEE order (scalar tail-fold), keeping byte-identical determinism.
5. **`vec-lower-simde`** — the only kernel with codegen contact: maps each typed vector IR op to a `ccw_simde_*` glue call or, on targets where isel handles the op natively (`isel-direct`/`isel-legalize` paths), to the native ISA form. Ops with no legal mapping are scalarized deterministically, lane order ascending. This kernel also serves `slp-vectorize` output, giving SLP packs and loop vectorization one shared lowering.

## `GlueSTD.h` surface

- Opaque value-type handles: `ccw_v128`, `ccw_v256` (only widths the target's `vec-width` facts admit are constructible).
- Functions `ccw_simde_<op>_<type>x<lanes>` (e.g. `ccw_simde_add_f32x4`), plus `ccw_simde_load/store/loadu/storeu`, `ccw_simde_shuffle` (compile-time-constant index vector only), `ccw_simde_select` (mask), `ccw_simde_hreduce_<op>`.
- No gather/scatter in v0.1.0: SIMDe emulation cost is unpredictable across targets; non-unit-stride accesses simply fail legality. Reserved error `CCW_E_VEC_ILLEGAL` for a plan that reaches lowering without a mapping (compiler bug, hard error).
- Pin: SIMDe version, enabled `SIMDE_*` feature macros, and native-vs-emulated status per op family recorded in `.note.ccw`, mirroring the ISL pinning rule, so byte-identical-output CI (double-run diff) covers vector code.

## `Stdrewrite.yaml` consumers

Three rules: apply `opt.loop-vectorize` plans when `analysis.vectorizable` is positive; gate `opt.loop-fusion` so fused loops re-run legality before vectorizing; direct `opt.slp-vectorize` packs into `lower.vector-simde`.

## `stdlib-salvo/libc` Intrinsics

Implement standard intrinsic files for Cephyr usage using kernels. If kernels are needed, add them.

## Decisions

- **D-0046** — All SIMD codegen flows through the SIMDe glue layer or native isel; kernels never emit intrinsics directly.
- **D-0047** — Vector IR ops are width- and type-explicit; no target-dependent "natural width" abstraction leaks into the IR (consistent with CCWas width-explicit directives, D-0033).
- **D-0048** — FP reductions default to order-preserving; reassociation requires an explicit fact flag and is recorded in provenance.
- **D-0049** — Gather/scatter and masked memory ops deferred until a target-cost model exists; legality rejects them.
- **D-0050** — `Kernel-(2).yaml` is the authoritative revision for the vector tier; `slp-vectorize` is not back-ported to `Kernel.yaml`.

