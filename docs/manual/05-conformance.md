\page ccweave-manual-05 Chapter 5: Conformance and build order

Build the project in dependency order: Glue values, Weave IR, profiles, the
S7 executor, host accessors, manifest tooling, kernels, Oeuph and
stdrewrite, then Kliche and Swaff. Later subsystems assume the preceding
contracts work.

Conformance requires ABI v1 support, engine-agnostic kernel exports,
generated-and-checked manifests, profile-declared round-trippable modules,
and Oeuph rules that state equivalences only.

Each subsystem should build warning-clean and pass its focused tests before
the next stage is relied upon. When the specification is silent, record a
necessary interpretation in the repository's `DECISIONS.md`.

Previous: \subpage ccweave-manual-04 "Building" · Next:
\subpage ccweave-manual-06 "Glue ABI overview"
