# Decisions

## 2026-08-25 — FrontMX replaces the Swaff/Kliche frontend boundary

FrontMX is now the forward frontend integration point described by
`.agents/FRONTMX-SPECS.md`.  It combines grammar parsing and attribute/action
metadata into one opaque C API under `frontmx/`, while keeping the specified
subsystem boundaries (`sppf`, `gss`, `grammar`, `ast`, `attr`, `diamb`,
`rewrite`, and `generate`) explicit for subsequent GLR and salvo-backed
implementations.

The initial implementation parses `.fmx` files as S-Expressions through the
vendored SFSExp library, validates the required schema keys and cardinalities,
retains terminals, productions, limitations, semantic actions, rewrites, and
the entry node, and verifies that file-based grammars use a language name
matching the `.fmx` basename.  Terminal regular expressions are compiled and
matched with the vendored RE2 dependency, rather than introducing a second
regex engine or a host-side lexer.

FrontMX is registered as `ccw_frontmx` in the global CMake build and has a
focused regression test covering parsing, metadata navigation, and terminal
matching.  The first `fmx-salvo` grammar declarations for Cephyr, Moonix,
Delphia, and SML-Parthia establish the new source format.  Generated frontend
artifacts are emitted only through `frontmx_generate`; the salvo and manifest
directories remain source/generated boundaries and are not hand-edited.

## 2026-08-24 — Cephyr schedules terminate at `codegen.sched-list` capability

The Swaff+C issue resolution exposed a manifest drift in Cephyr’s `O0.lua`,
`O1.lua`, and `O2.lua`: they required the retired `codegen.emit-x86-64`
capability, which is no longer produced by `Kernel.yaml` after the emit kernel
was retired and assembly emission moved back under `codegen.sched-list` +
host-side fallback (`emit_target_assembly` when the kernel-supplied artifact is
absent).

The scripts now only require capabilities present in the live manifest and end
their codegen chain at `codegen.sched-list`:

- `compilers/cephyr/sched/O0.lua`
- `compilers/cephyr/sched/O1.lua`
- `compilers/cephyr/sched/O2.lua`

This preserves the same execution model (`S:require` for mandatory capabilities,
no host-side op switching), but aligns sched requirements with actual manifest
contents so `compilers/cephyr` can reach a sealing pass and execute the Dijkstra
command-line repro successfully.

## 2026-08-24 — Vendored bridges use typed APIs, not a generic dispatcher

`glue/vendored-bridge.h` centralizes the existing C-callable SIMDe, utf8proc,
ISL, and Dynalo surfaces, while `glue/vendored-bridge.hpp` adds the optional
hipSYCL extension that depends on Glue executor types. `GlueSTD.h` re-exports
the C header so executors and hosts see one stable boundary. The bridge does
not add a name-based registry or aggregate argument marshalling: each vendored
library retains its typed scalar/opaque-handle API, consistent with the Glue
ABI's deliberately simple boundary and the ban on aggregate ABI values.

## 2026-08-24 — Dijkstra reproduction separated codegen and ELF defects

The reproducible Cephyr Dijkstra investigation established two independent
failure classes.  The first loss of the observable `printf` computation was
not in purity/DCE: `kernels/codegen-emit-x86-64.scm` published a lossy
return-value-only assembly artifact (`main` became `return 0`) even though the
canonical IR still contained the call.  The kernel now declines to publish
that artifact so Cephyr serializes the instruction-preserving canonical IR;
purity also classifies calls conservatively as side-effecting.  CCWas now
records unresolved external branch targets, and CCWld resolves archive
members iteratively so libc calls remain linkable.

The second defect was in CCWld image construction.  Executable layout now
reserves space for ELF/program headers, maps the first `PT_LOAD` from file
offset zero, keeps `e_entry` inside the mapped executable bytes, and enforces
power-of-two `p_align` plus `p_offset ≡ p_vaddr (mod p_align)`.  A minimal
`_start` image now executes and exits successfully.  The GCC oracle for the
Dijkstra source is `4` at both `-O0` and `-O2`; the full Cephyr Dijkstra run
still requires follow-up work in existing arithmetic/control-flow lowering,
so this entry must not be read as claiming end-to-end correctness yet.

## 2026-08-24 — Named-let recursion in kernels must thread every loop parameter

Cephyr compilation failed at the final codegen stage with
`kernel error: wrong-number-of-args: ins: not enough arguments: (+ ii 1)`.
The `ret-value` walker in `kernels/codegen-emit-x86-64.scm` iterates with a
named `let ins ((ii 0) (known known))`, but its `else` branch recursed as
`(ins (+ ii 1))`, dropping the `known` alist of constant returns.  s7 binds
a named let to a procedure of exact arity, so the first instruction that was
neither a const-`iconst` nor an `x86-64.ret` raised the arity error; the
`else` branch now threads `known` the way the `iconst` branch always did.
Kernel writers should treat a named-let parameter list as an arity contract:
every recursive call site must pass all parameters, including the ones the
branch does not use.  The fix also confirmed two operational facts: kernel
text is loaded from the manifest dir at compile time, so kernel repairs take
effect without rebuilding the host compiler; and executor-side s7 errors
surface verbatim as `kernel error`/`scheduler error` from the driver, which
correctly localizes such defects to the kernel rather than the scheduler.

## 2026-08-24 — Compiler and interpreter backends are kernel-only

Language implementations may own language-specific preprocessing, parsing,
typing, and AST construction, but once canonical Weave IR exists, every
analysis, transformation, lowering, instruction selection, allocation,
scheduling, interpretation/JIT lowering, and target-artifact emission step
MUST be supplied by kernels and executed through one sealed Sched plan. A
compiler/interpreter host MUST NOT invoke kernels or rewrite rules directly
outside that plan, and MUST NOT contain a fallback instruction printer,
assembly-text generator, bytecode emitter, or equivalent IR-to-target switch.
If a target artifact is needed, the responsible kernel publishes it through
Glue-owned IR facts or another specified kernel result channel; the host may
only serialize that result or pass it to the next toolchain stage.

Cephyr's `codegen.emit-x86-64` kernel and scheduler kernel-execution path are
the reference implementation. If a required capability is missing, implement
the kernel and regenerate the live manifests before changing the language
host.

## 2026-08-24 — Cephyr's emitted objects must remain linkable through ccwas/ccwld

The Dijkstra failure exposed a chain of integration gaps rather than a single
bad instruction: Cephyr's x86-64 fallback had to stop emitting bare virtual
names and instead map them to stack slots; ccwas had to write ELF symbol tables
with locals first and a correct `sh_info`; ccwld had to read ELF string tables
as raw byte ranges instead of C strings; and ccwld relocation resolution had
to honor same-object local labels before falling back to the global resolved
table.  That progression is now the expected path for Cephyr-generated
objects, and future fixes in this area should preserve linkability at each
stage rather than papering over one stage's output format.

## 2026-08-24 — Swaff adapters are total over their grammars

Swaff adapters must not reject a valid Tree-sitter construct merely because
its typed elaboration is owned by the language compiler/interpreter.  C, Lua,
OCaml, SML, and Delphi adapters now lower otherwise-unhandled expressions and
statements to explicit `opaque.expr`/`opaque.stmt` IR instructions, retaining
the source spelling as instruction metadata.  Constructs with a defined
Kliche mapping continue to use that stereotype; opaque nodes are a deliberate
handoff boundary for language-specific elaboration.  This makes
`unsupported_nodes` describe malformed/failed lowering only, while preserving
the complete frontend grammar surface for downstream consumers.

## 2026-08-24 — Semantic and rewrite responsibilities progress to shared schemes

The language frontends are progressing from local, case-by-case semantic and
rewrite handling toward shared CCWeave schemes.  First, Cephyr, Moonix, and
SML-Parthia dispatch semantic analysis through Oeuph's declarative
`sema-salvo` using `ccw_sema_analyze`.  Rewrite execution now follows the same
direction through `ccw_rewrite_scheme_apply`, which centralizes sealed-plan
validation, `rewrite-salvo` resolution, Oeuph execution, budgets, cost models,
and diagnostics.  New language implementations should adopt these two shared
entry points instead of embedding language-specific semantic or rewrite
dispatch.

## 2026-08-24 — Frontends use the central rewrite scheme

Rewrite execution is exposed through `ccw_rewrite_scheme_apply`, a stable
host-facing entry point over sealed scheduler plans.  The scheme owns plan
validation, `Stdrewrite.yaml` resolution, `rewrite-salvo` loading, Oeuph
budgets, cost models, deterministic execution, and per-ruleset diagnostics.
Cephyr calls it for its scheduled rewrite phase, Moonix uses it for T2
On1x plans, and SML-Parthia exposes it through its plan API.  Future language
implementations should depend on this entry point rather than calling
`ccw_plan_apply_rewrites` directly or reimplementing rewrite-salvo discovery.

## 2026-08-24 — Frontend semantics dispatch through Oeuph's sema-salvo

Cephyr, Moonix, and SML-Parthia now enter semantic analysis through the
shared `ccw_sema_analyze` Oeuph API.  Each frontend selects only its language
domain rulesets; the dispatcher loads the corresponding declarative
`sema-salvo` files, records the applied rulesets on the canonical IR module,
and performs the common IR/profile validation before lowering or execution
continues.  This removes frontend-owned case-by-case semantic dispatch from
the pipeline and gives all three consumers one observable ruleset-loading
contract.  The legacy Cephyr AST sema remains available for compatibility and
unit tests, but is no longer called by the Cephyr driver.

## 2026-08-24 — Cephyr embeds ucpp through its text-emission contract

The in-process Cephyr preprocessor binds both `lexer_state.output` and
ucpp's process-global `emit_output` to the same memory stream, then calls
`check_cpp_errors()` before reading the stream.  This follows ucpp's
standalone driver contract and prevents an empty preprocessed translation
unit from reaching Swaff.  The previous empty-IR failure surfaced
misleading linker diagnostics (`main` missing and an unplaced section);
preprocessing failures must instead be diagnosed before parsing.

## 2026-08-24 — Swaff C lowers structured control flow through Kliche

The C adapter now lowers C control-flow statements into imperative Kliche
constructs: `for`, `while`, and `do` loops; `break` and `continue`; and
`switch`/`case` dispatch with labeled statements.  Loop context is tracked
in the adapter so break/continue targets remain explicit block edges.
Increment/decrement and comma expressions are lowered as part of loop
initializers and updates.  Array declarations, initializer lists, indexed
loads, and indexed stores use Kliche array operations.  This keeps
Tree-sitter-specific handling confined to Swaff while preserving canonical
core-IR construction and makes unsupported-statement diagnostics indicate
remaining expression/runtime gaps rather than basic C control flow.

## 2026-08-24 — GPU-accelerated compilation kernels backed by hipSYCL

Seven new R7RS Scheme kernels were added under `kernels/gpu-*.scm` that
declare GPU-accelerated compilation capabilities.  Each kernel is a
conformant `(define-library (ccweave kernel ...))` module exporting
`kernel-info`, `kernel-capabilities`, and `kernel-apply` (§2.2).

The kernels and their capabilities:

- `gpu-parallel-parse.scm` — `parse.gpu-parallel`: batches lex/parse/tokenize
  instructions for parallel GPU dispatch.
- `gpu-pattern-match.scm` — `lower.gpu-pattern-match`: GPU decision-tree
  construction for pattern-match compilation (>8 arms → binary, ≤8 → linear).
- `gpu-dataflow.scm` — `analysis.gpu-dataflow` (and sub-capabilities
  `analysis.gpu-reaching-defs`, `analysis.gpu-liveness`,
  `analysis.gpu-def-use`): GPU-parallel fixed-point dataflow iteration.
- `gpu-batch-inline.scm` — `opt.gpu-batch-inline`: GPU-computed heuristic
  scores for call-site inlining decisions.
- `gpu-const-fold.scm` — `opt.gpu-const-fold`: GPU-parallel identification
  of foldable constant expressions.
- `gpu-regalloc.scm` — `opt.gpu-regalloc`: GPU-accelerated interference-graph
  colouring for register allocation.
- `gpu-codegen.scm` — `lower.gpu-codegen`: GPU-parallel dispatch of code
  generation across functions.

A hipSYCL C++ backend (`glue/bridge/hipsycl-bridge.cpp`) implements the
GPU operations via the SYCL 1.2.1 runtime.  It registers `gpu-has?`,
`gpu-device-count`, and `gpu-parse-batch` as host extension accessors;
kernels feature-test them with `(glue-has? …)` before use.  The backend
gracefully falls back to sequential execution when no SYCL device is
available.  The build is gated behind `CCWEAVE_ENABLE_HIPSYCL=ON`.

## 2026-08-23 — CCWAS reads instructions through the vendored Tree-sitter assembly grammar

`third_party/tree-sitter-asm` (RubixDev, pinned in
`third_party/VERSIONS.lock` as generated language ABI 14) is built from its
checked-in generated sources as `ccw_tree_sitter_asm`, under the same
vendored-grammar rule as the Swaff grammars: direct compilation of
`src/parser.c`, no `add_subdirectory`, no regeneration, no fetching at build
time. Inside ccwas the grammar is the primary reader for instruction
statements only. When the build has Tree-sitter enabled, `ccw_parse_line`
first offers the instruction line to `ccw_parse_insn_ts`
(`parse/ccw_parse_ts.c`), which maps a clean parse — mnemonic word, integer
immediates, register/label identifiers, bracket memory operands — onto the
same `ccw_stmt_t` the hand-rolled reader produces.

The grammar is deliberately not a dialect authority. Labels, directives,
macros, line structure, and ISA validation stay in the hand-rolled pass,
which also remains the unconditional reader when a form does not map
exactly: index/scale memory operands, float literals, string operands,
size-prefixed Intel operands (`dword ptr [...]`), the grammar's
parenthesized/`*rel[...]` pointer forms, and its non-ccwas literal spellings
(`#`/`$` prefixes, `_` digit separators) all decline to the hand-rolled
reader rather than produce an approximate statement. The reason is the
dialect mismatch: the grammar's `meta` rule cannot lex ccwas's
width-explicit `.2byte/.4byte/.8byte` directives, it treats `#` as a comment
character where ccwas deliberately does not, and it would accept the GNU
directive aliases ccwas rejects. Accept/reject authority stays with ISA
validation and the directive pass; the grammar only widens the instruction
syntax that reaches them (for example comma-free space-separated operands
and the `[reg, disp]` bracket form, which the hand-rolled splitter mangles).
Builds with `CCWEAVE_ENABLE_TREESITTER=OFF` compile the same call as a stub
that always declines, so the hand-rolled reader is the only code path. The
existing encode goldens now exercise the Tree-sitter reader on every
instruction statement, so both configurations are held to byte-identical
objects.

## 2026-08-21 — Kliche helpers are the Swaff lowering boundary

The common Kliche construction helpers introduced with the stereotype
expansion are part of the public Kliche surface, so Swaff adapters can use
them without reaching into Kliche implementation headers.  C, Lua, OCaml,
SML, and Delphi lowering now route arithmetic through `ccw_kliche_binop`,
comparisons through `ccw_kliche_cmp`, and supported floating-point literals
through `ccw_kliche_float_const`.  Delphi and Lua loop lowering also use
`ccw_kliche_loop` for the entry edge while retaining their frontend-specific
condition/body block construction.  This keeps CST handling in Swaff while
ensuring all shared lowering patterns receive Kliche's validation and
canonical core-IR emission.  Existing table/string and language-specific
operations remain frontend-owned because their runtime contracts are not
array or scalar Kliche operations.

## 2026-08-21 — Kliche stereotype de-stubbing and expansion

The kliche stratum was strengthened from thin single-instruction wrappers
into a proper stereotype library (§6.1).  The existing public interface is
preserved; every existing function signature is unchanged.  New functions
fill in the missing paradigm constructs.

**Common layer** (`ccw_kliche_common`):
- All inputs validated (null ir/blk/opcode return 0 immediately).
- `ccw_kliche_emit` now checks every operand resolution and propagates
  failures instead of silently producing broken IR.
- Added `ccw_kliche_emit_multi` for multi-instruction patterns, plus
  convenience helpers: `ccw_kliche_binop`, `ccw_kliche_cmp`,
  `ccw_kliche_alloca`, `ccw_kliche_gep`, `ccw_kliche_load`,
  `ccw_kliche_store`.
- Operand kind enum extended with `CCW_KO_FLOAT` and `CCW_KO_STR` for
  float constants and string symbols.

**Functional stereotype** additions:
- `ccw_kliche_record_alloc`, `ccw_kliche_record_tag`,
  `ccw_kliche_record_field_get`, `ccw_kliche_record_field_set` —
  algebraic data types as tagged records.
- `ccw_kliche_tag_switch` — pattern-matching dispatch via a single
  `switch` instruction with (tag, default, case*) operands.

**Imperative stereotype** additions:
- `ccw_kliche_float_const` — float constant emission.
- `ccw_kliche_loop` — while-loop skeleton (header/body/exit blocks).
- `ccw_kliche_cast` — numeric type casts (trunc, sext, fpext, fptrunc,
  sitofp, fptosi) with automatic opcode selection.
- `ccw_kliche_phi` — SSA phi node with parallel value/block arrays.
- `ccw_kliche_array_alloc`, `ccw_kliche_array_load`,
  `ccw_kliche_array_store` — heap-array operations.

**OOP stereotype** additions:
- `ccw_kliche_new` — allocate and construct an object in one step.
- `ccw_kliche_vtable_store`, `ccw_kliche_vtable_build` — vtable
  mutation and construction.
- `ccw_kliche_field_get`, `ccw_kliche_field_set` — object field access.
- `ccw_kliche_super_call` — superclass method dispatch through the
  parent vtable.
- `ccw_kliche_interface_dispatch` — dispatch through an interface
  vtable by interface id.
- `ccw_kliche_instanceof`, `ccw_kliche_dynamic_cast` — type checking
  and guarded downcast.
- `ccw_kliche_throw`, `ccw_kliche_rethrow` — exception raising from
  user code and handler rethrow.

All constructs emit only core-IR instructions; no profile-specific
constructs are introduced.  The existing `test_kliche_swaff` suite
(40 checks) passes unchanged.

## 2026-08-20 — SML_PARTHIA_PATH comma-separated directive search

Parthia's `use`, `load`, `#load`, and `CM.make` directive targets now resolve
through one shared rule: a target that already names a location (contains `/`
or begins with `.`) is opened literally, so absolute and working-directory
relative paths keep their native errors; a bare name is probed as given
first, then with the usual shared-object variants (`NAME`, `NAME.so`,
`libNAME.so`, `libNAME`), in each directory of the comma-separated
`SML_PARTHIA_PATH` in order.  The REPL's private colon-separated lookup is
retired in favour of the public `ccw_sml_parthia_resolve_path`, so REPL and
file mode agree, and directive failures now name the offending target.  The
`examples-salvo/sml-parthia` corpus uses bare names (`use "Basis.sml"`) and
is run with the basis directory on the search path, e.g.
`SML_PARTHIA_PATH=stdlib-salvo/sml-basis
./build/interpreters/sml-parthia/sml-parthia
examples-salvo/sml-parthia/01_hello.sml`.  Installation installs the basis
to `share/ccweave/sml-basis` and a `bin/sml-parthia` launcher that appends
that directory, when present, to the caller's `SML_PARTHIA_PATH`; entries
the user provides keep precedence.

## 2026-08-19 — dynalo/dyncall back the Moonix and Parthia interop layers

Moonix and SML-Parthia retain their stable C-linkage native-extension,
C-interoperability, and scalar FFI entry points, but their implementations no
longer call platform `dlopen`/`dlsym` APIs or cast fixed-arity function
pointers.  Shared-library handles and exported symbols are acquired through
the vendored dynalo C++ bridge, while integer FFI calls use vendored dyncall
call VMs with the platform's default C calling convention and an eight-argument
scalar limit.  This keeps C and C++ extensions interoperable, makes loading
portable across supported hosts, and gives the FFI its ABI-correct marshalling
without allowing aggregates or ownership-bearing values across the language
boundaries.

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
scheduler/quota options in `glue/bridge/isl-bridge.c`. This keeps builds offline and
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

# 2026-08-19 — examples-salvo language corpus and opt-in build

Add `examples-salvo/` with 25 source examples for each supported language
consumer: Cephyr (`.c`), Delphia (`.pas`), Moonix (`.lua`), and SML-Parthia
(`.sml`). Each language directory owns a CMake target that registers its
source corpus without forcing a compiler run during the default build.
`CCWEAVE_BUILD_EXAMPLES` is therefore `OFF` by default and conditionally adds
the examples tree from the root build. Cephyr examples resolve headers from
`stdlib-salvo/libc/include`; Parthia examples resolve the portable Basis
source from `stdlib-salvo/sml-basis`. This keeps examples useful as frontend
fixtures while avoiding a second, language-specific build orchestration layer
until the compiler drivers expose stable batch compilation interfaces.

# 2026-08-20 — platform syscall kernels

The three Linux syscall kernels share one `syscall` Weave IR operation. Its
first operand is a constant syscall number and its remaining operands are the
six Linux ABI arguments; platform kernels lower it to `x86-64.syscall`,
`aarch64.svc`, or `riscv64.ecall`. The syscall-number tables remain sourced
from `.agents/SYSCALL-*.txt`; the small name tables in the Scheme files are
documentation for the low-level runtime, not a replacement for those sources.
The existing CCWas embedding API currently assembles objects but does not
provide an in-process executable loader, so execution is deferred to the
target code-generation/link stage rather than performed during kernel
application.

# 2026-08-20 — platform I/O kernels

The shared `io.read`, `io.write`, `io.close`, and `io.open` operations are
low-level, unbuffered wrappers. Platform I/O kernels lower them to the shared
`syscall` operation and the corresponding syscall kernel then lowers the trap
for its ABI. Linux x86-64 uses `open(2)`; AArch64 and RV64 use
`openat(AT_FDCWD, path, flags, mode)` because their asm-generic tables do not
provide legacy `open`. This preserves an identical IR-level I/O API without
inventing a platform-independent kernel syscall number.

# 2026-08-20 — Parthia execution boundary

Parthia's public execution API returns a caller-owned textual rendering of
the conventional SML `it` binding; values remain arena-owned inside the
runtime. The initial executable tier is a deterministic core-ML evaluator
with a phrase cache for JIT reuse, while native scalar extensions and the
Basis TextIO surface remain behind the existing scalar FFI boundary. This
keeps aggregate SML values out of the C ABI until the CCWeave native code
emission and loader contracts expose a stable representation.

## 2026-08-20 — CCWld phase enum canonical location

The `ccwld_phase` enum is defined in `abi/ccwld-plugin.h` (the normative ABI
header) and consumed by `expr/ccwld_expr.h` via `#include "ccwld-plugin.h"`.
The duplicate definition previously in `ccwld_expr.h` was removed to avoid a
redeclaration error when `ccwld.h` includes both headers. The `CCWLD_PHASE_LOAD`
through `CCWLD_PHASE_EMIT` enumerators (and the `CCWLD_PHASE_RESOLVED` alias)
are the single source of truth. The include order in `ccwld.h` — ABI headers
first, then plan/expr — ensures the enum is available before any consumer.

## 2026-08-20 — Anonymous struct typedefs in the plan IR

`ccwld_sym`, `ccwld_mem`, and `ccwld_sec` are defined as `typedef struct { … }`
(anonymous structs without tags). Pointer and reference types must use
`ccwld_sym *`, `ccwld_mem *`, `ccwld_sec *` — never `struct ccwld_sym *` etc.
The tagged form `struct ccwld_plan` is the exception: `ccwld_plan` is defined
as `typedef struct ccwld_plan { … } ccwld_plan` with a proper tag, so both
`ccwld_plan *` and `struct ccwld_plan *` are valid. This distinction matters
in `ccwld_expr.c` where helper functions (`plan_find_symbol`, `plan_find_region`,
`plan_find_section`) and their call sites use the untagged form.

## 2026-08-20 — LIEF emitter API compatibility with vendored LIEF

The vendored LIEF (third_party/LIEF) API differs from the originally coded
interface in `emit_lief.cpp`:
- `LIEF::ELF::Parser::parse(input)` returns `std::unique_ptr<LIEF::ELF::Binary>`,
  not a raw `LIEF::ELF::Binary *`. The emitter now assigns the result directly
  rather than calling `.reset()`.
- `binary.symtab_symbols()` returns an iterable of `LIEF::ELF::Symbol` by value,
  not pointers. The range-for loop uses `auto &symbol` and accesses members
  with `.` instead of `->`, and null-check is removed since the iterator never
  yields null.
These changes bring the emitter in line with the LIEF version pinned in
`third_party/VERSIONS.lock`.

## 2026-08-20 — LIEF emission fallback in ccwld_link_run

When `ccwld_emit_lief` fails (e.g. LIEF not built, or parse/emission error),
`ccwld_link_run` now falls through to the text-based fallback output instead of
returning the LIEF error immediately. The text fallback writes a
`CCWLD-OBJECT` header with the serialized plan and `.note.ccw` hash. This
ensures the convenience API (`ccwld_link_files`) used by Cephyr produces
output even when LIEF is unavailable, and the text format serves as a
diagnostic/audit artifact. The LIEF success path still returns early.

## 2026-08-20 — LTO phase in the pipeline dispatch

The phase pipeline in `ccwld_link_run` now includes `CCWLD_PHASE_LTO` between
resolve and gc, matching the spec §3 order:
`load → resolve → [LTO] → gc → layout → relocate → emit`.
Previously the LTO phase was absent from the dispatch, so LTO-configured plans
would silently skip the LTO recompile step. The LTO backend is still a stub
(`abi.c`), but the hook point is now correctly sequenced.

## 2026-08-20 — CCWeave build configured with LIEF for Cephyr integration

Cephyr links object files through CCWld's embedding API (`ccwld_link_files`).
The build is now configured with `CCWEAVE_ENABLE_LIEF=ON` so that
`emit_lief.cpp` is compiled instead of `emit_lief_stub.c`, producing real ELF
executables. Without LIEF, the stub returned a hard error; with LIEF enabled,
the basic single-object link path produces a valid (though minimal) ELF binary
that Cephyr can use as its default linker backend.

## 2026-08-23 — CCWld fully implemented: one plan IR, two frontends, real pipeline

The CCWld linker (`toolchain/ccwld`, specs `.agents/CCWLD.md` + `.agents/LCCWLD.md`) is
complete. Recording the decisions that shaped the implementation, in the spec's numbering:

- **D-0039**: Both frontends lower to one immutable, serializable link-plan IR; the mpc
  frontend builds deferred expression nodes (no eager evaluation) so the two surfaces are
  provably parity-checkable (`tests/parity` string-compares the canonical serializations).
  IR seals after all frontends/includes; post-seal mutation only via phase hooks within
  phase scope.
- **D-0040**: The phase pipeline (load▸resolve▸[lto]▸gc▸layout▸relocate▸emit) is fixed and
  not user-reorderable; hooks and plugins attach at phases under phase-scoped mutability and
  never reorder phases.
- **D-0041**: LTO is a native backend behind `ccwld-lto.h`; the Lua/ld-script surfaces only
  configure it. ABI-versioned; non-deterministic backends must run `jobs=1` for
  reproducible builds or the output is marked non-reproducible.
- **D-0042**: Plugins are native, ABI-versioned (`ccwld-plugin.h`), phase-scoped, and
  CCWld-scheduled; options pass as JSON from both frontends; plugin/hook conflicts get a
  deterministic order (plugins first) plus a mandatory warning — CCWld is the conflict
  authority.
- **D-0043**: No-passthrough at the link stage — CCWld never blindly copies unrecognized
  input constructs into the output; an unplaced alloc section is explicitly placed, GC'd, or
  fatal.

Implementation decisions beyond the spec text:

- **lccwld binds with the raw Lua C API** (vendored Lua 5.5), not sol2 — sol2's Lua 5.5
  support is unverifiable in-tree. Consequences: expression objects are userdata with
  metamethods that *clone* child trees (no shared ownership), and the sandbox nils out
  `io`/`os`/`package`/`require`/`load`/`loadfile`/`dofile`/`debug`, replacing `os`
  with a frozen table whose `getenv` answers only `CCWLD_*` keys (`math.random` removed).
- **The Lua state outlives phase 0**: hooks run during the link, so the runtime hangs off
  `plan->frontend_ctx`/`frontend_ctx_free` and closes with the plan.
- **lccwld statement sink**: `ccwld.assign`/`provide` return handles; `ccwld.out{}`
  consumes handles appearing in its spec array into that section's context, `ccwld.sections{}`
  consumes array-level handles at the top level, and unconsumed handles flush at seal with
  top-level scope. This preserves ld-style section-scoped semantics through Lua's
  table-constructor evaluation order.
- **Emission is a pure-C ELF64 writer** (always available, `emit/ccwld_emit_elf.c`),
  superseding the 2026-08-20 text-fallback plan: LIEF (`emit/ccwld_emit_lief.cpp`) and
  Binaryen remain optional under `CCWEAVE_ENABLE_LIEF`/`CCWEAVE_ENABLE_BINARYEN`.
  The stub `abi.c` LTO backend is removed — the pipeline dlopens real backends (D-0041),
  and the in-tree reference backend is `tests/lto/lto_ref.c`.
- **Driver-level declarations flow through the frontends pre-seal**: positional inputs,
  `-L`/`-l`, `-e`, `--plugin`/`--plugin-opt`, and `--lto-*` are passed as a
  `ccwld_driver_defs` struct into `ccwld_run_lua`/`ccwld_run_ldscript` and applied before
  the script body (command-line `-e` wins over the script's ENTRY, GNU-style). Driver-only
  flags (`--gc-sections`, `--print-plan`, cache options) set `plan->options` post-seal —
  they are outside the serialized declarative plan and join the cache key separately.
- **ld-script `AT(<expr>)` lowers to a dedicated `at_expr`** on the section (distinct from
  `AT>` region form); `SUBALIGN`, `ORIGIN`, and `LENGTH` accept constants only in the mpc
  frontend (lccwld passes numbers for the same fields — parity preserved).
- **Default sections are synthesized at link time** when a plan has none
  (`.text`/`.rodata`/`.data`/`.bss` with conventional selectors), so script-less
  command-line links (`ccwld a.o -o a.out`) work without a script.
- **`.ccw.lto` is the LTO-module marker section**; its payload is the line-oriented
  `CCWIR1` text IR consumed by the reference backend, which lowers each module to a native
  ET_REL through the emit callback and re-enters the pipeline before gc.
- **Reference plugin and LTO backends live under `tests/`** and build as MODULEs against
  the shipped ABI headers; test executables export their symbols (`ENABLE_EXPORTS` +
  `-rdynamic`) so dlopened modules resolve `ccwld_link_*`/`ccwld_lto_*` from the host.

## 2026-08-24 — ccwld_driver_defs moved to plan header to fix include ordering

The `ccwld_driver_defs` struct was originally defined in `ccwld.h` after
`#include "plan/ccwld_plan.h"`, but `ccwld_plan.h` references the type in its
frontend entry-point declarations and `ccwld_plan.c` includes `ccwld_plan.h`
directly (not through `ccwld.h`). The struct definition was moved to
`ccwld_plan.h` before the function declarations and removed from `ccwld.h`.
The struct tag is `ccwld_driver_defs_s` to allow forward-declaration if
needed later. Since `ccwld.h` includes `ccwld_plan.h`, all consumers see the
definition through the same include path.

## 2026-08-24 — strdup(NULL) guarded in ccwld_link_files

The `ccwld_output_simple` convenience struct has optional fields (`entry`,
`soname`, `osabi`) that may be NULL when the caller passes no options. The
original code called `strdup(options ? options->entry : NULL)` which is UB
under glibc's `__attribute__((nonnull))` declaration. The fix checks for
non-NULL before calling `strdup`, leaving the field NULL when the source is
NULL — consistent with the struct's optional-field semantics.

## 2026-08-24 — Merged duplicate CCWLD_EXPR_SEGMENT_START case in ccwld_expr_to_string

The `ccwld_expr_to_string` function had two `case CCWLD_EXPR_SEGMENT_START`
labels: one handling only the 1-argument form and a later unreachable one
handling both 1-arg and 2-arg forms. The two were merged into a single case
that tests `e->b` for the 2-argument path. The second label was also
erroneously falling through to `CCWLD_EXPR_UNARY` due to a prior incomplete
merge — the spurious fallthrough label was removed.

## 2026-08-24 — Fixed const-correctness cast in lower_item for ccwld_plan_group

`lower_item` in `ccwld_ldscript.c` declares `const char *paths[128]` and
casts it to `(char **)` when calling `ccwld_plan_group`, which expects
`const char **`. The cast was corrected to `(const char **)`.

## 2026-08-24 — name_arg return type fixed in lccwld

The `name_arg` helper in `lccwld/lccwld.c` was declared `static int` but
returned `luaL_checkstring` which yields `const char *`. This caused
`-Wint-conversion` errors at every call site that passed the result to
functions expecting `const char *` (e.g. `ccwld_expr_addr`,
`ccwld_expr_loadaddr`, `ccwld_expr_sizeof`, `ccwld_expr_region_origin`,
`ccwld_expr_region_length`). The return type was changed to `const char *`.

## 2026-08-24 — IR validation strengthened: terminator, duplicate, and unreachable-block checks

The IR validator was strengthened from a permissive stub to a comprehensive
pass that enforces the §5.2/§5.3 structural invariants:

**Validator additions (`ir/ccw_ir_validate.c`):**
- **Duplicate function names** — the module-level scan rejects `@f` appearing
  in more than one function header.
- **Duplicate block names** — within a single function, `^name` must be unique.
- **Terminator requirement** — every block must end with one of `br`, `ret`,
  `br.cond`, or `switch`; a block whose last instruction is a non-terminator
  (e.g. `iadd`) is rejected.
- **Empty block rejection** — a block with zero instructions has no terminator
  and is rejected.
- **Undefined branch targets** — every block-operand in a terminator must
  resolve to a block name declared in the same function.
- **Unreachable blocks** — a non-entry block with zero predecessors (as
  computed by the live CFG successor walk) is rejected.
- **Duplicate parameter names** — `%%x` appearing twice in the same function's
  `(params ...)` list is rejected.
- **Return type consistency** — the operand type of a `ret` instruction must
  match the function's declared result type (e.g. `f64` in a function typed
  `i64` is rejected).
- Profile-cross-contamination checks are retained from the original.

**CFG analysis tightened (`ir/ccw_ir.c`):**
- `ccw_ir_block_successors` now looks only at the last instruction (the
  terminator) rather than scanning backwards for any instruction with block
  operands. This prevents non-terminator instructions with block references
  (e.g. hypothetical `phi`/`block-addr` ops) from contaminating the CFG.
- `ccw_ir_block_successor_ref` uses a dynamically allocated buffer instead of
  a fixed 16-element stack array, so `switch` instructions with many targets
  work correctly.

**New public API (`ir/ccw_ir.h`):**
- `ccw_ir_instr_is_terminator` — returns `true` for `br`, `ret`, `br.cond`,
  and `switch`, `false` for all other opcodes.
- Terminator opcode constants: `CCW_OP_BR`, `CCW_OP_BR_COND`, `CCW_OP_RET`,
  `CCW_OP_SWITCH`.

**Glue accessor added (`glue/ccw_host_accessors.c`):**
- `instr-terminator?` — Scheme-visible predicate so kernels can query whether
  an instruction is a terminator without inspecting opcode strings.

**Swaff adapters unified:**
- All five Swaff adapters (C, Lua, SML, OCaml, Delphi) had identical
  `block_terminated` static functions duplicated across files. Each was
  replaced with a one-liner that delegates to `ccw_ir_instr_is_terminator`.

**Tests (`tests/test_ir_validate.c`):**
- New test file covering all the new checks listed above, plus a smoke test
  for `ccw_ir_instr_is_terminator` and a validation round-trip (validate →
  print → parse → validate).
- `tests/test_ir_profiles.c` updated to add terminators to every block whose
  validation was previously tested without them (the old tests happened to
  pass because the validator didn't check for terminators).

## 2026-8-24 - Revamped the Glue Layer

The Glue layer now uses "bridges" to hook vendored libraries used by kernels, into the Executor layer.



## 2026-08-24 — Strengthened codegen and register allocation kernels from ISA-Bundle

The `.agents/ISA-Bundle/` directory defines three target ISAs — amd64, arm64,
and rv64 — each with register classes, aliases, flag registers, encoding
formats, and concrete operations. The existing codegen kernels were limited to
scalar-integer opcode renaming and lacked FP, width-specific load/store,
extension/truncation, and conversion operations. The register allocators were
simplistic: they assigned virtual slots/colors without awareness of physical
register counts, calling conventions, or spill logic.

### Strengthened codegen kernels (all bumped to v0.3.0)

**`kernels/codegen-x86-64.scm`** — added op-table entries from `amd64.isa`:
- FP arithmetic: `fadd`/`fadd.d`, `fsub`/`fsub.d`, `fmul`/`fmul.d`,
  `fdiv`/`fdiv.d`, `fneg`
- FP compares: `fcmp.ord`/`fcmp.ord.d`, `fcmp.uno`/`fcmp.uno.d`
- Width-specific loads: `loadb`/`loadh`/`loadw`/`load`, plus signed/unsigned
  variants (`loadsb`/`loadub`/`loadsh`/`loaduh`/`loadsw`/`loaduw`)
- Width-specific stores: `storeb`/`storeh`/`storew`/`store`/`storel`,
  plus `stores`/`stored`
- Integer extension: `extsb`/`extub`/`extsh`/`extuh`/`extsw`/`extuw`
- FP conversion: `exts`/`truncd`/`stosi`/`stosi.l`/`dtosi`/`dtosi.l`/
  `swtof`/`sltof`/`cast.fp.i`/`cast.i.fp`
- Misc: `swap`/`addr`/`sign.ext`/`copy.sign`/`udiv`/`uidiv`
- Compare flag extraction: `flagfeq`/`flagfne`/`flagflt`/`flagfle`/
  `flagfgt`/`flagfge`/`flagfa`/`flagfae`/`flagfb`/`flagfbe`

**`kernels/codegen-aarch64.scm`** — added from `arm64.isa`:
- FP arithmetic: `fadd`/`fadd.d`, `fsub`/`fsub.d`, `fmul`/`fmul.d`,
  `fdiv`/`fdiv.d`, `fneg`
- FP compare: `fcmp.ord`/`fcmp.ord.d`
- Width-specific loads: `loadb`/`loadh`/`loadw`/`load`, plus signed/unsigned
  variants
- Width-specific stores: `storeb`/`storeh`/`storew`/`store`/`storel`/
  `stores`/`stored`
- Integer extension: `extsb`/`extub`/`extsh`/`extuh`/`extsw`/`extuw`
- FP conversion: `exts`/`truncd`/`stosi`/`stoui`/`dtosi`/`dtoui`/
  `swtof`/`uwtof`/`sltof`/`ultof`/`cast.fp.i`/`cast.i.fp`
- Misc: `swap`/`udiv`/`uidiv`/`copy`
- Compare flag extraction: `flagfeq`/`flagfne`/`flagflt`/`flagfle`/
  `flagfgt`/`flagfge`/`flagflo`/`flagfls`/`flagfhi`/`flagfhs`

**`kernels/codegen-riscv64.scm`** — added from `rv64.isa`:
- FP arithmetic: `fadd`/`fadd.d`, `fsub`/`fsub.d`, `fmul`/`fmul.d`,
  `fdiv`/`fdiv.d`, `fneg`
- FP compare: `fcmp.eq`/`fcmp.eq.d`/`fcmp.lt`/`fcmp.lt.d`/
  `fcmp.le`/`fcmp.le.d`
- Width-specific loads/stores: `loadb`/`loadh`/`loadw`/`load`/
  `loadub`/`loaduh`/`loaduw`/`storeb`/`storeh`/`storew`/`store`
- Integer extension: `extsb`/`extub`/`extsh`/`extuh`/`extsw`/`extuw`
- FP conversion: `exts`/`truncd`/`stosi`/`stosi.l`/`stoui`/`stoui.l`/
  `dtosi`/`dtosi.l`/`dtoui`/`dtoui.l`/`swtof`/`uwtof`/`sltof`/`ultof`/
  `cast.fp.i`/`cast.i.fp`
- Misc: `swap`/`udiv`/`uidiv`/`urem`/`copy`
- Compare zero: `reqz`/`rnez`

**`kernels/codegen-wasm32.scm`** — added FP arithmetic, compares,
width-specific loads/stores, extension, and conversion ops.

### New register info kernels

Three new kernels publish per-ISA register class metadata derived from the
ISA-Bundle register classes, aliases, and standard calling conventions:

- **`kernels/codegen-reg-info-x86-64.scm`** (capability `codegen.reg-info.x86-64`)
  — 16 GPRs (RAX-R15, RBP, RSP), 16 FPRs (XMM0-XMM15), with caller/callee-saved
  classification per System V AMD64 ABI. Reserved: RBP, RSP. Publishes
  `stack-pointer`, `frame-pointer`, `link-register`, and per-register name→index
  mappings.
- **`kernels/codegen-reg-info-aarch64.scm`** (capability `codegen.reg-info.aarch64`)
  — 32 GPRs (X0-X30, SP), 31 FPRs (V0-V30), AAPCS64 calling convention.
  Reserved: X30(LR), SP. Link register is X30.
- **`kernels/codegen-reg-info-riscv64.scm`** (capability `codegen.reg-info.riscv64`)
  — 30 GPRs (T0-T6, A0-A7, S1-S11, SP, GP, TP, RA), 32 FPRs
  (FT0-FT11, FA0-FA7, FS0-FS11). RISC-V calling convention. Reserved:
  SP, GP, TP, RA.

### New spill and frame kernels

- **`kernels/regalloc-spill.scm`** (capability `codegen.regalloc-spill`) —
  Reads `allocator-slot` from `codegen.regalloc-linear` and
  `allocatable-gpr-count` from `codegen.reg-info.*` to determine which virtual
  registers exceed physical limits. Inserts spill-store after definitions and
  rewrites subsequent uses to reload from the spill slot.
- **`kernels/codegen-frame.scm`** (capability `codegen.frame`) —
  Target-parameterized frame layout. Accepts `target` and `frame-size` in
  the options alist. Computes stack slot count from `regalloc-linear` metadata,
  16-byte-aligns the frame, and inserts prologue (push FP, mov FP←SP, sub SP,
  save LR) and epilogue (restore LR, mov SP←FP, pop FP) instructions.
  Supports `x86-64`, `aarch64`, and `riscv64` targets.

### Interpretation

The ISA-Bundle opcodes use a different naming convention than the kernel
op-table (e.g., `add` vs. `iadd` for integer add, `add` vs. `fadd` for
FP add). The kernel op-table maps the generic Weave IR opcodes to
ISA-specific target opcodes, preserving the existing convention that
codegen kernels are pure opcode renamers. Legalization (e.g., splitting
width-specific ops, constraining immediates) remains the job of
`isel-legalize` and downstream kernels. Register info kernels publish
facts through `analysis-put!` rather than modifying IR, consistent with
the analysis-vs.-transform separation in the existing kernel design.
