\page ccweave-manual-29 Chapter 29: Imperative stereotype

The imperative stereotype maps mutable locals to slots and structured
control flow to conditional and unconditional branches. Helpers construct
local allocation, loads, stores, branches, jumps, calls, arithmetic, and
returns.

The pattern makes frontends explicit about lowering mutable source variables
and control flow into core IR, rather than creating a parallel imperative
IR model.

After generating branches, use normal IR navigation and validation to
inspect control-flow edges and verify the resulting module.

Previous: \subpage ccweave-manual-28 "Functional stereotype" · Next:
\subpage ccweave-manual-30 "Object-oriented stereotype"
