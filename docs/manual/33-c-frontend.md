\page ccweave-manual-33 Chapter 33: C frontend

The C frontend combines the vendored tree-sitter-c grammar and CCWeave's C
lowering adapter. Obtain it with `ccw_swaff_frontend_c` and identify it with
`ccw_swaff_frontend_name`.

Call `ccw_swaff_lower` with source bytes, a module name, target profile,
error policy, report storage, and an error-message out parameter. The
result is a fresh Weave IR module or null on failure.

Check `ccw_swaff_available` before presenting this frontend as usable in a
build configured without Tree-sitter support.

Previous: \subpage ccweave-manual-32 "Adapter policy" · Next:
\subpage ccweave-manual-34 "S7 executor"
