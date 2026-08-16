\page ccweave-manual-16 Chapter 16: Constructing IR

Build a module by adding functions, parameters, and blocks, then create
detached instructions. Set an instruction destination and operands before
attaching it to a block with `ccw_ir_block_append_instr`.

Construct operands with the explicit register, function, block, integer
constant, and floating constant constructors. This keeps the representation
typed and avoids passing raw textual operands around the host API.

Attributes are ordered key/value pairs and can belong to the module or to a
node. Node id zero denotes the module when using the attribute API.

Previous: \subpage ccweave-manual-15 "Types and nodes" · Next:
\subpage ccweave-manual-17 "Navigation and mutation"
