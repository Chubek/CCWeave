\page ccweave-manual-33 Chapter 33: C and OCaml frontends

The C frontend combines the vendored tree-sitter-c grammar and CCWeave's C
lowering adapter. Obtain it with `ccw_swaff_frontend_c` and identify it with
`ccw_swaff_frontend_name`.

Call `ccw_swaff_lower` with source bytes, a module name, target profile,
error policy, report storage, and an error-message out parameter. The
result is a fresh Weave IR module or null on failure.

Check `ccw_swaff_available` before presenting this frontend as usable in a
build configured without Tree-sitter support.

The OCaml implementation frontend is obtained with
`ccw_swaff_frontend_ocaml`. It lowers top-level `let` functions, integer and
boolean scalar expressions, direct calls, higher-order parameter application,
immutable local `let` bindings, sequencing, and value-producing `if`
expressions. Higher-order calls use Kliche's functional `call.indirect`
pattern; `if` values use explicit Kliche control flow and a merge slot.

The current adapter deliberately reports unsupported CST nodes for global
value bindings, destructuring patterns, nested named functions, and other
OCaml constructs without a defined mapping to the present Weave IR and
Kliche APIs. Both adapters apply the same explicit reject/recover policy to
Tree-sitter `ERROR` and `MISSING` nodes.

Previous: \subpage ccweave-manual-32 "Adapter policy" · Next:
\subpage ccweave-manual-34 "S7 executor"
