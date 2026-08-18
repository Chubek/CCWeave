# CCWld Specification v0.1

CCWld (`ccwld`) is the CCWeave linker. It consumes a **link description** in one of two
surfaces — ld-script (via `third_party/mpc`) or Lua (via `lccwld` + `third_party/sol2`) —
lowers both to a single immutable **link-plan IR**, runs a fixed phase pipeline over it, and
emits an object via `third_party/LIEF`. It lives at `toolchain/ccwld/`.

This document specifies the linker: the plan IR, the phase pipeline, the LTO and plugin C
ABIs, emission, caching, and determinism. The Lua surface is specified separately in the
lccwld doc (D-0034–D-0038); this doc is the consumer of that contract.

## 1. Position in the toolchain

```
                 .ld/.x ──▶ mpc frontend ─┐
                                          ├─▶  link-plan IR  ─▶ [resolve▸gc▸layout▸emit]  ─▶ LIEF ─▶ object
   *.ccwld.lua ──▶ lccwld (sol2) frontend ─┘         ▲                    │
                                                     └──── LTO C ABI ─────┤
                                                     └──── plugin C ABI ──┘
```
- **Input**: object files and archives from `ccwas`/external toolchains, plus a link
  description. CCWld does not assemble; `ccwas` (D-0025–D-0033) does.
- **Output**: `exe` / `dso` / `reloc` / `pie` in `elf` / `pe` / `macho`, written by LIEF.
- CCWld is the *conflict authority*: where a Lua hook and a plugin both claim a phase
  artifact (lccwld §4.12), CCWld resolves and reports it.

## 2. The link-plan IR

**D-0039 (single IR, two frontends, deferred-eager symmetry):** both frontends lower to one
immutable link-plan IR. The IR is the semantic target named by lccwld D-0034; this doc
makes it normative. Critically, **the mpc frontend does not evaluate expressions eagerly** —
it builds the same deferred expression nodes (§2.3) that lccwld builds. Eager evaluation at
parse time is removed so the two surfaces are provably equivalent and the parity suite
(lccwld §8) can compare serialized IR byte-for-byte.

### 2.1 Structure

The plan IR is a sealed, serializable tree (echoing `Sched`'s seal/serialize model,
D-0017): once both frontends and all includes have run, `Plan::seal()` freezes it. Post-seal
mutation is only via **phase hooks** (§3) operating through the introspection handle, and
only within each phase's mutability scope (lccwld §4.9).

Top-level nodes:

- `output` — kind, format, entry, soname, osabi (validated against the format table).
- `memory[]` — regions `{ name, attrs, origin, length }`.
- `inputs[]` — ordered objects/archives/groups with `as_needed`/`startup` flags.
- `sections[]` — output sections, each with input selectors (`match`/`keep`), align,
  fill, region/at_region, phdr/segment binding, and inline symbol assignments.
- `phdrs[]` / `segments[]` — load segments.
- `symbols[]` — assigns, provides, aliases, visibility/binding overrides (all values are
  expression nodes).
- `version[]` — ELF symbol version nodes.
- `lto` — configuration record (§4).
- `plugins[]` — ordered plugin registrations with options (§5).
- `hooks[]` — phase-keyed callback registrations (Lua frontend only; ld-script has no
  hooks, which is a deliberate expressiveness gap noted in §2.4).

### 2.2 Immutability and serialization

- The sealed plan serializes to a canonical, deterministic form (stable key order, ordered
  collections — lccwld §6). This serialization is the artifact the CI double-run diffs.
- Serialized plans are content-addressable and feed the cache key (§7).

### 2.3 Deferred expression nodes

Expression-valued fields (symbol values, section addresses, fills, `at` load addresses)
hold **expression ASTs**, never pre-computed integers:

- Leaves: integer literals, symbol refs, region origin/length, `dot()`, `sizeof`,
  `addr`/`loadaddr`, `sizeof_headers`, `segment_start`.
- Nodes: arithmetic, bitwise, `align`, `max`/`min`/`abs`, `defined`, `cond`.
- Evaluated during **layout** in plan order against a live location counter. Evaluation is
  single-pass with a defined progression; a cyclic dependency (symbol A needs B needs A) is
  fatal with both definition sites reported.

Both frontends build identical AST node types. This is what makes D-0039's parity claim
real rather than aspirational.

### 2.4 Frontend expressiveness gap (acknowledged)

ld-script has no analogue to lccwld phase hooks (§4.9). D-0034 claims full parity; hooks are
the one exception. Resolution: **hooks are not part of the parity contract.** The parity
suite compares the *declarative* plan (output/memory/sections/symbols/phdrs/version/inputs);
hooks are an additive Lua-only capability that operates on the introspection handle, not a
plan-expressible construct. This is stated so no one reads D-0034 as promising ld-script
hooks.

## 3. Phase pipeline

CCWld runs a fixed, ordered pipeline over the sealed plan. Phases and the hook points that
straddle them:


  load ▸ [merge inputs] ▸ RESOLVE ▸ on("resolved") ▸ [LTO recompile] ▸ GC ▸ on("gc")
        ▸ LAYOUT ▸ on("layout") ▸ [relocate] ▸ on("emit") ▸ EMIT(LIEF)

**D-0040 (fixed pipeline, phase-scoped mutability):** the phase order is fixed and not
user-reorderable from either frontend. Hooks and plugins attach *at* phases; they never
reorder phases. Each phase defines what the introspection handle may mutate (lccwld §4.10):
`resolved`/`layout` may assign symbol values; `gc` may reorder/keep sections; `emit` is
read-only except metadata/note insertion.

- **load**: LIEF parses inputs; symbol/section tables built. Input order preserved.
- **resolve**: symbol resolution across objects/archives (repeated-scan for groups),
  `--as-needed` DSO pruning, undefined-symbol determination. `--gc-sections` roots are the
  `keep` set plus entry plus dynamic-referenced symbols.
- **LTO recompile** (if configured, §4): bitcode/IR members handed to the LTO backend via
  the C ABI; results re-enter as native objects and re-resolve. LTO runs *after* first
  resolve so the backend sees the full symbol picture.
- **gc**: dead-section elimination from roots.
- **layout**: sections placed into regions/segments; location counter advances; deferred
  expressions (§2.3) evaluated in order; addresses and sizes become concrete.
- **relocate**: relocations applied against resolved addresses; per-type stats recorded.
- **emit**: LIEF writes the output object; `.note.ccw` producer note attached (§8).

## 4. LTO C ABI (`ccwld-lto.h`)

**D-0041 (LTO is a native backend behind a C ABI; Lua only configures it):** consistent with
lccwld D-0035, no LTO codegen enters through Lua. `ccwld.lto{...}` and the ld-script
equivalent populate the `lto` configuration record only.

The ABI is a stable C interface CCWld dlopens (or links) the backend through:

```c
// ccwld-lto.h  (ABI-versioned; CCWLD_LTO_ABI_VERSION)
typedef struct ccwld_lto_ctx ccwld_lto_ctx;

typedef struct {
  uint32_t abi_version;         // must equal CCWLD_LTO_ABI_VERSION
  const char *pipeline;         // opaque backend pipeline string from config
  unsigned    jobs;             // parallelism hint
  const char *cache_dir;        // may be NULL
} ccwld_lto_config;

// backend entry points
ccwld_lto_ctx *ccwld_lto_begin(const ccwld_lto_config*);
int  ccwld_lto_add_module(ccwld_lto_ctx*, const void *buf, size_t len,
                          const char *name);              // one IR/bitcode member
int  ccwld_lto_run(ccwld_lto_ctx*,
                   void (*emit)(void *user, const void *obj, size_t len,
                                const char *name),
                   void *user);                           // callback yields native objects
void ccwld_lto_end(ccwld_lto_ctx*);
const char *ccwld_lto_last_error(ccwld_lto_ctx*);         // NUL-terminated, backend-owned
```
- The backend receives IR members, produces native objects via the `emit` callback, and
  those objects re-enter the pipeline before **gc**.
- ABI-version mismatch is fatal at `begin`. Backend errors surface through
  `ccwld_lto_last_error` into CCWld's diagnostic stream (§9).
- Determinism: a backend that is non-deterministic (parallel codegen ordering) must be run
  with `jobs=1` under the reproducible-build flag, or its output is marked non-reproducible
  in `.note.ccw`.

## 5. Plugin C ABI (`ccwld-plugin.h`)

**D-0042 (plugins are native, phase-scoped, and CCWld-scheduled):** plugins run in C at the
phases their manifest declares. Lua neither implements nor reorders them (lccwld §4.12).

```c
// ccwld-plugin.h  (ABI-versioned; CCWLD_PLUGIN_ABI_VERSION)
typedef enum {
  CCWLD_PHASE_RESOLVED = 1,
  CCWLD_PHASE_GC       = 2,
  CCWLD_PHASE_LAYOUT   = 3,
  CCWLD_PHASE_EMIT     = 4,
} ccwld_phase;

typedef struct ccwld_link ccwld_link;   // opaque introspection/mutation handle

typedef struct {
  uint32_t abi_version;
  const char *name;
  uint32_t phases;               // OR of (1u << (ccwld_phase-1)) it wants
  int  (*init)(void *self, const char *opts_json);
  int  (*run)(void *self, ccwld_phase, ccwld_link*);
  void (*fini)(void *self);
  void *self;
} ccwld_plugin_vtable;

// symbol the plugin must export
const ccwld_plugin_vtable *ccwld_plugin_entry(void);
```
- The `ccwld_link` handle exposes the same read-mostly views the Lua introspection handle
  wraps (lccwld §4.10), with C accessors, under identical phase-scoped mutability rules
  (§3, D-0040).
- Options from `ccwld.plugin(path, opts)` / `--plugin-opt` arrive as a JSON string to
  `init` — one option-passing convention for both frontends.
- **Plugin/hook conflict:** if a plugin and a Lua hook both mutate the same artifact at the
  same phase, CCWld applies a deterministic order (plugins in registration order, then
  hooks in registration order) and emits a conflict *diagnostic* so the collision is never
  silent. This is the authority lccwld §4.12 defers to CCWld.
- `--unsafe-lua` does not gate plugins (they are always native); but a link that loads a
  plugin CCWld cannot verify as deterministic is marked accordingly in `.note.ccw`.

## 6. Object emission (LIEF)

- Emission is LIEF-driven; CCWld builds the LIEF object model from the laid-out plan and
  serializes. Format-specific structures (ELF phdrs, PE section characteristics, Mach-O
  load commands) are populated from the plan's segment/phdr nodes.
- Format-inappropriate plan nodes are rejected earlier (frontend §4.1 discipline), so
  emission never has to silently drop.
- **D-0043 (no-passthrough at the link stage):** mirroring ccwas D-0033, CCWld never copies
  an input construct it does not understand into the output. Unrecognized input sections
  with no matching selector are either explicitly placed by a catch-all rule, discarded
  under `--gc-sections`, or a fatal "unplaced section" diagnostic — never blindly appended.

## 7. Caching and determinism

- **Cache key**: content hash of `{ serialized sealed plan, input object hashes, target,
  LTO config + backend ABI version, plugin identities + ABI versions, CCWld version }`.
- **Reproducibility gate**: CI double-runs the whole link and byte-diffs the output object
  *and* the serialized plan (extending lccwld §6's plan-diff to the emitted artifact). The
  plan diff runs before emission so plan-level nondeterminism is caught even when LIEF would
  mask it.
- Sources of nondeterminism are removed by construction: ordered collections everywhere,
  no wall-clock in notes (SOURCE_DATE_EPOCH honored), counter-based gensym shared regime
  with ccwas/lccwld, deterministic tie-breaking (link order then first-occurrence) in
  resolve/gc/layout.
- Any of `--unsafe-lua`, a non-deterministic LTO backend at `jobs>1`, or an unverifiable
  plugin flips the output's `.note.ccw` reproducibility flag to false.

## 8. Producer note (`.note.ccw`)

Every output carries a `.note.ccw` (ELF note / equivalent PE-Mach-O metadata) recording:
CCWld version, both-frontend provenance summary, plan content hash, LTO backend + ABI
version (if used), plugin identities + ABI versions, and the reproducibility flag. This is
the audit anchor the cache and CI gates read.

## 9. Diagnostics

One diagnostic stream, one shape, shared with the frontends (lccwld §7):


output-position ◂ plan-node ◂ frontend-site ◂ include/-T stack

- lccwld errors carry Lua file:line (sol2 traceback); mpc errors carry ld-script file:line;
  LTO/plugin errors carry backend/plugin name + their `last_error`/return; LIEF emission
  errors carry the offending plan node.
- Deferred-expression failures report definition site *and* layout point (lccwld §7).
- Exit-code mapping is defined here and consumed by both frontends: `0` ok, `1` link error
  (unresolved/unplaced/relocation), `2` usage/config error, `3` plugin/LTO ABI error, `4`
  internal.

## 10. Layout in tree and testing

```
toolchain/ccwld/
  frontend/
    ldscript/     # mpc grammar + lowering to plan IR (deferred nodes, D-0039)
    lua/          # (lccwld lives in lccwld/, this is the CCWld-side glue)
  plan/           # link-plan IR, seal, canonical serialization
  expr/           # deferred expression evaluation engine (shared node types)
  phases/         # load, resolve, lto, gc, layout, relocate, emit
  abi/
    ccwld-lto.h
    ccwld-plugin.h
  emit/           # LIEF object-model construction
  cache/          # content-addressable plan/output cache
  tests/
    parity/       # ld-script vs lccwld -> identical serialized declarative plan (D-0039)
    phases/       # phase ordering, phase-scoped mutability (D-0040)
    expr/         # deferred evaluation, dot() progression, cycle detection
    lto/          # ABI conformance, version mismatch, determinism gating (D-0041)
    plugin/       # ABI conformance, phase scheduling, hook/plugin conflict order (D-0042)
    emit/         # LIEF output per format, no-passthrough fatal paths (D-0043)
    determinism/  # double-link byte-diff of plan AND object
    negative/     # every fatal path in this spec
```
- `parity/` is shared with lccwld §8 and is the enforcement of D-0034/D-0039.
- All CI-gated, ASan-clean; LTO/plugin tests use in-tree reference backends/plugins built
  against the shipped ABI headers.

## 11. Decisions to append to `DECISIONS.md`

- **D-0039**: Both frontends lower to one immutable, serializable link-plan IR; the mpc
  frontend builds deferred expression nodes (no eager evaluation) so the two surfaces are
  provably parity-checkable. IR seals after all frontends/includes; post-seal mutation only
  via phase hooks within phase scope.
- **D-0040**: The phase pipeline (load▸resolve▸[lto]▸gc▸layout▸relocate▸emit) is fixed and
  not user-reorderable; hooks and plugins attach at phases under phase-scoped mutability and
  never reorder phases.
- **D-0041**: LTO is a native backend behind `ccwld-lto.h`; the Lua/ld-script surfaces only
  configure it. ABI-versioned; non-deterministic backends must run `jobs=1` for reproducible
  builds or the output is marked non-reproducible.
- **D-0042**: Plugins are native, ABI-versioned (`ccwld-plugin.h`), phase-scoped, and
  CCWld-scheduled; options pass as JSON from both frontends; plugin/hook conflicts get a
  deterministic order plus a mandatory diagnostic (CCWld is the conflict authority
  lccwld §4.12 defers to).
- **D-0043**: No-passthrough at the link stage — CCWld never blindly copies unrecognized
  input constructs into the output; an unplaced section is explicitly placed, GC'd, or
  fatal (link-stage analogue of ccwas D-0033).
`

