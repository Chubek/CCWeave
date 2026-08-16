\page ccweave-manual-25 Chapter 25: Budgets and diagnostics

Provide maximum node, e-class, iteration, and wall-time limits for every
Oeuph run. A budget hit stops saturation and proceeds to extraction; it is a
best-effort result rather than a failure to extract.

For reproducibility, use a fixed seed and fixed budget. Under those inputs,
saturation and extraction must produce the same result across runs.

Inspect `ccw_oeuph_stats` after each run. It reports ruleset name, growth,
matches, unions, iteration count, saturation status, and the budget that
stopped the run when applicable.

Previous: \subpage ccweave-manual-24 "Rulesets and patterns" · Next:
\subpage ccweave-manual-26 "Standard rewrites"
