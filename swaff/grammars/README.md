# Swaff grammars

Each Swaff frontend pairs a Tree-sitter grammar with a lowering adapter
in `../adapters/`. The C frontend builds the checked-in runtime and
generated parser from `third_party/tree-sitter/` and
`third_party/tree-sitter-c/`; no parser generation, system package, or
network access is required.

Adding a frontend means adding a grammar entry to the lock file and an
adapter that emits Kliche stereotype calls. Nothing outside
`swaff/adapters/` may include a Tree-sitter header (spec §6.2).
