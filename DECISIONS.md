# Decisions

## 2026-08-17 — Cephyr toolchain forwarding uses ordered option lists

`-Wp,`, `-Wa,`, and `-Wl,` split on commas and preserve order; `-X*` adds
one unmodified argument after the corresponding list. Profiles express the
same channels as string arrays, alongside `library_paths`, `libraries`, and
the `pic`/`pie`/`shared` booleans. `-E`, `-s`/`-S`, and `-o` are CLI-only
stage controls; `-o` retains its output-file meaning and requests the
pre-link stop stage. The current Cephyr backend emits IR rather than invoking
an assembler/linker, so those two option channels are retained on the driver
configuration until backend emission is enabled.

## 2026-08-17 — Cephyr profiles are declarative YAML/TOML overlays

Cephyr discovers `CEPHYR.yaml` before `CEPHYR.toml` in the working directory;
`--profile` selects an explicit file. A profile may select one Sched Lua
script or declare ordered kernel and Stdrewrite selections, but not both.
Profile-relative manifest and script paths resolve beside the profile, while
command entries are intentionally executed by the host shell through
`cephyr run`. `profile init` is non-destructive and defaults to YAML.

## 2026-08-17 — Sched executes sealed Stdrewrite batches through Oeuph

`S:rewrite` remains a selection-only Lua operation.  The host consumes a
sealed plan with `ccw_plan_apply_rewrites`, resolves each selected ruleset
through `Stdrewrite.yaml`, and invokes Oeuph in deterministic DAG/node and
manifest order.  Kernel and barrier nodes are treated as already handled by
the host and are skipped by this rewrite-only executor.  The caller supplies
one Oeuph budget and cost model for every selected ruleset and receives one
`ccw_oeuph_stats` record per invocation.

## 2026-08-17 — Sched lookup indexes use klib khash

Sched uses the pinned `third_party/klib/khash.h` implementation for manifest
name/capability/ruleset indexes and duplicate-edge detection.  The plan
artifact hash remains SHA-256 because `khash` is a hash-table implementation,
not a cryptographic digest, and the scheduler specification requires a
portable 256-bit plan hash.

## 2026-08-17 — Sched v0.1 open items

D-0016 is accepted: Lua 5.5 is Sched's sole scripting surface. D-0017 is
accepted: plan artifacts record their selected kernel and ruleset names and
are re-validated against current manifests; they are never silently
re-resolved. D-0018 is accepted: scripts own analysis-to-consumer dependency
edges in v0.1. The planner intentionally has no IR access or inferred
semantic dependencies.

## 2026-08-17 — Initial OCaml frontend subset

The OCaml Swaff adapter lowers the source forms that have an unambiguous
mapping to the current Kliche and Weave IR APIs: top-level functions, scalar
integer and boolean expressions, direct and higher-order application,
immutable local bindings, sequencing, and value-producing conditionals.
Global value storage, algebraic data, pattern matching, nested named
functions, objects, modules, and exceptions remain explicitly unsupported
because the current lower layers do not define their representation. The
adapter reports those CST nodes instead of inventing an encoding.

## 2026-08-16 — Capability kernels preserve IR without required extensions

The Core Accessor Set exposes instruction navigation and structural edits, but
does not define portable accessors for control-flow edges, analysis-fact
storage, target machine nodes, ABI metadata, or debug emission.  The latter
30 manifested kernels therefore validate their capability and options alist,
then return the unchanged IR handle.  Their advertised contracts become
behavioral when the corresponding profile-specific host accessors are
standardized.

## 2026-08-16 — Draft functional-kernel extension contract

The user authorized a draft of the missing host accessor contracts. The
proposal in `docs/GLUE_EXTENSIONS_DRAFT.md` preserves the Glue ABI v1 scalar
boundary, keeps analysis facts host-owned, and defines extension groups for
control flow, SSA, memory, target, VM, sanitizer, and debug functionality.
It is explicitly non-normative pending review and promotion into the Glue
specification.

## 2026-08-16 — Phase 1 functional kernel semantics

The approved extension contract is implemented first for scalar IR:
node/operand inspection, destination and operand mutation, and host-owned
analysis facts. `analysis.purity`, `analysis.def-use`, and `analysis.range`
now publish real facts; `opt.copy-propagation`,
`opt.dead-code-elimination`, and `normalize.instructions` perform verified
scalar rewrites. CFG, target-machine, VM, sanitizer, and debug kernels
remain staged until their corresponding IR semantics are implemented.

## 2026-08-16 — Phase 2 CFG substrate

Phase 2 promotes derived successor and predecessor navigation. A block's
outgoing edges are its final instruction's block-target operands, resolved
within the containing function. This is sufficient for dominator,
reachability, and loop analyses; CFG mutation remains deferred until its
rewrite invariants are specified and tested.

## 2026-08-16 — Phase 3 unreachable-block deletion

Phase 3 permits only predecessor-free block deletion. The unreachable-block
kernel computes reachability from function entry and repeatedly removes an
unreachable block once it has no predecessors. Edge rewrites and block
splitting remain deferred, so the host can preserve CFG integrity without
inventing branch-rewrite semantics.

## 2026-08-16 — Five conservative staged-kernel semantics

Five metadata-only kernels now use only promoted Phase 1–3 accessors.
`normalize.cfg` repeatedly merges linear predecessor/successor pairs;
`opt.phi-simplify` replaces a phi only when every register or integer input
is identical; and `opt.null-check-elim` removes repeated checks of the same
named SSA value within one block. `opt.tailcall` publishes eligibility only
for a call whose result is immediately returned, while `vm.deopt-points`
publishes state requirements only for explicit `guard` and `deopt`
instructions in an On1x module. These intentionally conservative rules avoid
assuming unstandardized call, null, target, or VM semantics.

## 2026-08-16 — Ten additional functional kernel subsets

Ten staged kernels now implement the largest semantics-preserving subset
available through the promoted scalar accessors. ANF normalization names
unnamed scalar computations; instruction legalization expands integer negate
and complement; pattern lowering handles scalar equality; peephole
optimization removes integer identities; local register coalescing propagates
copies until redefinition; and SSA destruction lowers single-input phis.
Tail-call and On1x deopt kernels publish conservative host-owned facts.
On1x GC barriers are inserted idempotently before stores, and UBSan divisor
checks are inserted before integer division or remainder unless the divisor is
a known nonzero constant. Full target selection, multi-predecessor SSA
destruction, ABI-aware tail calls, complete deopt state maps, and sanitizer
runtime binding remain deferred until their draft extension groups are
promoted.

## 2026-08-16 — Eleven additional staged-kernel implementations

Eleven more placeholders now provide deterministic behavior without relying
on unpromoted physical-target APIs. Atomic load/store nodes lower to
sequentially consistent libcall operations; constant 64-bit bitfield extracts
become shift-and-mask sequences; generic exception and varargs operations
become explicit runtime operations; and checked integer arithmetic becomes
an explicit `with-overflow` form. Instruction selection publishes
target-neutral pattern classes. Register allocation publishes conservative
unbounded virtual colors or linear slots, scheduling publishes reverse
distance priorities, and explicit byte-sized spill slots receive contiguous
offsets. SSA construction is deliberately local to one block: repeated
definitions are renamed and subsequent local uses are updated. Physical
register assignment, ABI exception layout, aligned spill packing, global SSA
phi placement, and target-specific pattern emission remain deferred.

## 2026-08-16 — Final metadata-only kernel promotion

The final 24 `0.0.0` placeholders now have executable conservative subsets.
Scalar target selectors rewrite supported arithmetic and memory opcodes to
target-prefixed machine forms; closure, complex, exception, CPS, switch, and
constant-return inline cases perform concrete lowering; local mem2reg handles
an exact stack-slot/store/load sequence; and On1x kernels assign inline-cache
slots or insert idempotent call safepoints. Capabilities that require
unpromoted movement, vector, debug-source, or physical-register APIs publish
deterministic host-owned decisions instead: sink positions, jump targets,
loop fusion/unroll candidates, top-level lambda status, synthetic line
ordinals, virtual register colors/slots, stable list schedules, and SLP pack
ids. These facts are intentionally advisory until the corresponding target
extension groups are promoted. No kernel remains metadata-only or versioned
`0.0.0`.
