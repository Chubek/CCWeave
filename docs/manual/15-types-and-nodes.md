\page ccweave-manual-15 Chapter 15: Types and nodes

Core scalar types are void, integer widths from `i1` through `i64`, `f32`,
`f64`, and pointer. Use the profile and type parse/name helpers when
converting text-facing names at an API boundary.

Functions, blocks, instructions, and operands are nodes. Node ids are
stable host-owned identities, are never pointers, and are never reused
after deletion. Query a node's kind before applying a kind-specific API.

Operands represent registers, integer or floating constants, functions, and
blocks. Constants carry a type and may be inspected with the corresponding
integer or floating-value API.

Previous: \subpage ccweave-manual-14 "Weave IR" · Next:
\subpage ccweave-manual-16 "Constructing IR"
