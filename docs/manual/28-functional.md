\page ccweave-manual-28 Chapter 28: Functional stereotype

The functional stereotype represents closures as records. Allocate a closure
with a code symbol and capture count, write captured fields, read fields by
slot, and apply the closure through an indirect call.

The public helpers are `ccw_kliche_closure_alloc`,
`ccw_kliche_closure_capture`, `ccw_kliche_closure_ref`, and
`ccw_kliche_closure_apply`. They emit core-IR construction patterns.

This representation is profile-agnostic. Dynamic profiling or cache
refinements, if wanted, belong in a profile-aware layer above the core
pattern.

Previous: \subpage ccweave-manual-27 "Kliche overview" · Next:
\subpage ccweave-manual-29 "Imperative stereotype"
