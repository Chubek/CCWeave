# Decisions

## 2026-08-19 — Parthia REPL directives

The Parthia REPL now treats lines beginning with `#` as immediate directives,
separate from `;;`-terminated SML phrases.  It provides `#help`, `#quit`,
`#open MODULE` (reports matching structure/signature declarations in the
session), `#use "FILE"`, and `#load LIB`.  Native libraries for `#load` are
resolved through the colon-separated `SML_PARTHIA_PATH`, then loaded via the
existing extension ABI; ordinary source phrases continue to use the persistent
runtime and session history.

## 2026-08-19 — Parthia Basis Salvo and source library directives

`stdlib-salvo/sml-basis` is split between portable SML source (`Basis.sml`)
and a small native companion loaded through Parthia's extension ABI; scalar
host operations may additionally be reached through the existing integer FFI.
Parthia accepts line-oriented `use`, `load`, `#load`, and `CM.make` directives:
SML source directives are expanded before parsing, while native-library
directives require a runtime and use the extension init contract. This mirrors
the common workflow of SML implementations without introducing a second
module/build language into the compiler.

## 2026-08-19 — Scalar native extensions and FFI for Moonix and Parthia

Moonix and SML-Parthia expose a shared-shaped, C-linkage extension boundary
usable from both C and C++: extensions register named native entry points and
may also be loaded from a shared object through one version-independent init
symbol.  Calls exchange only scalar values (Moonix's Lua stack, or Parthia's
tagged `ccw_sml_value`), so aggregates and ownership-bearing objects never
cross the ABI.  Each runtime also provides a deliberately narrow, portable
integer FFI (`*_ffi_call_i64`) with explicit arity limits; unsupported
platforms return the runtime's unsupported/failure status rather than
silently emulating a foreign call.  This is the smallest contract consistent
with the language runtimes and can be extended without changing existing
entry points.

## 2026-08-17 — Cephyr uses CCWAS as its default assembler

Cephyr resolves `toolchain/ccwas` first for assembly tooling. A profile-provided
assembler remains authoritative, while system assemblers remain fallback
compatibility options for external builds that do not build CCWAS. The linker
selection is unchanged.

## 2026-08-17 — CCWAS encoding authority interpretation

The supplied x86reference XML is used as the x86-64 opcode/form authority.
Where the repository's CPU-ISA JSONL contains only QBE format metadata, ccwas
uses canonical baseline encodings for the implemented register forms and emits
a deterministic placeholder encoding for unsupported target forms. This keeps
the standalone tool usable while preserving deterministic output; adding
complete AArch64, RISC-V, and wasm encoding tables remains a follow-up.

## 2026-08-17 — Cephyr profile schema is a generated shared-data-model artifact

`manifests/CephyrProfile.schema.json` uses JSON Schema Draft 2020-12 to
describe the common object model accepted from `CEPHYR.yaml` and
`CEPHYR.toml`. It captures driver-level validity constraints in addition to
field types, including profile version 1, kernel selector exclusivity,
capability syntax, and the choice between `sched_script` and a non-empty
explicit kernel/rewrite plan. `ccw-manifest` generates and checks the schema
alongside the kernel manifests so it is never hand-edited.

## 2026-08-17 — Moonix v0.1 tier admission uses the available Sched boundary

D-0020 through D-0024 are accepted. Moonix emits versioned bytecode from the
pinned Lua 5.5 implementation and treats T0 as the semantic reference. Swaff
is the sole CST-facing component and produces On1x IR when the current Kliche
mapping supports the source. T1 admission requires that On1x lowering plus a
sealed plan containing inline caches, GC barriers, safepoints, and the
`on1x-complete` barrier. T2 plan construction is checked in v0.1, but execution
falls back to T0 because T2 is a v0.2 feature.

The current Sched public API can seal/revalidate plans and execute Oeuph
ruleset batches, but cannot execute kernel nodes or return a native code
buffer. Moonix therefore does not invent a private code-emission ABI: when
native kernel-plan execution is unavailable, admitted T1 chunks and all T2
chunks retain T0 execution semantics. Promoting a code-cache result contract
into Sched/Glue is required before native JIT emission can replace this
fallback.

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

## 2026-08-17 — Eight paradigm-oriented kernels

The request did not prescribe names or capabilities. Functional kernels use
the `functional-*` family, OOP kernels use `oop-*`, and imperative kernels use
`imperative-*`. They stay within the existing Core Accessor Set: concrete
rewrites use builder-based mutation, while analyses publish facts through
`analysis-put!`.

## 2026-08-17 — Standard ML Swaff lowering subset

The SML frontend lowers the profile-independent functional core: curried
functions, single-pattern `fn` bindings, scalar Basis operators, named and
higher-order application, `if`, short-circuit `andalso`/`orelse`, sequences,
and local simple-name `val` bindings. Because tree-sitter-sml intentionally
represents fixity syntax as application CSTs, the adapter normalizes recognized
Basis infix forms itself. Chained unresolved fixity, destructuring parameters,
multi-clause pattern matching, and non-function top-level values are reported
as unsupported rather than assigned semantics not specified by CCWeave.

## 2026-08-19 — Parthia module boundary

The SML parse-only adapter exposes a deterministic surface S-expression for
structures, signatures, functors, signature constraints, and nested module
expressions. Parthia consumes that representation and emits a compact
signature-erased core fact description; this keeps Tree-sitter types confined
to Swaff while the module system is removed before Kliche/IR lowering. Functor
application facts are sorted by their surface application path, with duplicate
paths receiving a stable suffix. This is the smallest ABI-neutral boundary
consistent with the proposal's defunctorization requirement while typed core
lowering continues to be staged independently.

## 2026-08-17 — Broadened codegen-* instruction coverage

The request asked for the `codegen-*` kernels to cover "the majority of
instructions needed for compilation" but did not enumerate a target ISA
subset. Weave IR's conventional scalar-integer inventory (§5.4, and as
exercised by `anf-normalize.scm`, `code-sink.scm`, and
`isel-tree-match.scm`) is: `imov`, the ALU set (`iadd isub imul idiv irem
iand ior ixor shl lshr ashr ineg inot`), the compare family (`icmp.{eq,ne,
lt,le,gt,ge}`), `load`/`store`, control flow (`jmp br ret phi`), and calls
(`call call.dynamic call.virtual`). Each of `codegen-x86-64.scm`,
`codegen-aarch64.scm`, `codegen-riscv64.scm`, `riscv64-codegen.scm`
(RV64GC), and `codegen-wasm32.scm` now maps this full set one opcode at a
time to target-mnemonic-prefixed forms (e.g. `x86-64.idiv`, `aarch64.orr`,
`rv64.seq`, `wasm32.div_s`), replacing the previous five-opcode
(`iadd/isub/imul/load/store`) placeholder tables. Floating-point,
vector, and target-specific addressing-mode legalization remain out of
scope here: they are not part of the shared scalar-integer core and would
require either new profile-neutral opcodes or per-target extension groups
not yet specified. Mapping stays a same-arity, same-operand-order rename;
register/immediate legalization is `isel-legalize`'s job and physical
allocation is `regalloc-*`'s, per the existing pipeline division.

## 2026-08-17 — Cephyr target triples and assembly handoff

Cephyr accepts a finite registry of GNU-style triples corresponding to the
four ccwas architectures and maps them to ccwas's architecture spelling.
`-S` writes deterministic target assembly; other compilation modes write a
relocatable object by invoking ccwas. The environment variable `CEPHYR_AS`
overrides the default ccwas command, while retaining the same target and
input/output argument contract.

## 2026-08-18 — Cephyr linker discovery

Cephyr's default linker is the in-tree CCWld executable, discovered from
the build target when available and otherwise from `PATH`. `CEPHYR_LD`
overrides this default (and profile linker configuration) for external
toolchains and test doubles.
# 2026-08-18 — Pin vendored ISL through a CCWeave configuration shim

ISL's autotools-generated `isl_config.h` and `gitversion.h` are not checked
into the upstream vendored tree.  CCWeave therefore supplies equivalent
generated configuration headers under `third_party/isl-config/`, builds the
library directly from its checked-in C sources with GMP, and applies all
scheduler/quota options in `glue/ccw_isl.c`.  This keeps builds offline and
reproducible without modifying upstream ISL sources.

# 2026-08-18 — Soft-fail polyhedral kernels on non-affine regions

The initial kernel slice publishes explicit `nonaffine`/`unavailable` facts
when a host has not registered an ISL fact accessor.  This preserves the
ISLKERN deterministic soft-failure contract while allowing a later host
integration to replace those payloads with canonical ISL strings.

# 2026-08-18 — Run Cephyr's polyhedral tier at O2

Cephyr wires `affine-extract`, `dep-poly`, `isl-schedule`, and `tile-plan`
into the O2 Sched plan as a strict producer/consumer chain.  The ISL tier is
not included in O0/O1 because ISLKERN describes dependence-aware scheduling
and tiling as the expensive optimization tier; keeping it at O2 preserves
the fast-pipeline contract while making the core-to-Tilly ordering explicit.
# D-0046 (2026-08-19): Delphia v0.1 compiler façade

Delphia v0.1 uses the existing Delphi Swaff adapter as its frontend and
emits deterministic target assembly stubs from validated Tilly IR. The
adapter gate vector, OOP stereotype version, and manifest provenance are
recorded in a `.note.ccw` assembly section. Full target instruction selection
and link orchestration remain delegated to the existing CCWeave toolchain.

# 2026-08-19 — stdlib-salvo/libc and Cephyr stdlib manifests

stdlib-salvo/libc (`salvoc`) is a freestanding Linux LP64 libc
(x86-64/aarch64/riscv64) accompanying Cephyr: own headers, syscall layer
via per-arch inline asm, crt0 + `__libc_start_main`, and a core subset
(string, ctype, errno, stdlib, unbuffered stdio with integer-only
formatting, unistd, time, assert). Interpretations recorded while the
spec is silent:

- **Stdlib manifest schema (version 1)** lives in
  `stdlib-salvo/libc/Libc.yaml`: `include_dirs`, `library_paths`,
  `libraries`, `start_files`, plus optional `targets` sequences merged
  after the global lists on an exact triple match. It is hand-authored
  configuration (like `CEPHYR.yaml`), not a generated `manifests/`
  artifact. Entries resolve relative to the manifest file; absolute
  entries pass through; `libraries` entries are `-l` names, not paths.
- **Discovery**: Cephyr loads the manifest from the new
  `cephyr_options.stdlib_manifest` field, else `$CEPHYR_STDLIB_MANIFEST`,
  else discovers `stdlib-salvo/libc/Libc.yaml` (then `../stdlib-salvo/...`)
  relative to the working directory — mirroring the Sched-script
  search convention. Explicit manifests are authoritative (load failure
  is a compile error); the in-tree default is advisory (absence or
  malformation compiles without stdlib wiring).
- **Merge order**: stdlib include dirs come after profile/CLI `-I`
  paths (user paths win); stdlib `library_paths`/`libraries` come after
  profile/CLI ones (libc links last). `start_files` is a new retained
  driver channel, like `-L`/`-l`, pending link orchestration — Cephyr
  deliberately still stops before linking, so today the manifest's
  operative effect is the preprocessor include path.
- **Build boundary**: salvo-libc is compiled by the host toolchain
  (`-ffreestanding -nostdinc`); Cephyr's Swaff C adapter does not yet
  accept the inline-asm syscall layer. The build configures a second
  manifest in the build tree with an absolute include path so
  `CEPHYR_STDLIB_MANIFEST=<build>/stdlib-salvo/libc/Libc.yaml` consumes
  the freshly built archive. errno and the allocator are
  single-threaded; stdio is unbuffered; `%f`/`%e`/`%g` await a
 salvo-libm.

# 2026-08-19 — Cephyr assembly input classification and linking

Cephyr treats `.s`, `.as`, and `.asm` as assembler inputs without
preprocessing.  `.S` is preprocessed first using `CEPHYR_CPP` (or the
configured preprocessor, with the system `cpp -E -P` fallback for assembly
syntax).  Additional suffixes can be registered through the
`CEPHYR_AS_EXTENSIONS` comma-separated environment variable.  Assembly is
assembled with the in-process CCWas API by default; an explicitly configured
`CEPHYR_AS`/profile assembler is invoked as an external command.  A normal
assembly invocation then passes the resulting object through CCWld with
`_main` as the default entry symbol; `-c` retains compile-only behavior.
