\page ccweave-manual-10 Chapter 10: Kernel contract

A kernel is an R7RS `define-library` stored in a `.scm` file. It imports
only `(scheme base)` and `(ccweave glue)` for portable behavior, and exports
`kernel-info`, `kernel-capabilities`, and `kernel-apply`.

`kernel-info` supplies at least a symbolic name, semantic-version string,
and description. `kernel-capabilities` returns the authoritative list of
capability symbols. `kernel-apply` accepts a capability, opaque IR handle,
and options alist, and returns an IR handle or a Scheme error object.

Kernels define transformations, not pipeline positions. Scheduling remains
the host's responsibility; kernels also must not mutate global state.

Previous: \subpage ccweave-manual-09 "Host accessors" · Next:
\subpage ccweave-manual-11 "Capabilities"
