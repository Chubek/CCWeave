\page ccweave-manual-19 Chapter 19: Validation and profiles

Every module declares Tilly or On1x in its header. `ccw_ir_validate` checks
the shared representation and profile-specific restrictions, returning an
allocated diagnostic on failure.

Validation is the enforcement point for the profile split. A Tilly module
must reject dynamic-dispatch metadata; an On1x module may use its dynamic
execution constructs without making them part of the shared core.

Run validation at externally visible boundaries: after parsing, after
lowering, and after transformations that can affect structure or metadata.

Previous: \subpage ccweave-manual-18 "Text format" · Next:
\subpage ccweave-manual-20 "Tilly profile"
