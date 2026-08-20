\page ccweave-manual-03 Chapter 3: Repository map

The source tree mirrors the architecture. `glue/`, `ir/`, `executors/`,
`oeuph/`, `kliche/`, `swaff/`, `kernels/`, `rewrite-salvo/`, and
`tools/ccw-manifest/` contain the implementation and its extension points.
Tests live in `tests/`.

`third_party/` contains vendored dependencies and `VERSIONS.lock`. Those
sources are pinned and must not be fetched or regenerated during a build.
`manifests/` is generated output only; do not hand-edit it.

The documentation source is under `docs/`; this manual is under
`docs/manual/`. The Doxygen build consumes both Markdown and public C
headers.

Previous: \subpage ccweave-manual-02 "Architecture" · Next:
\subpage ccweave-manual-04 "Building CCWeave"
