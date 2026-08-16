\page ccweave-manual-17 Chapter 17: Navigation and mutation

IR navigation is indexed: obtain a count, then retrieve a node by index.
Functions expose parameters and blocks; blocks expose instructions,
predecessors, and successors; instructions expose their opcode, destination,
type, and operands.

Structural instruction mutation is intentionally limited to replacement,
insertion before an anchor, and deletion. The Glue accessors expose the same
three operations so hosts can interpose on every kernel edit.

Block deletion and merge are host C APIs. Validate after a nontrivial
structural transformation so malformed graphs or profile violations are
reported at the right boundary.

Previous: \subpage ccweave-manual-16 "Constructing IR" · Next:
\subpage ccweave-manual-18 "Text format"
