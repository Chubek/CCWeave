\page ccweave-manual-30 Chapter 30: Object-oriented stereotype

The OOP stereotype supplies object allocation, vtable loading, vtable
dispatch, and exception-frame construction patterns. Its dispatch is the
profile-agnostic vtable form.

On1x can refine dynamic dispatch using inline caches, but that refinement is
not a prerequisite for using the stereotype. Tilly and core-only clients can
still use the common object-layout patterns.

Use frame push and pop helpers to make exception-frame construction explicit
in the emitted IR.

Previous: \subpage ccweave-manual-29 "Imperative stereotype" · Next:
\subpage ccweave-manual-31 "Swaff overview"
