\page ccweave-manual-02 Chapter 2: Architecture and boundaries

CCWeave has five vertical layers: Swaff, Kliche, Weave IR, Glue, and Kernels.
Each depends only on the layer directly beneath it. Oeuph is auxiliary: it
depends on Weave IR and Glue, but is not part of that vertical stack.

Swaff owns parsing and CST handling. Kliche maps language paradigms to IR
construction. Weave IR is the canonical representation. Glue is the narrow
C ABI used by embedded Scheme executors. Kernels express scheduled-by-host
transformations in portable R7RS Scheme.

Keeping these boundaries intact is more important than reducing call count:
no component below Swaff sees Tree-sitter node types, and no kernel receives
an IR pointer.

Previous: \subpage ccweave-manual-01 "Orientation" · Next:
\subpage ccweave-manual-03 "Repository map"
