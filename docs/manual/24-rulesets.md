\page ccweave-manual-24 Chapter 24: Rulesets and patterns

Rulesets are named containers of unordered equivalence rules. Create one
with `ccw_oeuph_ruleset_create`, add rules with textual patterns, or load a
Scheme ruleset from `stdrewrite/`.

Patterns are small s-expressions over opcodes, variables, and integer
constants. A rule may be bidirectional and may have an optional
side-condition predicate.

Do not encode priority in rule order. Equality saturation resolves competing
equivalent forms at extraction, and authors must not rely on an application
order.

Previous: \subpage ccweave-manual-23 "Oeuph overview" · Next:
\subpage ccweave-manual-25 "Budgets and diagnostics"
