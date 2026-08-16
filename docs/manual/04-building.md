\page ccweave-manual-04 Chapter 4: Building CCWeave

Configure from the repository root with `cmake -S . -B build`, build with
`cmake --build build -j`, and run `ctest --test-dir build
--output-on-failure`. C11 is required for host code.

Tree-sitter frontends are enabled by default. Configure with
`-DCCWEAVE_ENABLE_TREESITTER=OFF` when a frontend-free build is wanted.
AddressSanitizer and UndefinedBehaviorSanitizer are enabled with
`-DCCWEAVE_ENABLE_ASAN=ON`.

Dependencies are already vendored; a normal configure or build must never
download them. Treat an attempted network fetch as a build defect.

Previous: \subpage ccweave-manual-03 "Repository map" · Next:
\subpage ccweave-manual-05 "Conformance and build order"
