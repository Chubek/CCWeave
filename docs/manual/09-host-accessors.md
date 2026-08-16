\page ccweave-manual-09 Chapter 9: Host accessors

The host registers a Scheme-visible procedure with `ccw_glue_register`.
Each registration declares an arity range and a C callback. The executor
checks arity before invoking the callback and raises a Scheme error on a
violation.

Callback arguments are executor-owned and temporary; a host callback must
not retain them. On success it returns `CCW_OK` and supplies a `ccw_val`.
On failure it returns an error status and may allocate a diagnostic message
for the executor to surface to Scheme.

`ccw_host_register_core_accessors` installs the required portable accessor
set for a Weave IR host.

Previous: \subpage ccweave-manual-08 "Boundary values" · Next:
\subpage ccweave-manual-10 "Kernel contract"
