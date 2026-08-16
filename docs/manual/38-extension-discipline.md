\page ccweave-manual-38 Chapter 38: Extension discipline

Extend CCWeave at the layer that owns the concern. Add a host accessor for
host-specific IR functionality, a profile construct for profile divergence,
a Kliche pattern for a paradigm mapping, or a Swaff adapter for CST logic.

Do not broaden the Glue value model with aggregate types. Do not expose
Tree-sitter types outside a Swaff adapter. Do not use engine-specific Scheme
features in a portable kernel.

When a necessary design choice is not covered by the specification, choose
the smallest mechanism consistent with its rationale and record the
interpretation in `DECISIONS.md` for review.

Previous: \subpage ccweave-manual-37 "Ownership" · Next:
\subpage ccweave-manual-39 "Dependencies"
