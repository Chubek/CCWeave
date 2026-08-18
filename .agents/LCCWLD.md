# lccwld Specification v0.1

`lccwld` is the Lua library that CCWld exposes as the global `ccwld` module inside **Lua
linker scripts** (`*.ccwld.lua`). It is the Lua frontend to CCWld's *link plan* — the same
internal IR that the traditional `ld(1)`-style scripts (parsed by `third_party/mpc`) build.
lccwld lives at `toolchain/ccwld/lccwld/`.

This document specifies the library only: its surface, its evaluation model, its sandbox,
and its determinism guarantees. The linker phases it drives, the LTO C ABI, the plugin C
ABI, and object emission via `third_party/LIEF` belong to the CCWld spec.

## 1. Role and the two-frontend rule

CCWld accepts a link description in one of two languages:

- **ld-script** (`*.ld`, `*.x`), parsed by mpc into the link plan.
- **Lua** (`*.ccwld.lua`), executed with the `ccwld` module bound via `third_party/sol2`,
  building the *same* link plan through API calls.

**D-0034 (the bridge rule):** both frontends target one immutable link-plan IR. Every
`ccwld.*` builder call has an ld-script counterpart and vice versa; neither frontend can
express a plan the other cannot. lccwld is a builder for that IR, not a superset language.
This is the layout-side analogue of the ccwas grammar-core rule — one semantic target, two
surfaces — and it keeps `SECTIONS { ... }` and `ccwld.sections{ ... }` interchangeable.

**Non-goal:** lccwld is *not* a template processor. It emits no text and has no parse pass
of its own (contrast lccwas §1–§2). It is not code generation; it is link configuration.

## 2. Loading and lifetime

- The `ccwld` module is injected **only** when CCWld executes a Lua linker script. It is
  not a standalone library; `require "ccwld"` outside a linker-script context is an error.
  A Lua script named on `-T`/`--script` with a `.lua` extension is a linker script; a
  `.ld`/`.x` file goes to the mpc frontend. Extension is authoritative — no sniffing.
- **One script owns the link.** Exactly one top-level linker script drives a link (ld
  semantics). Additional scripts enter only via `ccwld.include` (§4.11) or `-T` composition
  rules defined in the CCWld spec.
- The script's **top-level execution is phase 0**: it declares the plan (output kind,
  memory, sections, symbols, input control, LTO/plugin configuration). Declarations are
  order-sensitive exactly as ld statements are.
- Anything that needs *resolved* link state (final symbol values, section sizes, addresses,
  the location counter's concrete value) is unavailable at phase 0. It is reached one of two
  ways:
  - **Deferred expression objects** (§4.6) — lazy ASTs the linker evaluates during layout.
  - **Phase hooks** (§4.9) — callbacks the linker invokes at named later phases with
    introspection access (§4.10).

This split is the central model: **declare eagerly, compute lazily.** A symbol assigned
`ccwld.dot() + 0x1000` captures an expression; it is not evaluated when the script runs.

## 3. Environment and defines

- `ccwld.env` — read-only table of `-D key=value` and `--defsym name=expr` bindings, same
  provenance role as `ccwas.env` (lccwas §3.6). Values are strings; `--defsym` symbols are
  additionally visible as defined absolute symbols in the plan.
- `ccwld.target` — read-only descriptor: `{ arch, format, endianness, ptr_width }` derived
  from `--target`/`-m`. Lets one script branch on ELF vs PE vs Mach-O without string
  sniffing.
- `ccwld.builtin` — read-only feature/limits table (max include depth, max hook depth,
  library version, LIEF version, whether `--unsafe-lua` is in effect).

## 4. Module surface

All builders return the plan node they created (or a handle to it) so scripts can compose
without rebuilding by name.

### 4.1 Output
```lua
ccwld.output{
  kind    = "exe" | "dso" | "reloc" | "pie",   -- executable / shared / relocatable / PIE
  format  = "elf" | "pe" | "macho",             -- default from --target
  entry   = "start" | ccwld.symbol"start",
  soname  = "libfoo.so.1",   -- dso only
  osabi   = ...,             -- format-specific, validated against the format table
}
```
Format-inappropriate fields (`soname` on PE) are fatal, not ignored — silent dropping is
how mismatched outputs reach the cache (mirrors ccwas §3's no-silent-host rule).

### 4.2 Input control

- `ccwld.input(path...)` — add objects/archives, respecting search paths.
- `ccwld.group{ path... }` — archive group with repeated-scan resolution (ld `GROUP`).
- `ccwld.as_needed(fn)` — DSOs referenced inside `fn` are `--as-needed`.
- `ccwld.search_path(dir...)` — append to the library search path (ld `SEARCH_DIR`).
- `ccwld.startup(path)` — object forced first in output order.

Ordering matters and is preserved; lccwld records definition order, never a hashed set.

### 4.3 Memory regions

```lua
ccwld.memory{
  { name="rom", attrs="rx",  origin=0x08000000, length=256*1024 },
  { name="ram", attrs="rwx", origin=0x20000000, length=64*1024  },
}
```

Regions are consumed by output sections' `region`/`at_region` (ld `>region`/`AT>region`).

### 4.4 Sections

```lua
ccwld.sections{
  ccwld.out(".text", {
region = "rom",
align  = 16,
input  = { ccwld.match("*", ".text .text.*"),  -- glob over input file + section
ccwld.keep("*", ".init_array") },   -- keep = GC root (§ CCWld GC)
-- symbols defined relative to this section:
ccwld.provide("__text_end", ccwld.dot()),
  }),
  ccwld.out(".data", {
region    = "ram",
at_region = "rom",                             -- load addr in rom, run addr in ram
input     = { ccwld.match("*", ".data .data.*") },
  }),
}
```
- `ccwld.out(name, spec)` — one output section.
- `ccwld.match(file_glob, section_globs)` — input-section selector; order within a section
  list is the placement order.
- `ccwld.keep(...)` — like `match` but marks matched sections as GC roots.
- `ccwld.fill(byteval)`, `align`, `subalign` — as ld.
- Overlays and `NOLOAD` map to `ccwld.overlay{...}` and `spec.load=false`.

### 4.5 Segments / program headers

`ccwld.phdrs{ ... }` (ELF) and `ccwld.segments{ ... }` (Mach-O) declare load segments;
output sections reference them by `phdr=`/`segment=`. On PE this maps to section
characteristics; requesting phdrs on a PE link is fatal per §4.1's discipline.

### 4.6 Expression objects and the location counter

Layout-dependent values are **deferred expression objects**. Any arithmetic on them builds
an AST (via Lua metamethods) that the linker evaluates during layout, in plan order:

- `ccwld.dot()` — the location counter `.`. Only meaningful inside a `sections`/`out`
  context; captured lazily.
- `ccwld.align(expr, n)`, `ccwld.addr(sec)`, `ccwld.loadaddr(sec)`, `ccwld.sizeof(sec)`,
  `ccwld.sizeof_headers()`, `ccwld.origin(region)`, `ccwld.length(region)`,
  `ccwld.max(a,b)`, `ccwld.min(a,b)`, `ccwld.abs(a)`, `ccwld.segment_start(...)`.
- Numbers auto-lift into expression objects; `expr + 0x1000`, `expr & ~0xf`, etc. all work.
- `ccwld.defined(sym)` returns a deferred boolean usable in deferred `ccwld.cond(c,a,b)`.

Reading a deferred object's concrete value at phase 0 is fatal ("value not available until
layout") — it forces authors toward the lazy model instead of accidentally freezing a
zero.

### 4.7 Symbols

- `ccwld.symbol(name)` — a symbol reference (defined or external), usable in expressions.
- `ccwld.assign(name, expr)` — define/override a symbol (ld `name = expr`). Redefinition of
  a symbol already defined by an input object is fatal unless `ccwld.provide` is used.
- `ccwld.provide(name, expr)` — define only if referenced and not otherwise defined (ld
  `PROVIDE`); `ccwld.provide_hidden` for the hidden-visibility variant.
- Visibility/binding setters: `ccwld.hidden(name)`, `ccwld.weaken(name)`,
  `ccwld.alias(new, old)`.

### 4.8 Version scripts (ELF/DSO)

`ccwld.version{ ... }` builds symbol version nodes (globals/locals, `extern "C++"` demangle
matching) equivalent to a `--version-script`. On non-ELF formats it is fatal.

### 4.9 Phase hooks

lua
ccwld.on("resolved", function(link) ... end)  -- after symbol resolution
ccwld.on("gc",       function(link) ... end)  -- after section GC, before layout
ccwld.on("layout",   function(link) ... end)  -- during layout; live location counter
ccwld.on("emit",     function(link) ... end)  -- before object is written

- Hooks run in registration order at their phase. Hook depth (hooks registering hooks) is
  capped (`ccwld.builtin.max_hook_depth`, default 8).
- The `link` argument is the introspection handle (§4.10). Its mutability is
  phase-scoped: symbol values may be assigned in `resolved`/`layout`; sections may be
  reordered only in `gc`; `emit` is read-only except for note/metadata insertion.
- Hooks are the imperative escape hatch. Deferred expressions (§4.6) are the declarative
  path and should be preferred; a hook that only computes a symbol value is a code smell an
  expression object would express more clearly.

### 4.10 Introspection (hook-time only)

The `link` handle exposes ordered, read-mostly views:

- `link.objects` — input objects in link order: `{ path, kind, format, symbols, sections }`.
- `link.sections` — output sections with `addr`, `size`, `align`, member input sections.
- `link.symbols` — resolved symbols: `{ name, value, defined_in, binding, visibility }`.
- `link.undefined()` — still-undefined symbols (for diagnostics or lazy stubbing).
- `link.reloc_stats()` — per-type relocation counts (read-only, for diagnostics).

All iteration order is deterministic (link order, then first-occurrence) — §6. These views
do **not** exist at phase 0; touching `link` outside a hook is fatal.

### 4.11 Includes, gensym, diagnostics

- `ccwld.include(path)` — include another Lua linker script into the current plan;
  cycle-checked, depth cap (default 32). ld-script includes use the mpc `INCLUDE`
  directive; the two include worlds do not cross (a `.lua` script cannot `ccwld.include` a
  `.ld` file — that composition, if wanted, goes through `-T` at the CCWld level).
- `ccwld.gensym(prefix)` — deterministic unique symbol name (shared counter regime with
  `ccwas.gensym`); for generated trampolines/aliases.
- `ccwld.error(msg)` / `ccwld.warn(msg)` — emit into CCWld's diagnostic stream with full
  provenance (§7). `ccwld.assert(cond, msg)` is sugar over `error`.

### 4.12 LTO and plugin configuration surface

lccwld exposes only *configuration and registration*; the executable interfaces are the C
headers (`ccwld-lto.h`, `ccwld-plugin.h`) specified in the CCWld doc.

- `ccwld.lto{ enable=true, jobs=N, cache_dir=..., pipeline="..." }` — configures the native
  LTO path. The actual recompilation ABI is `ccwld-lto.h`; lccwld cannot supply codegen, it
  can only turn the path on and parameterize it.
- `ccwld.plugin(path, opts_table)` — load a `ccwld-plugin.h` plugin and pass it options.
  `ccwld.plugin_opt(name, value)` sets a single option. Plugins run in C at the phases
  their ABI declares; Lua neither implements nor reorders them. If a plugin and a Lua hook
  both claim ownership of the same phase artifact, that conflict is resolved (and reported)
  by CCWld, not by lccwld.

**D-0035:** lccwld configures but never implements LTO or plugin logic. Codegen and plugin
callbacks live behind the C ABIs; the Lua surface is declarative registration only. This
keeps the deterministic-Lua sandbox (§5) intact — no native code enters through Lua.

## 5. Sandbox

Same regime as lccwas §4:

- Default environment excludes `io`, `os` (except a frozen `os.getenv` limited to
  documented `CCWLD_*` keys), `package`, `require`, `load`/`loadstring`, `debug`, and raw
  FFI. Available: `math` (minus `random`), `string`, `table`, `pairs/ipairs` over ordered
  proxies (§6), and the `ccwld` module.
- File access is only through `ccwld.include`, `ccwld.input`, and search paths — all
  routed through CCWld's provenance-tracked, cycle-checked file layer, never bare Lua io.
- `--unsafe-lua` unlocks `os`/`io`/FFI for local experimentation and is **forbidden in
  CI**, identical to lccwas §4. A plan built under `--unsafe-lua` is marked non-reproducible
  in the `.note.ccw` producer note.

## 6. Determinism

lccwld is a deterministic plan builder. Same (scripts, inputs, `-D`/`--defsym` set, target,
lccwld version) ⇒ byte-identical link plan ⇒ (with a deterministic linker back end)
byte-identical output.

- All plan collections preserve insertion/first-occurrence order; `pairs` over lccwld
  tables iterates a stable order, never Lua hash order.
- `math.random`, wall-clock, and address-of-Lua-object leakage are removed or forbidden.
- `ccwld.gensym` is counter-based and reset per link.
- Deferred expressions evaluate in plan order with a defined location-counter progression;
  no evaluation-order ambiguity.
- CI double-runs the script and diffs the serialized link plan (extending the ecosystem's
  double-assembly gate to the link stage). This gate runs *before* LIEF emission so a
  plan-level nondeterminism is caught even if the back end would have masked it.

## 7. Diagnostics and provenance

Every diagnostic carries the chain: output artifact position ← plan node ← the `ccwld.*`
call site (Lua file:line, via sol2's traceback) ← `ccwld.include` stack ← originating
`-T` argument. `ccwld.error`/`warn`, mpc-frontend errors, and back-end (LIEF) errors all
land in one stream in CCWld's standard diagnostic shape. Deferred-expression evaluation
failures report both the definition site (where the expression was built) and the layout
point (where it failed to resolve). Exit-code mapping is defined by CCWld.

## 8. Layout in tree and testing

```
toolchain/ccwld/lccwld/
  bind/          # sol2 bindings from ccwld.* to the link-plan IR
  expr/          # deferred expression objects + metamethods
  sandbox/       # environment construction, --unsafe-lua gating
  order/         # ordered-table proxies for deterministic iteration
  tests/
parity/      # every ccwld.* builder vs its ld-script counterpart -> identical IR (D-0034)
expr/        # deferred expression evaluation, dot() progression, cond/defined
hooks/       # phase hooks, ordering, depth cap, phase-scoped mutability
introspect/  # link handle views, deterministic ordering
sandbox/     # denied globals, include cycles/depth, unsafe-lua marking
determinism/ # double-run plan byte-diff harness
negative/    # every fatal path in this spec (phase-0 deferred read, format mismatch, …)
```
The `parity/` suite is the enforcement mechanism for D-0034: each test writes the same link
two ways and asserts IR equality, so a builder added to one frontend without the other
fails CI. All CI-gated, ASan-clean.

## 9. Decisions to append to `DECISIONS.md`

- **D-0034**: lccwld and the ld-script frontend build one immutable link-plan IR; the two
  surfaces are expressively equivalent and CI-checked for parity. lccwld is a plan builder,
  not a superset language, and (unlike lccwas) emits no text.
- **D-0035**: lccwld configures LTO and plugins but never implements them; executable logic
  lives behind `ccwld-lto.h` / `ccwld-plugin.h`, keeping the deterministic Lua sandbox free
  of native entry points.
- **D-0036**: Linker-script language is chosen by file extension (`.lua` → lccwld, `.ld`/
  `.x` → mpc), authoritatively, with no content sniffing.
- **D-0037**: lccwld uses the declare-eagerly / compute-lazily model — deferred expression
  objects for layout-dependent values and phase hooks for imperative introspection; reading
  a deferred value at phase 0 is fatal.
- **D-0038**: lccwld inherits the ecosystem sandbox and determinism regime (lccwas §4–§5);
  `--unsafe-lua` is CI-forbidden and marks output non-reproducible in `.note.ccw`.
`

