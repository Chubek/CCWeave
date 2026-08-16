\page ccweave-manual-36 Chapter 36: Errors

Use `ccw_status` to distinguish loading, missing capability, kernel, ABI,
accessor, type, arity, and out-of-memory failures. Library code must return
an error rather than terminating the process.

When an accessor fails, the executor raises a Scheme condition whose message
includes the Scheme accessor name and host diagnostic. An accessor called
outside an active `kernel-apply` also raises.

Where an API provides `char **error_message`, the callee allocates its
diagnostic and the caller frees it. Always initialize and handle these
out-parameters consistently at API boundaries.

Previous: \subpage ccweave-manual-35 "Host integration" · Next:
\subpage ccweave-manual-37 "Ownership"
