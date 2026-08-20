# DEPHIA.md — Specification for `compilers/dephia`

Status: PROPOSAL
Component: `compilers/dephia/` — a Delphi (Object Pascal) compiler on CCWeave
Decisions: D-0039–D-0045
Grounding: `Capabilities.yaml` (lines 31–43, 79–84, 150–152),
`Kernel-(2).yaml` (lines 60–66, 448–468), `Rewrite-salvo.yaml` (218-line
revision, no OOP-keyed rules)

---

## 1. Scope and manifest ground truth

Dephia compiles Delphi-dialect Object Pascal to native code through the
CCWeave pipeline, terminating in `ccwas`/`ccwld`. Before the design, three
facts established by manifest inspection constrain it:

1. **Kliche's `oop` stereotype is not a manifest entity.**
   `Kernel.yaml` does not contain a `stereotype:` key. What
   exists is the four-kernel OOP family; Dephia is the occasion to formalize
   the stereotype (§3).
2. **There is no `frontend.*` capability tier** in `Capabilities.yaml`, and no
   reference to Delphi, Pascal, or `swaff/adapters` anywhere. The frontend
   adapter and its capabilities are new work (§2, §6).
3. **`Rewrite-salvo.yaml` has no rules keyed on object/dispatch facts**, so the
   OOP kernels currently run without rewrite consumers; Dephia must ship its
   consumers or the optimization tier is dead weight (§5).

## 2. Frontend: `swaff/adapters/ccw_swaff_delphi.c`

Following the Moonix precedent (`swaff/adapters/ccw_swaff_lua.c`), parsing is
delegated to a Swaff adapter rather than a bespoke parser.

- **D-0039**: The adapter is a single C11 translation unit exposing the
  standard Swaff adapter entry points, producing the CCWeave frontend AST.
  Dialect gates (`{$MODE DELPHI}`-equivalent, unit syntax, generics, class
  helpers, anonymous methods) are compile-time flags in the adapter, recorded
  in `.note.ccw` provenance so two builds with different dialect gates can
  never be conflated.
- Case-insensitive identifier resolution is performed in the adapter with a
  single canonical folding (ASCII lower), and the *original* spelling is kept
  as metadata — required for RTTI and `published` name fidelity.
- The unit system (interface/implementation sections, `uses` cycles via
  implementation-only imports, initialization/finalization ordering) is
  resolved by the adapter into an explicit, sorted unit-order fact; the
  deterministic initialization order is part of the compiled artifact, not a
  link-time accident.

## 3. Kliche `oop` stereotype — formalization

- **D-0040**: Add a `stereotypes:` section to the kernel-manifest schema. A
  stereotype is a named, versioned bundle of kernels with a declared partial
  order. The first instance:
```yaml
stereotypes:
  oop:
version: 0.1.0
kernels:
- oop-vtable-lower       # lower.oop-vtable        (Kernel-(2).yaml 455–461)
- oop-devirtualize       # opt.oop-devirtualization (448–454)
- oop-null-check         # opt.oop-null-check-elimination (462–468)
- exception-lower        # lower.exceptions         (60–66)
order:
- [oop-devirtualize, oop-vtable-lower]   # devirtualize before layout is fixed
- [oop-vtable-lower, oop-null-check]
```
A consumer (Dephia) requests the stereotype; Sched expands it to the kernel
set and enforces the order. Kernels remain individually requestable — the
stereotype is composition, not encapsulation.

## 4. Lowering Delphi semantics onto the stereotype

| Delphi construct | Capability | Notes |
|---|---|---|
| Class VMT, `virtual`/`override` | `lower.oop-vtable` | VMT laid out at Delphi's fixed negative offsets (see D-0041) |
| `is` / `as`, type-feedback sites | `opt.oop-devirtualization` | `as` lowers to checked cast + devirt opportunity fact |
| Implicit `Self` nil deref, `Assigned` chains | `opt.oop-null-check-elimination` | |
| `try..except`, `try..finally`, `raise` | `lower.exceptions` | `finally` duplicated/funclet per target ABI, chosen deterministically per triple |

- **D-0041**: Dephia targets Delphi ABI *shape*, not binary compatibility:
  VMT with negative-offset system slots (InstanceSize, ClassName, parent
  pointer) is preserved as layout convention because RTTI and `TObject`
  intrinsics assume it, but no compatibility with Embarcadero-compiled units
  is claimed. `register`-convention (eax/edx/ecx legacy) is **not**
  implemented; calling conventions are the platform C ABI plus a documented
  `Self`-first rule.
- **D-0042**: Constructs with no manifest support are desugared in the
  frontend, not the kernel tier: properties become accessor calls, `dynamic`
  methods are compiled as `virtual` (message-table dispatch declined, unit
  emits a diagnostic), class helpers resolve statically. Interfaces
  (`IInterface` refcounting, method thunks with `Self` adjustment) cannot be
  fully desugared and require a new `lower.oop-interface` capability + kernel
  (§6).

## 5. `Rewrite-salvo.yaml` consumers

- **D-0043**: Ship, in the same change as the compiler: a devirt-apply rule
  keyed on `opt.oop-devirtualization` facts (rewrites indirect VMT call to
  direct call + guard), a null-check-elide rule keyed on the null-check
  facts, and an exception-normalize rule that folds empty `finally` regions.
  Rule keys use the sorted fact forms; the CI double-run diff gate extends
  over their output.

## 6. Required manifest additions

New capabilities (`Capabilities.yaml`):

- `frontend.delphi` — the Swaff adapter's provided capability; first entry of
  the new `frontend.*` tier.
- `lower.oop-interface` — interface thunk/refcount lowering
  (kernel `oop-interface-lower`, new).
- `lower.oop-rtti` — RTTI table emission for `published`/`$M+` classes
  (kernel `oop-rtti-emit`, new); tables are emitted as sorted, canonical
  byte sequences.

Codegen consumes the existing tier unchanged: `codegen.isel-*`,
`codegen.regalloc-ssa` (default) / `codegen.regalloc-linear` (fallback),
`codegen.x86_64` and `codegen.aarch64` (`Capabilities.yaml` 31–43), then
`ccwas` textual assembly and `ccwld` link plans per D-0025–D-0038.

## 7. Determinism

- **D-0044**: Standard gates apply: byte-for-byte reproducibility CI,
  sorted emission of all facts (unit order, VMT layouts, RTTI tables,
  devirt sites), no host-dependent iteration anywhere in the adapter.
- **D-0045**: `.note.ccw` records the adapter version, dialect-gate vector,
  stereotype version, and manifest hashes, alongside the existing toolchain
  provenance.

## 8. Rollout order

1. Manifest schema: `stereotypes:` section + `oop` bundle (D-0040).
2. `ccw_swaff_delphi.c` adapter + `frontend.delphi`, minimal dialect
   (units, classes, virtual dispatch, exceptions).
3. `Rewrite-salvo.yaml` consumers (D-0043) with CI gates.
4. `oop-interface-lower` and `oop-rtti-emit` kernels.
5. Dialect expansion (generics, anonymous methods) behind adapter gates.
`

