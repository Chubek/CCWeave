\page ccweave-manual-13 Chapter 13: Writing a kernel

Start a kernel with the required R7RS library wrapper and metadata exports.
Accept the `options` argument even when no options are currently used, and
reject unsupported capabilities defensively inside `kernel-apply`.

Navigate IR collections with the `-count` and `-ref` accessors. Inspect an
instruction through its opcode and operands. Build a detached replacement
with `instr-build`, then splice it with `instr-replace!`,
`instr-insert-before!`, or `instr-delete!`.

The supplied strength-reduction kernel is the reference pattern: it rewrites
integer multiply by a power of two, but leaves non-powers and two constants
unchanged.

Previous: \subpage ccweave-manual-12 "Generated manifests" · Next:
\subpage ccweave-manual-14 "Weave IR"
