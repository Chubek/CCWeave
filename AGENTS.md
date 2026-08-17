# AGENTS.md — Implementing CCWeave

You are implementing CCWeave, a modular compiler infrastructure. This file
tells you how to work in this repository. It is not the specification.

## Normative sources (read before writing any code)

1. `.agents/CCWEAVE_SPECS.md` -- CCWeave Specification v0.1. Defines all
   subsystems, their contracts, and conformance conditions (§9).
2. `.agents/GLUESTD_H.md` -- the complete, normative `glue/GlueSTD.h`
   (Glue ABI v1), including the host accessor registration API, the
   `ccw_val` boundary-value model, and the Core Accessor Set.
3. `.agents/SCHED.md` -- the complete, normative specs for the scheduler + orchestrator layer (Lua-based)
4. `.agents/CEPHYR.md` -- the complete, normative specs for `compiler/cephyr`, the C compiler derived from the infrastructure
5. `.agents/MOONIX.md` -- the complete, normative specs for `interpreters/moonix`, a JIT-backed Lua interpreter

If this file and the specs conflict, the specs win. If the two specs
conflict, `GLUESTD_H.md` wins for anything ABI-related. Do not invent
behavior not covered by either; stop and ask instead.

## Repository layout (create exactly this; spec §8)

```
ccweave/
  glue/GlueSTD.h        # copied verbatim from .agents/GLUESTD_H.md
  executors/s7/         # reference executor
  kernels/              # *.scm R7RS kernel libraries
  manifests/            # GENERATED ONLY — never hand-edit
  ir/                   # Weave IR core + tilly/ + on1x/ profiles
  kliche/               # functional/, imperative/, oop/ stereotypes
  swaff/                # grammars/ + adapters/
  oeuph/                # e-graph engine
  stdrewrite/           # rulesets
  sched/                # the scheduler + orchestrator engine
  compilers/            # the compilers derived from the CCWeave infrastructure
  interpreters/         # the interpreters weaved from CCWeave
  third_party/          # vendored deps + VERSIONS.lock
  tools/ccw-manifest/
  tests/
  CMakeLists.txt        # global build specs
```

**Note**: Each subsystem has its own `CMakeLists.txt`.

## Build order and dependencies

Implement in this order. Each stage must build and pass its tests before
the next begins; later stages depend on earlier ones.

1. **`glue/GlueSTD.h` + `ccw_val` support library.** The header is
   transcribed verbatim from `.agents/GLUESTD_H.md`. Implement the
   `ccw_val` constructors and `ccw_val_clear` in `glue/`.
2. **Weave IR core** (`ir/`): types, functions, blocks, instructions;
   the programmatic C API; MPC-based text parser and printer.
   Round-trip (parse → print → parse gives identical structures) is a
   conformance requirement — write the round-trip test harness first.
3. **Tilly and On1x profiles** (`ir/tilly/`, `ir/on1x/`): profile
   declaration in module headers, profile-specific constructs, and
   validation that rejects out-of-profile constructs (e.g.
   dynamic-dispatch metadata in a Tilly module).
4. **S7 executor** (`executors/s7/`): vendored S7, implements every
   function in `GlueSTD.h`, registers nothing itself — accessors come
   from the host at runtime.
5. **Host accessor layer**: the Core Accessor Set from `GLUESTD_H.md`
   (`glue-has?`, `ir-profile`, the `-count`/`-ref` navigation pairs,
   inspection, and the builder-based mutation channel).
6. **`tools/ccw-manifest`**: loads kernels via the executor, calls live
   `kernel-capabilities`, emits `manifests/Kernel.yaml` and
   `manifests/Capabilities.yaml` with generated-file headers;
   `--check` mode regenerates in-memory and diffs.
7. **Example kernels** (`kernels/`): start by transcribing
   `strength-reduce.scm` from the specs, then add at least one kernel
   per capability domain you need for testing.
8. **Oeuph** (`oeuph/`) and **stdrewrite**: e-graph engine over the
   canonical in-memory IR, with budgets, determinism, per-ruleset
   diagnostics.
9. **Kliche** stereotypes, then **Swaff** frontends (Tree-sitter
   grammar + lowering adapter) last.

## Hard rules (violating any of these is a broken build)

- **Manifests are generated artifacts.** Never write to `manifests/` by
  hand. Any change there comes from running `ccw-manifest`. CI runs
  `ccw-manifest --check`; a diff is a failure.
- **Kernels are engine-agnostic R7RS.** No S7-specific functions, no
  `#+s7` conditionals, no engine-detection. If a kernel needs something
  beyond `(scheme base)` and `(ccweave glue)`, the design is wrong.
- **Kernels never mutate global state and never touch IR except through
  Glue accessors.** All structural edits go through `instr-build` +
  `instr-replace!`/`instr-insert-before!`/`instr-delete!`.
- **No aggregate types cross the C ABI.** Only the seven `ccw_type`
  variants. Collections are traversed with `-count`/`-ref` pairs. If
  you feel the need to marshal a list, you are off-spec.
- **IR nodes cross the ABI as `uint64_t` ids, never pointers.** Id 0 is
  nil. The host owns node resolution.
- **String ownership:** copied at the boundary; each side frees its own
  copies; `error_message` out-params are malloc'd by the callee and
  freed by the caller. Run the Glue tests under ASan/LSan.
- **Oeuph rules are equivalences only.** No rule may "fix" incorrect
  code. Optimization and normalization differ only in cost model.
  Every ruleset declares a name; rules are unordered within a ruleset.
- **Dependencies are vendored and pinned.** S7, MPC, Tree-sitter, and
  grammars live in `third_party/` at exact commits recorded in
  `third_party/VERSIONS.lock`. Never fetch dependencies at build time;
  never bump a version without updating the lock file in the same
  commit.
- **Only Swaff adapters may reference Tree-sitter node types.** Nothing
  in `kliche/`, `ir/`, or below may include Tree-sitter headers.

## Conventions

- **Language:** C11 for host code (`ir/`, `glue/`, `executors/`,
  `oeuph/`, `tools/`), portable R7RS-small for kernels and rewrite
  rules. No C++ in the core.
  - Lua for the orchestrator + scheduler (`sched`) layer
- **Naming:** public C symbols are prefixed `ccw_`; capability ids
  match `[a-z0-9-]+(\.[a-z0-9-]+)+` (validate this in `ccw-manifest`).
- **Errors:** C functions return `ccw_status` or negative status;
  Scheme kernels raise error objects. Never `exit()` or `abort()` from
  library code.
- **Comments:** explain intent and spec-section references
  (e.g. `/* §5.4: round-trip required */`), not mechanics.

## Testing requirements

Every stage lands with tests in `tests/`. Minimum coverage per subsystem:

- **Glue/executor:** ABI version check rejection, missing-export load
  failure, capability verification before dispatch, accessor arity
  violations raising in Scheme, accessor errors surfacing as Scheme
  conditions, string ownership (ASan-clean).
- **IR:** text↔memory round-trip on every construct in both profiles;
  profile validation rejects cross-profile constructs.
- **Manifests:** `ccw-manifest --check` passes after generation; fails
  after any manual edit; capability strings validated against the
  grammar.
- **Kernels:** `strength-reduce` rewrites `imul x, 8` to `shl x, 3`,
  leaves `imul x, 7` and `imul c1, c2` untouched, and reports the
  correct capability set.
- **Oeuph:** fixed seed + budget produces byte-identical extraction
  across runs; budgets actually halt saturation; per-ruleset match
  statistics are emitted.

## Definition of done (per subsystem)

A subsystem is done when: it builds warning-clean (`-Wall -Wextra`),
its tests pass including under ASan where applicable, it satisfies the
relevant items of spec §9 (conformance summary), and nothing in it
required editing a generated file or an upstream vendored source.

## When the spec is silent

Prefer the smallest mechanism consistent with the spec's stated
rationale (e.g. the ABI is deliberately chatty-but-trivial; do not add
batching "optimizations"). Record any interpretation you had to make in
`DECISIONS.md` at the repo root, one dated entry per decision, so it
can be reviewed and promoted into the spec or reverted.
`

