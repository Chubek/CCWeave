\page ccweave-manual-20 Chapter 20: Tilly profile

Tilly is the ahead-of-time profile. It adds static call and relocation
constructs, link-section attributes, and whole-module layout directives.

Tilly deliberately forbids dynamic-dispatch metadata. This is a profile
constraint, not a reason to fork the shared IR core or duplicate its
construction and serialization machinery.

Use Tilly when the target needs static linking and layout facts. A kernel
that relies on a Tilly-only facility should advertise that requirement in an
appropriate capability variant.

Previous: \subpage ccweave-manual-19 "Validation and profiles" · Next:
\subpage ccweave-manual-21 "On1x profile"
