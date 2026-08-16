\page ccweave-manual-37 Chapter 37: Ownership

The host owns `ccw_ir` modules and resolves every node id. Executors and
kernels treat IR handles as opaque and see individual nodes only as 64-bit
ids.

Strings are copied at the ABI boundary. A `ccw_val` string or symbol owns
its buffer until cleared. Kernel-info strings and error-message out values
are allocated by the callee and freed by the caller as documented.

Capability enumeration strings are executor-owned and remain valid only
until the next call on that executor; copy them before retaining them.
Run Glue tests under ASan/LSan to verify these rules.

Previous: \subpage ccweave-manual-36 "Errors" · Next:
\subpage ccweave-manual-38 "Extension discipline"
