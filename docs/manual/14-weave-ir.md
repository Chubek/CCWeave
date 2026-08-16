\page ccweave-manual-14 Chapter 14: Weave IR

Weave IR is one canonical in-memory representation with two profiles:
Tilly for ahead-of-time compilation and On1x for dynamic execution. It is
not two independent IRs.

The shared core provides types, functions, blocks, instructions, a C API,
text parsing and printing, native extension support, and Glue integration.
Its conventional instruction inventory is intentionally not the defining
feature; profile boundaries and kernel integration are.

Create a module with `ccw_ir_module_create`, passing its name and profile.
The module owns its IR nodes until `ccw_ir_module_destroy`.

Previous: \subpage ccweave-manual-13 "Writing a kernel" · Next:
\subpage ccweave-manual-15 "Types and nodes"
