\page ccweave-manual-27 Chapter 27: Kliche overview

Kliche provides documented construction stereotypes for functional,
imperative, and object-oriented source concepts. It is a library of mappings
to the core IR API, not a separate intermediate representation.

A stereotype must not require a particular profile, although a caller may
add profile-specific refinements afterward. Kliche has no Tree-sitter
dependency.

Use Kliche from a Swaff lowering adapter or another host-side frontend when
the source-level concept is clearer than spelling each IR construction step.

Previous: \subpage ccweave-manual-26 "Standard rewrites" · Next:
\subpage ccweave-manual-28 "Functional stereotype"
