\page ccweave-manual-39 Chapter 39: Dependencies

S7, MPC, Tree-sitter, and grammars are vendored under `third_party/` or the
repository's grammar area at exact revisions recorded in
`third_party/VERSIONS.lock`.

Never fetch a dependency at build time. A dependency update is an explicit
source change that updates the lock file in the same change set.

CCWeave core code is C11. Vendored code may need its own build treatment,
but it is not an invitation to modify upstream sources merely to satisfy a
local build preference.

Previous: \subpage ccweave-manual-38 "Extension discipline" · Next:
\subpage ccweave-manual-40 "Testing"
