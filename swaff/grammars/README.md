# Swaff grammars

Each Swaff frontend pairs a Tree-sitter grammar with a lowering adapter
in `../adapters/`. Grammars are pinned in `third_party/VERSIONS.lock`;
the C frontend resolves `tree-sitter` and `tree-sitter-c` through
pkg-config at configure time when `CCWEAVE_ENABLE_TREESITTER=ON`.

Adding a frontend means adding a grammar entry to the lock file and an
adapter that emits Kliche stereotype calls. Nothing outside
`swaff/adapters/` may include a Tree-sitter header (spec §6.2).
