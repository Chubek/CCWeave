\page ccweave-manual-41 Chapter 41: Documentation workflow

Build the documentation with `cmake -B build -DCCWEAVE_BUILD_DOCS=ON` and
`cmake --build build --target docs`. Generated HTML is placed under
`build/docs/html/`; enabled man pages are placed under `build/docs/man/`.

Doxygen consumes public headers and the Markdown sources in `docs/`,
including this manual. Use stable `\page` identifiers and `\subpage` links
for manual navigation instead of relying only on filesystem ordering.

Keep prose aligned with the normative specification and ABI header. This
manual explains their use; it does not change their contracts.

Previous: \subpage ccweave-manual-40 "Testing" · Next:
\subpage ccweave-manual-42 "Release checklist"
