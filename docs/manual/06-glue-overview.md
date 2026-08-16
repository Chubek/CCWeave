\page ccweave-manual-06 Chapter 6: Glue ABI overview

`glue/GlueSTD.h` defines ABI version 1 between a CCWeave host and a kernel
executor. The executor embeds a Scheme implementation; the host owns the
IR and registers procedures through which a kernel can inspect or edit it.

The ABI deliberately carries only seven scalar-like `ccw_type` variants:
nil, bool, integer, float, string, symbol, and node. Collections are
traversed through indexed accessors rather than marshalled as aggregates.

An executor is conformant only when it implements every declaration in the
header with its documented semantics. Check its ABI version before any
other executor operation.

Previous: \subpage ccweave-manual-05 "Conformance" · Next:
\subpage ccweave-manual-07 "Executor lifecycle"
