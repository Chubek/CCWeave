\page ccweave-manual-34 Chapter 34: S7 executor

The S7 executor is the reference implementation of Glue ABI v1. It embeds
the vendored S7 engine and provides the ABI lifecycle, loading, metadata,
capability enumeration, invocation, unloading, and accessor binding.

The executor does not invent a fixed IR interface of its own. The host
registers accessors at runtime, making kernels portable across conforming
executors and host implementations.

An executor must reject a load when the required kernel exports are missing,
and it must verify a requested capability before calling `kernel-apply`.

Previous: \subpage ccweave-manual-33 "C frontend" · Next:
\subpage ccweave-manual-35 "Host integration"
