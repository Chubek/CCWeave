# ccwas Specification v0.1

`ccwas` is the CCWeave cross-assembler. It turns one assembly source file into one object
artifact for a single target per invocation: **x86-64**, **aarch64**, **riscv64**, or
**wasm32**. It lives at `toolchain/ccwas/` and is a standalone build tool — no Sched plans,
no Kernel.yaml probing, no runtime coupling to Cephyr or Moonix. Codegen kernels (the four
`codegen.*` capabilities) and hand-written runtime stubs are its expected producers; the
linker consuming Glue ABI v1 objects is its expected consumer.

## 1. Scope and non-goals

- **In scope:** assembling, macro expansion, ISA validation against the manifests, template
  expansion via lccwas, relocatable object emission.
- **Non-goals:** linking, disassembly, optimization of any kind (ccwas encodes exactly what
  it is given — no relaxation beyond what §8.3 mandates, no peepholes; the equivalence-only
  regime of D-0007 applies to rewriters, and ccwas is not a rewriter), and multi-target fat
  objects. One invocation, one target, one object.

## 2. Pipeline

Strictly ordered phases per translation unit:

1. **Template pass (lccwas).** Runs iff the source contains `<?lua` or `--template` is
   passed. Output is the emission buffer: plain assembly text with provenance spans
   (lccwas §1–§2). Tag-free sources skip this phase entirely.
2. **Parse pass (mpc).** The buffer (or raw source) is parsed by the target's grammar from
   `toolchain/ccwas/grammar/<arch>.mpc`. Grammars share a common core (labels, directives,
   expressions, macro syntax) and differ only in the statement/operand layer.
3. **Macro expansion.** Directive-defined macros (`.macro`) and Lua-defined macros
   (`ccwas.defmacro`, D-0028) expand from a single namespace, outermost-first, with a
   recursion depth cap (default 128). Expanded text re-enters the parse pass; it may not
   contain `<?lua` (lccwas §1).
4. **Semantic pass.** Symbol table construction, section assignment, expression evaluation
   where operands are known, ISA validation of every instruction against the loaded
   manifests (§8).
5. **Layout and encoding.** Section offsets fixed, instructions encoded, expressions that
   depended on layout resolved; anything still unresolved becomes a relocation.
6. **Emission.** Object writer for the target's container (§9).

Phases never interleave: an ISA error in phase 4 is reported before any encoding happens,
and a template error aborts before mpc ever runs.

## 3. Invocation

```
ccwas --target=<arch> [options] input.s -o output.o
```
| Option | Meaning |
|---|---|
| `--target=<arch>` | Required. `x86-64`, `aarch64`, `riscv64`, `wasm32`. No default — silent host-targeting is how wrong objects enter build caches. |
| `--syntax=<s>` | x86-64 only: `intel` (default) or `gas`. Other targets reject the flag. |
| `-D key=value` | Populates `ccwas.env` (lccwas §3.6) and the `.ifdef` directive namespace. Repeatable. |
| `--template` | Force the template pass even without a `<?lua` tag. |
| `--keep-expanded=<path>` | Dump the post-template buffer with `#line` markers (lccwas §2). |
| `--unsafe-lua` | lccwas §4. Forbidden in CI. |
| `-I <dir>` | Search path for `.include` (parse-pass) and `ccwas.include` (template-pass), tried after the including file's directory. Repeatable, searched in order. |
| `--defsym name=expr` | Predefine an absolute symbol. |
| `-W error` | Promote all warnings to errors. CI builds pass this. |

Exactly one input file. Multi-file programs are the linker's job.

## 4. Source language

### 4.1 Common core (all targets)

- Line-oriented; `;` starts a comment on CPU targets, `;;` on wasm32 (avoiding collision
  with the folded-form convention); `#` is **not** a comment character (it belongs to the
  preprocessor world ccwas deliberately does not have — templates cover that role).
- Labels: `name:` at line start. Local labels: `.Lname` (not emitted to the symbol table
  unless `.global`ized, which is an error for `.L` names).
- One statement per line; `\` continues a line.
- Case: mnemonics and register names case-insensitive; symbols, section names, and macro
  names case-sensitive. This matches the dominant convention per target while keeping user
  namespaces exact.
- Integer literals: `0x`, `0b`, `0o`, decimal; character literals `'a'`; strings with the
  usual C escapes. Expressions: `+ - * / % << >> & | ^ ~ ()` over integers and symbols,
  with the standard restriction that symbol-difference expressions must resolve within one
  section or become relocations (§7.3).

### 4.2 Per-target statement layer

- **x86-64:** Intel syntax default (`mov rax, [rbx+8]`); `--syntax=gas` accepts AT&T. The
  two syntaxes parse to the same operand IR, so validation and encoding are shared.
- **aarch64 / riscv64:** the standard GNU-style syntax for each; RISC-V accepts the
  canonical ABI register names (`a0`, `sp`) and numeric names (`x10`).
- **wasm32:** flat text format — one instruction per line, block structure via explicit
  `block`/`loop`/`if`/`end`, function boundaries via `.func`/`.endfunc` directives (§5.3).
  This is *not* full WAT: no s-expressions, no folded forms. Rationale: keeping the mpc
  grammar in the same line-oriented family as the CPU targets keeps templates and macros
  target-portable; users who want WAT have other tools.

## 5. Directives

### 5.1 Universal

`.section name[, flags]`, `.global sym`, `.local sym`, `.weak sym`, `.equ name, expr`,
`.byte/.2byte/.4byte/.8byte expr...`, `.ascii/.asciz str`, `.zero n`, `.align n`, `.repeat`, etc (add at least 4 more directives)
(power-of-two, fill with target NOP in code sections and zeros elsewhere), `.include path`
(parse-pass textual include, cycle-checked, depth 32 — distinct from `ccwas.include`),
`.ifdef/.ifndef/.if expr/.else/.endif` (over `-D` keys and `--defsym` symbols only —
conditionals cannot see layout), `.macro/.endm` (§6), `.error "msg"`, `.warning "msg"`.

### 5.2 CPU-target

`.text/.data/.rodata/.bss` shorthands, `.type sym, @function|@object`, `.size sym, expr`,
`.reloc offset, type, sym` (escape hatch for relocation types the expression syntax cannot
express; type names validated against §7.3's per-target set).

### 5.3 wasm32

`.func name (params) (results)`, `.endfunc`, `.local type...`, `.import module name kind`,
`.export name kind`, `.memory min[, max]`, `.table`, `.global_wasm type [mutable]` (named to
avoid colliding with the universal `.global` visibility directive), `.data_seg offset`.
Universal data directives inside `.data_seg` emit into the active data segment.

### 5.4 Alias policy

Every directive has exactly one name. No GNU-compat aliases (`.word` vs `.short` vs
`.hword`): sized data directives are byte-count-explicit (`.2byte`, `.4byte`, `.8byte`) so
they mean the same thing on every target. Porting friction is cheaper than a directive that
silently changes width across targets.

## 6. Macros


.macro name arg1, arg2=default
    ; body; \arg1 substitutes textually; \@ is a per-expansion unique suffix
.endm

- Textual substitution, expanded at phase 3, re-parsed after expansion.
- `\@` provides local-label uniqueness inside macro bodies (deterministic counter, same
  regime as `ccwas.gensym`).
- Directive-defined and `defmacro`-defined macros share one namespace; collisions are fatal
  at definition time regardless of which side defined first (lccwas §3.7 states the Lua
  side of this rule; it is symmetric).
- Macros may invoke macros; recursion depth cap 128; expansion is deterministic and
  side-effect-free except through emitted text.
- Selection guidance (normative note, not enforced): use `.macro` for target-specific
  idioms living next to the code; use lccwas templates for loops, tables, and anything
  needing computation; use `defmacro` only when a macro genuinely needs Lua logic *and*
  must expand at parse time with access to invocation-site arguments.

## 7. Symbols, sections, relocations

### 7.1 Symbols

Defined (label/`.equ`/`--defsym`), or undefined-external (referenced, never defined —
emitted as undefined symbols for the linker). Visibility: local (default), `.global`,
`.weak`. Redefinition of any symbol is fatal; there is no `.set`-style reassignment.

### 7.2 Sections

Standard progbits/nobits distinction on CPU targets; section flags default sensibly from
well-known names (`.text` → alloc+exec) and are otherwise explicit. On wasm32, "sections"
map to the module-space equivalents: code goes in functions, data directives in data
segments, and the section machinery validates that e.g. instructions never appear outside
`.func` scope.

### 7.3 Relocations

Unresolvable expressions become relocations, restricted to each target's supported set:
`R_X86_64_*`, `R_AARCH64_*`, `R_RISCV_*` (including the `PCREL_HI20`/`PCREL_LO12` pairing,
which the assembler validates as pairs), and wasm32 linking-convention relocations. The
supported subset per target is tabulated in `toolchain/ccwas/reloc/<arch>.def`, and an
expression that would need an unsupported relocation is a fatal error naming the relocation
it would have needed — never a silently truncated constant.

## 8. Encoding and ISA validation

### 8.1 Source of truth

`.agents/CPU-ISA.jsonl` (CPU targets) and `.agents/WASM-ISA/` (wasm32) are loaded at
startup into the same immutable tables that back `ccwas.isa` (lccwas §3.3). Every parsed
instruction is validated against them: mnemonic exists for the target, operand form
matches, extension requirement satisfied. There is no fallback encoder path and no
"unknown instruction passthrough."

### 8.2 Extensions

Baseline per target is the manifest's baseline set. `.arch enable <ext>` /
`.arch disable <ext>` adjust the allowed set from that point in the file; using an
instruction whose `ccwas.isa.requires` extension is not enabled is fatal, with the
extension named in the diagnostic. `--defsym`-level control is deliberately absent:
extension state is source-visible or it is nothing.

### 8.3 Encoding discipline

Encoding is deterministic and canonical: one encoding per (instruction, operand-form)
pair, chosen by the manifest, byte-identical across runs and hosts (the object-level
counterpart of lccwas §5). The only size flexibility is mandatory-by-ISA relaxation
(RISC-V compressed forms are emitted only under `.arch enable c` and even then only when
the manifest marks the form canonical; branch relaxation inserts the documented
short/long sequences and is reflected in the relocation stream). No instruction selection,
no scheduling — see §1.

## 9. Object output

- **CPU targets:** ELF64 relocatable objects (`ET_REL`), little-endian, conforming to
  Glue ABI v1's object-level conventions (symbol naming, section naming, the `.note.ccw`
  version note identifying producer `ccwas` and manifest hashes of the ISA tables used).
- **wasm32:** a wasm object module using the linking custom sections (`linking`, `reloc.*`),
  i.e. linker input, not a runnable module.
- Object output is byte-identical for identical (input, `-D` set, target, syntax, ccwas
  version): section order is definition order, symbol table order is first-occurrence,
  no timestamps anywhere. CI double-assembles and byte-diffs, extending the lccwas
  determinism gate through the whole pipeline.

## 10. Diagnostics

Every diagnostic carries the full provenance chain: object-file position ← macro expansion
stack (with `\@` instance) ← parse-pass line ← template `emit` call site, where each link
exists. Format matches the ecosystem's standard diagnostic shape so `ccw-manifest`-adjacent
tooling can parse it. `.error`/`.warning` and `ccwas.error`/`ccwas.warn` all land in the
same stream. Exit codes: 0 clean, 1 diagnostics with errors, 2 usage/environment errors.

## 11. Layout and testing

```
toolchain/ccwas/
  driver/          # CLI, phase sequencing
  grammar/         # <arch>.mpc grammars + shared core grammar
  sema/            # symbol table, expressions, ISA validation
  encode/          # per-target encoders (manifest-driven)
  reloc/           # <arch>.def relocation tables
  obj/             # ELF64 and wasm object writers
  lccwas/          # per the lccwas spec
  tests/
    parse/         # grammar goldens per target, including syntax=gas
    encode/        # instruction goldens: every manifest form assembled + byte-checked
    reloc/         # relocation stream goldens, incl. RISCV hi/lo pairing errors
    macro/         # .macro, \@, defmacro interop, collision fatality
    object/        # ELF/wasm container goldens, .note.ccw checks
    determinism/   # double-assemble byte-diff harness
    negative/      # every fatal path in this spec has at least one test
```
The `encode/` goldens are generated *from* the ISA manifests by a checked-in generator, so
manifest updates automatically widen coverage; hand-written goldens cover the interesting
composite cases. All CI-gated, ASan-clean.

## 12. Decisions to append to `DECISIONS.md`

- **D-0029**: ccwas performs no optimization — canonical, deterministic encoding only;
  mandatory ISA relaxation (RISC-V compressed/branch) is the sole size flexibility. It is
  not a rewriter and sits outside the D-0007 regime rather than being an exception to it.
- **D-0030**: The ISA manifests (`.agents/CPU-ISA.jsonl`, `.agents/WASM-ISA/`) are the
  single validation and encoding authority; no fallback encoder, no unknown-instruction
  passthrough. `ccwas.isa` and the encoder read the same loaded tables.
- **D-0031**: One invocation = one target = one relocatable object. CPU targets emit ELF64
  with the `.note.ccw` producer note; wasm32 emits a linking-convention object module.
  Objects are byte-reproducible and CI byte-diffed.
- **D-0032**: Directives are single-name and width-explicit (no GNU-compat aliases);
  wasm32 uses the flat line-oriented dialect, not WAT.
- **D-0033**: `.macro` and `ccwas.defmacro` share one namespace with symmetric fatal
  collisions; all macro expansion is deterministic with capped depth.
`

