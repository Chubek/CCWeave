\page ccweave-manual-31 Chapter 31: Swaff overview

Swaff makes frontends swappable. A frontend combines a vendored Tree-sitter
grammar with a lowering adapter that walks its concrete syntax tree and
emits calls into a Kliche stereotype.

Tree-sitter's CST is editor-oriented and error-tolerant; it is not a
ready-made compiler AST. The adapter must normalize or discard trivia and
make an explicit decision for error and missing nodes.

Only the adapter may reference Tree-sitter node types. Public Swaff APIs and
all lower layers remain parser-framework independent.

Previous: \subpage ccweave-manual-30 "Object-oriented stereotype" · Next:
\subpage ccweave-manual-32 "Adapter policy"
