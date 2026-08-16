\page ccweave-manual-22 Chapter 22: Round-trip discipline

Round-trip behavior is a conformance requirement: parse, print, and parse
again must yield structurally identical canonical modules. The inverse
direction also matters: every in-memory module must be printable.

Test every supported construct in both profiles. Compare results with
`ccw_ir_equal`, not node ids, because ids are allocation details rather than
part of structural equality.

This guarantee lets Oeuph and other clients work identically on parsed IR
and programmatically constructed IR.

Previous: \subpage ccweave-manual-21 "On1x profile" · Next:
\subpage ccweave-manual-23 "Oeuph overview"
