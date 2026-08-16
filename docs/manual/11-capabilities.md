\page ccweave-manual-11 Chapter 11: Capabilities

A capability identifier is a dotted lowercase symbol such as
`opt.licm` or `regalloc.linear-scan`. It must match
`[a-z0-9-]+(\.[a-z0-9-]+)+`.

The live value returned by `kernel-capabilities` is the single source of
truth. Before dispatching, an executor verifies that the requested
capability is present and returns `CCW_ERR_NO_CAPABILITY` when it is not.

A capability variant can communicate a profile requirement, for example an
On1x-specific optimization. A profile-agnostic kernel must operate on only
the shared core.

Previous: \subpage ccweave-manual-10 "Kernel contract" · Next:
\subpage ccweave-manual-12 "Generated manifests"
