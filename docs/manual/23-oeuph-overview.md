\page ccweave-manual-23 Chapter 23: Oeuph overview

Oeuph is CCWeave's equality-saturation engine. It operates directly on the
canonical in-memory Weave IR, growing equivalent alternatives and selecting
one with an extraction cost model.

Optimization and normalization are different extraction goals, not different
soundness rules. Performance extraction selects a performance or size
oriented result; canonical extraction selects a normalized equivalent.

Oeuph is not a repair system. A rule that turns incorrect code into correct
code is not an equivalence and is out of scope.

Previous: \subpage ccweave-manual-22 "Round-trip discipline" · Next:
\subpage ccweave-manual-24 "Rulesets and patterns"
