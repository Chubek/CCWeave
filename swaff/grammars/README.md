# Swaff grammars

Each Swaff frontend pairs a Tree-sitter grammar with a lowering adapter
in `../adapters/`. The frontends build the checked-in runtime and generated
parsers from `third_party/tree-sitter/`,
`swaff/grammars/tree-sitter-c/`,
`swaff/grammars/tree-sitter-lua/`,
`swaff/grammars/tree-sitter-ocaml/`, and
`swaff/grammars/tree-sitter-sml/`; no parser generation, system package, or
network access is required.

Adding a frontend means adding a grammar entry to the lock file and an
adapter that emits Kliche stereotype calls. Nothing outside
`swaff/adapters/` may include a Tree-sitter header (spec §6.2).
