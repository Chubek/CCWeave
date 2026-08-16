\page ccweave-manual-08 Chapter 8: Boundary values

Use the `ccw_nil`, `ccw_bool`, `ccw_int`, `ccw_float`, `ccw_string`,
`ccw_symbol`, and `ccw_node_val` constructors to create values crossing the
Glue boundary. String and symbol constructors copy their input.

Call `ccw_val_clear` on any value whose lifetime has ended. It frees owned
string storage when applicable, resets the value to nil, and is safe to call
repeatedly.

No pair, vector, list, pointer, or other aggregate is a legal boundary
value. In particular, IR identities are `uint64_t` node ids; id zero means
no node.

Previous: \subpage ccweave-manual-07 "Executor lifecycle" · Next:
\subpage ccweave-manual-09 "Host accessors"
