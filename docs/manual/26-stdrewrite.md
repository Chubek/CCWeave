\page ccweave-manual-26 Chapter 26: Standard rewrites

`rewrite-salvo/` holds the shipped Scheme rulesets, organized by domains such
as arithmetic, bitwise operations, comparisons, memory, normalization, and
peepholes.

Each file declares a ruleset name. Enable rulesets intentionally rather than
assuming every shipped rule applies globally to every compilation.

Review a new rule as an equivalence claim. Its direction, any
side-condition, and behavior under the selected cost model must preserve
semantics; a convenient code repair is not sufficient.

Previous: \subpage ccweave-manual-25 "Budgets and diagnostics" · Next:
\subpage ccweave-manual-27 "Kliche overview"
