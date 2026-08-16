# Proposed Glue Extension Contract v0.1

**Status:** approved for phased implementation. Phase 1 is promoted into
`glue/GlueSTD.h`; Phase 2 additionally promotes CFG navigation. The
remaining groups stay non-normative until their host implementations and
capability tests land. Glue ABI v1 remains scalar-only.
A host advertises each implemented accessor through `glue-has?`; a kernel
must reject or leave the IR unchanged when a required accessor is absent.

## Goals

- Make the manifested kernel capabilities implementable without aggregates
  crossing the C ABI.
- Keep IR nodes as `uint64_t` ids and use `-count`/`-ref` navigation for
  every collection.
- Keep analysis results host-owned, deterministic, and invalidated on
  structural edits.
- Make target, VM, and debug features explicitly profile-scoped.

## Common Rules

All accessors below use the existing `ccw_val` scalar types. `key`, `kind`,
`opcode`, and `name` parameters are symbols unless stated otherwise.
`node` parameters and return values are `ccw_node` ids. Mutation accessors
must invalidate any facts whose dependency set intersects the edit.

An accessor returning a collection has a matching `-count` and `-ref` pair.
Out-of-range indexes and unsuitable node kinds raise an accessor condition.
`analysis-*` accessors operate only during `kernel-apply`, just like the
Core Accessor Set.

## Analysis Facts

The host owns all analysis data. Facts are namespaced by capability, so a
kernel writing `analysis.range` cannot overwrite facts owned by
`analysis.alias`.

```scheme
(analysis-clear! capability)                    ; -> nil
(analysis-has? capability subject key)          ; -> bool
(analysis-get capability subject key)           ; -> scalar | nil
(analysis-put! capability subject key value)    ; -> nil
(analysis-ref-count capability subject key)     ; -> int
(analysis-ref capability subject key index)     ; -> node
```

`subject` is normally an instruction, block, function, or operand node.
Scalar values represent booleans, integers, floats, strings, or symbols;
node-valued facts use the `analysis-ref-*` collection pair. A kernel must
clear its namespace before publishing a recomputed analysis.

## General IR and SSA

```scheme
(node-kind node)                                 ; -> symbol
(node-type node)                                 ; -> symbol
(instr-result ins)                               ; -> node | nil
(instr-result-name ins)                          ; -> string | nil
(instr-use-count ins)                            ; -> int
(instr-use-ref ins index)                        ; -> node
(operand-kind operand)                           ; -> symbol
(operand-name operand)                           ; -> string | nil
(operand-def operand)                            ; -> node | nil
(operand-use-count operand)                      ; -> int
(operand-use-ref operand index)                  ; -> node
(instr-set-dest! ins name)                       ; -> nil
(instr-set-operand! ins index operand)           ; -> nil
```

`instr-result` identifies the SSA value defined by an instruction.
`operand-def` is nil for constants, parameters, and externally defined
values. `instr-set-operand!` is the only portable mutation of an existing
instruction's use list; it must type-check the replacement.

## Control Flow

```scheme
(block-pred-count block)                         ; -> int
(block-pred-ref block index)                     ; -> node
(block-succ-count block)                         ; -> int
(block-succ-ref block index)                     ; -> node
(block-terminator block)                         ; -> node | nil
(block-build function name)                      ; -> node
(block-insert-before! anchor block)              ; -> nil
(block-delete! block)                            ; -> nil
(block-split! block anchor name)                 ; -> node
(jump-build target-block)                        ; -> node
(branch-build condition true-block false-block)  ; -> node
(terminator-replace! old new)                    ; -> nil
```

`block-split!` moves `anchor` and following instructions into the returned
block and inserts a jump from the original block. `block-delete!` rejects a
block with predecessors unless those predecessors have first been rewired by
`terminator-replace!`.

## Calls, Memory, and Data Layout

```scheme
(instr-memory-effect ins)                        ; -> symbol
(instr-volatile? ins)                            ; -> bool
(instr-atomic-order ins)                         ; -> symbol | nil
(call-kind ins)                                  ; -> symbol | nil
(call-callee ins)                                ; -> node | nil
(call-arg-count ins)                             ; -> int
(call-arg-ref ins index)                         ; -> node
(call-build kind callee arg ...)                 ; -> node
(type-size type)                                 ; -> int
(type-align type)                                ; -> int
(field-offset type field-index)                  ; -> int
```

Memory effects are one of `none`, `read`, `write`, `readwrite`, or `unknown`.
The host must conservatively return `unknown` for unanalyzable calls.
`call-build` is profile-validated and rejects unsupported calling forms.

## Target and Machine IR

These accessors are available only when `ir-profile` and the target profile
support machine IR.

```scheme
(target-name)                                    ; -> symbol
(target-feature? feature)                        ; -> bool
(target-int-width)                               ; -> int
(target-register-count class)                    ; -> int
(target-register-ref class index)                ; -> symbol
(target-latency opcode)                          ; -> int
(machine-instr? ins)                             ; -> bool
(machine-opcode ins)                             ; -> symbol
(machine-build opcode operand ...)               ; -> node
(machine-set-register! value register)           ; -> nil
(machine-spill-slot-build type)                  ; -> node
```

`machine-set-register!` rejects registers outside the target's declared
class. `machine-build` must preserve the operand/result typing rules of the
selected target profile.

## VM, Sanitizer, and Debug Profiles

```scheme
(vm-safepoint-required? block)                   ; -> bool
(vm-safepoint-build)                             ; -> node
(vm-deopt-target ins)                            ; -> string | nil
(vm-deopt-state-count ins)                       ; -> int
(vm-deopt-state-ref ins index)                   ; -> node
(vm-deopt-state-set! ins index value)            ; -> nil
(sanitize-handler kind)                          ; -> node | nil
(sanitize-check-build kind operand ...)          ; -> node
(node-source-file node)                          ; -> string | nil
(node-source-line node)                          ; -> int | nil
(debug-line-record! node file line column)       ; -> nil
```

VM accessors require an On1x-like VM profile. Debug accessors do not emit an
object file; they publish host-owned line records for the later object writer.
Sanitizer checks must preserve normal control-flow semantics on the
non-trapping path.

## Capability Requirements

| Capability family | Required extension groups |
| --- | --- |
| `analysis.*` | Analysis Facts; General IR; Control Flow as needed |
| `opt.*` | General IR; Control Flow; Calls/Memory as needed |
| `normalize.*`, `transform.*`, `lower.*` | General IR plus the construct-specific group |
| `codegen.*` | Target and Machine IR; Calls/Memory |
| `vm.*` | VM, Sanitizer, and Debug Profiles; Control Flow |
| `sanitize.*` | VM, Sanitizer, and Debug Profiles; General IR |
| `debug.*` | VM, Sanitizer, and Debug Profiles |

## Adoption Sequence

1. Promote reviewed accessors into a versioned normative Glue extension
   header without changing the closed `ccw_val` set.
2. Implement host accessors and S7 registration, with arity and accessor
   error tests for every accessor.
3. Add capability-level kernel tests before replacing no-op kernel bodies.
4. Require each kernel to feature-test every non-Core accessor and return an
   explanatory Scheme error when the selected capability cannot run.
