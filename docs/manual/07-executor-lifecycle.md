\page ccweave-manual-07 Chapter 7: Executor lifecycle

Create an executor with `ccw_executor_create`, verify
`ccw_executor_abi_version` equals `CCW_GLUE_ABI_VERSION`, register host
accessors, then load kernels. Destroy the executor only after its kernels
are no longer needed.

Accessors must be registered before loading any kernel. Re-registering an
accessor name replaces the old binding, but the ABI forbids doing so after a
kernel has loaded.

`ccw_executor_name` returns a static identifying string. It is informational
only and must not be freed.

Previous: \subpage ccweave-manual-06 "Glue overview" · Next:
\subpage ccweave-manual-08 "Boundary values"
