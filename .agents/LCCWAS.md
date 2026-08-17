# lccwas Specification v0.1

`lccwas` is the C library that embeds Lua 5.5 (`third_party/lua`) into the `ccwas` assembler
and exposes the `ccwas` module to template code. It lives at `toolchain/ccwas/lccwas/` and is
the *only* bridge between Lua and the assembler: template code never sees mpc, ISA tables, or
encoder state except through this module.

> **Naming note.** `lccwas` is the library (`lccwas.c`, `lccwas.h`, following the `lpeg`/`lfs`.
> convention); `lccwas` is the module table it registers inside template states. Existing docs
> that say "the `lccwas.emit` function" refer to this module.

> **Version note.** Sched scripts and Moonix pin Lua 5.4 (D-0016). `lccwas` pins **Lua 5.5**
> deliberately: `ccwas` is a standalone build tool with no runtime coupling to Sched plans or
> the Moonix VM, so the two version tracks never share a `lua_State`. This asymmetry is a
> decision, not drift (D-0025).

## 1. Execution model: template pass before parse pass

`ccwas` processes a source file in two strictly ordered passes:

1. **Template pass (lccwas).** PHP-style: all text outside `<?lua ... ?>` regions is copied
   verbatim into the *emission buffer*; text inside is executed as Lua 5.5 chunks against a
   sandboxed state. `<?lua= expr ?>` is sugar for `lccwas.emit(expr)`.
2. **Parse pass (mpc).** The finished emission buffer — plain assembly text — is handed to the
   `third_party/mpc` grammar. ccwas is **purely textual**: it never encodes instructions or
   emits binary. Anything ccwas produces is subject to the same parsing, macro expansion, and
   ISA validation as hand-written assembly.

Consequences:

- Template expansion completes before any assembler directive or macro is interpreted. Lua
  cannot observe assembler state (symbol values, section offsets) because none exists yet.
  Anything address-dependent belongs in assembler directives/macros, not templates.
- The template pass runs only when the file contains a `<?lua` tag or `--template` is passed;
  tag-free files skip Lua entirely and pay zero cost.

- One expansion pass, no re-entry: emitted text containing `<?lua` is a fatal error, not a
  second expansion (prevents injection and non-terminating templates).

## 2. Buffering and provenance

1. `lccwas.emit(...)` appends to a single append-only buffer per translation unit. Ordering is
   exactly program order; there is no reordering, deduplication, or post-hoc patching API.
2. Every buffered span records provenance: template file, template line, and the Lua chunk
   position for generated spans. The parse pass threads this through, so an ISA error in
   generated text points at the `emit` call site, not at a synthetic line in an invisible
   intermediate file. `ccwas --keep-expanded` dumps the buffer with `#line`-style markers for
   debugging.
3. Verbatim text between tags is buffered through the same path, so provenance is uniform.

## 3. Module API

All functions raise Lua errors on misuse; §6 defines how those surface.

### 3.1 Emission

| Function | Behavior |
|---|---|
| `lccwas.emit(...)` | Appends each argument. Strings verbatim; integers in decimal; floats via `%.17g`; other types are errors (no implicit `tostring`, no `__tostring` — silent coercion hides bugs in generated assembly). |
| `lccwas.emitf(fmt, ...)` | `string.format` then emit. |
| `lccwas.emitln(...)` | `emit` plus trailing newline. Zero-argument form emits a bare newline. |

### 3.2 Target introspection (read-only)

`lccwas.target` is a frozen table describing the current assembly target:

- `arch` — `"x86-64"`, `"aarch64"`, `"riscv64"`, `"wasm32"` (matching `kernels/codegen-*.scm`)
- `syntax` — `"intel"` or `"gas"` (x86-64 only; other arches report their single syntax)
- `bits`, `endian`, `ptr_size`

### 3.3 ISA introspection (read-only)

Backed by the same loaded source-of-truth the assembler validates against —
`.agents/CPU-ISA.jsonl` for CPU targets, `.agents/WASM-ISA/` for wasm32 — so a template can
never disagree with the encoder about what exists:

- `lccwas.isa.has(mnemonic)` → boolean, for the current target
- `lccwas.isa.forms(mnemonic)` → array of operand-form descriptors (operand kinds, widths,
  required extension), or `nil`
- `lccwas.isa.extensions()` → sorted array of extension names known for the target
- `lccwas.isa.requires(mnemonic)` → extension name or `nil` (baseline)

Query results are loaded from the manifests at startup and immutable thereafter.

### 3.4 Data and layout helpers

Textual conveniences that expand to ordinary directives (they buy provenance and
target-awareness, nothing more):

- `lccwas.db(...)`, `lccwas.dw(...)`, `lccwas.dd(...)`, `lccwas.dq(...)` — emit the target's data
  directives with range-checked integer arguments
- `lccwas.bytes(str)` — emit a byte directive sequence from a Lua string (arbitrary bytes)
- `lccwas.zstring(str)` — `bytes` plus NUL terminator
- `lccwas.align(n)` — power-of-two check, then the target's alignment directive
- `lccwas.label(name)` — emit `name:` after validating label syntax for the target
- `lccwas.gensym([prefix])` — returns a fresh label name (`.Lccwas_<prefix>_<n>`), unique per
  translation unit, deterministic counter (§5). Does not emit.

### 3.5 Structure helpers

- `lccwas.section(name)` — emit the target's section-switch directive
- `lccwas.global(sym)`, `lccwas.equ(name, val)` — emit the corresponding directives
- `lccwas.include(path)` — run another template file through the template pass into the same
  buffer. Paths resolve relative to the including file; the include stack is cycle-checked and
  depth-capped (default 32). This is *template* inclusion; the assembler's own `.include`
  directive remains available in the parse pass and is unrelated.

### 3.6 Environment and diagnostics

- `lccwas.env` — read-only string map of CLI definitions (`ccwas -Dkey=value`). Absent keys are
  `nil`; there is no ambient environment access (§4).
- `lccwas.pos()` — current template file and line (for building custom diagnostics)
- `lccwas.warn(msg)` — assembler-formatted warning with template provenance
- `lccwas.error(msg)` — fatal; aborts the translation unit (equivalent to a Lua `error` with
  provenance pre-attached)

### 3.7 Lua-defined assembler macros

`lccwas.defmacro(name, fn)` registers `name` so that the *parse pass* macro expander, on
encountering a `name` invocation, calls `fn(args...)` with the invocation's arguments as
strings and splices the string it returns into the macro expansion stream. This is the one
deliberate exception to strict pass ordering — the Lua state is kept alive after the template
pass if any macros were registered. Restrictions keep it sound:

- `fn` must be pure text→text: calling `lccwas.emit` or `lccwas.include` inside a macro body is
  an error (the emission buffer is sealed once the parse pass starts).
- Returned text is macro-expanded and ISA-validated normally; it may not contain `<?lua`.
- Name collisions with directive-defined macros are a fatal error at `defmacro` time.

## 4. Sandbox

The template state is built from scratch; nothing is inherited from a default `luaL_openlibs`
world:

- **Available:** `string`, `table`, `math`, `utf8`, `ipairs`/`pairs`/`select`/`tonumber`/
  `tostring`/`type`/`error`/`pcall`/`assert`, and the `ccwas` module.
- **Removed:** `io`, `os`, `debug`, `package`/`require`, `load`/`loadstring`/`dofile`,
  coroutines, and any FFI. File access exists only via `lccwas.include`; host data enters only
  via `lccwas.env`.
- `--unsafe-lua` restores `io`, `os`, and `require` for local experimentation. CI builds MUST
  NOT pass it, and `ccwas` prints a prominent warning when it is active.

Rationale: assembly generation is part of the build graph; a template that reads the clock or
the filesystem produces outputs the build system cannot hash or cache correctly.

## 5. Determinism

Given identical (source file, `-D` set, target, syntax), the emission buffer MUST be
byte-identical across runs, hosts, and lccwas builds. Enforced by construction:

- no ambient input channels (§4);
- `math.random` is seeded to a fixed constant per translation unit;
- `pairs` over `ccwas`-provided tables iterates in sorted key order (template-authored tables
  keep normal Lua semantics — templates that iterate their own maps unordered and emit from
  them are a template bug, flagged in review, not something lccwas can fix);
- `lccwas.gensym` counters start at zero per translation unit.

CI double-assembles a golden template corpus and diffs the `--keep-expanded` output.

## 6. Error handling

1. Any uncaught Lua error in a template chunk, include, or macro function aborts the
   translation unit with a nonzero exit and a diagnostic carrying template file/line (and the
   include chain, if any). There is no "best effort" partial assembly.
2. Templates may use `pcall` internally; what they must not do is swallow `lccwas.error`.
3. Syntax errors in a `<?lua` chunk are reported against the template line of the opening tag
   plus the in-chunk offset.

## 7. Layout and testing

```
toolchain/ccwas/lccwas/
  lccwas.c        # module registration, sandbox construction, buffer
  lccwas.h        # embedding API for the ccwas driver (single header)
  isa_bridge.c    # read-only views over the loaded ISA tables
  tests/
    templates/    # golden template corpus (input .s + expected expanded .s)
    api/          # Lua-side unit tests for every §3 function
    sandbox/      # tests asserting removed globals are absent, --unsafe-lua works
    determinism/  # double-run byte-identity harness
```
All tests CI-gated and ASan-clean, matching the ecosystem-wide rule.

## 8. Decisions to append to `DECISIONS.md`

- **D-0025**: `ccwas` templates use Lua 5.5 (`third_party/lua`); Sched/Moonix stay on 5.4.
  The two Lua tracks never share a state.
- **D-0026**: lccwas is purely textual. Template output is ordinary assembly text, fully
  re-parsed and ISA-validated by the mpc pass; Lua has no direct access to encoders or
  symbol/section state.
- **D-0027**: Template states are sandboxed and deterministic; ambient I/O is opt-in via
  `--unsafe-lua` and forbidden in CI.
- **D-0028**: `lccwas.defmacro` is the sole cross-pass hook: parse-pass macros may be
  implemented as pure text→text Lua functions, with the emission buffer sealed.
`

