\page ccweave-manual-18 Chapter 18: Text format

Weave IR text is a serialization of canonical memory, not a second
representation. `ccw_ir_parse` and `ccw_ir_parse_file` create fresh modules;
`ccw_ir_print` returns malloc-allocated UTF-8 text that the caller frees.

A parse failure returns no module and supplies a malloc-allocated error
message for the caller to free. Treat text as an interchange and debugging
surface, not as a place to preserve identity-based implementation details.

Use structural equality, which excludes node-id allocation choices, when
testing the semantic result of text serialization.

Previous: \subpage ccweave-manual-17 "Navigation and mutation" · Next:
\subpage ccweave-manual-19 "Validation and profiles"
