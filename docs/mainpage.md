# CCWeave  {#mainpage}

CCWeave is a modular compiler infrastructure built around Weave IR, a
single intermediate representation with two profiles (Tilly for
ahead-of-time compilation and On1x for dynamic execution). The system
is organized as five layers plus one auxiliary engine:

| Layer      | Role                                          |
|------------|-----------------------------------------------|
| **Swaff**  | Frontend orchestration, Tree-sitter grammars  |
| **Kliche** | Paradigm stereotypes (functional, imperative, OOP) |
| **Weave IR** | Canonical IR with Tilly and On1x profiles  |
| **Glue**   | C ABI bridging Kernels to the host (`GlueSTD.h`) |
| **Kernels** | R7RS Scheme libraries describing compilation logic |

**Oeuph** is an equality-saturation (e-graph) rewrite engine that
operates on the canonical in-memory Weave IR.

## Quick Links

- \subpage glue-section "Glue ABI"
- \subpage ir-section "Weave IR"
- \subpage kernels-section "Kernels"
- \subpage oeuph-section "Oeuph Rewrite Engine"
- \subpage kliche-section "Kliche Stereotypes"
- \subpage swaff-section "Swaff Frontends"

## Building

Documentation is built with Doxygen. Enable with `-DCCWEAVE_BUILD_DOCS=ON`
(the default) and build the `docs` target:

    cmake -B build -DCCWEAVE_BUILD_DOCS=ON
    cmake --build build --target docs

HTML output lands in `build/docs/html/`. Man pages (if enabled) land in
`build/docs/man/`.
