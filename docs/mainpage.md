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

## Manual

\ref ccweave-manual-01 "CCWeave Manual" is a 42-chapter guide to the
architecture, public contracts, implementation workflow, and release
checks.

| Part | Chapters |
|------|----------|
| Foundations | \subpage ccweave-manual-01 "1 Orientation", \subpage ccweave-manual-02 "2 Architecture", \subpage ccweave-manual-03 "3 Repository map", \subpage ccweave-manual-04 "4 Building", \subpage ccweave-manual-05 "5 Conformance" |
| Glue and kernels | \subpage ccweave-manual-06 "6 Glue ABI", \subpage ccweave-manual-07 "7 Executor lifecycle", \subpage ccweave-manual-08 "8 Boundary values", \subpage ccweave-manual-09 "9 Host accessors", \subpage ccweave-manual-10 "10 Kernel contract", \subpage ccweave-manual-11 "11 Capabilities", \subpage ccweave-manual-12 "12 Manifests", \subpage ccweave-manual-13 "13 Writing a kernel" |
| Weave IR | \subpage ccweave-manual-14 "14 Weave IR", \subpage ccweave-manual-15 "15 Types and nodes", \subpage ccweave-manual-16 "16 Constructing IR", \subpage ccweave-manual-17 "17 Navigation and mutation", \subpage ccweave-manual-18 "18 Text format", \subpage ccweave-manual-19 "19 Validation", \subpage ccweave-manual-20 "20 Tilly", \subpage ccweave-manual-21 "21 On1x", \subpage ccweave-manual-22 "22 Round-trip" |
| Rewriting | \subpage ccweave-manual-23 "23 Oeuph", \subpage ccweave-manual-24 "24 Rulesets", \subpage ccweave-manual-25 "25 Budgets", \subpage ccweave-manual-26 "26 Standard rewrites" |
| Frontends | \subpage ccweave-manual-27 "27 Kliche", \subpage ccweave-manual-28 "28 Functional", \subpage ccweave-manual-29 "29 Imperative", \subpage ccweave-manual-30 "30 OOP", \subpage ccweave-manual-31 "31 Swaff", \subpage ccweave-manual-32 "32 Adapter policy", \subpage ccweave-manual-33 "33 C and OCaml frontends" |
| Operations | \subpage ccweave-manual-34 "34 S7 executor", \subpage ccweave-manual-35 "35 Host integration", \subpage ccweave-manual-36 "36 Errors", \subpage ccweave-manual-37 "37 Ownership", \subpage ccweave-manual-38 "38 Extension discipline", \subpage ccweave-manual-39 "39 Dependencies", \subpage ccweave-manual-40 "40 Testing", \subpage ccweave-manual-41 "41 Documentation", \subpage ccweave-manual-42 "42 Release checklist" |

## API Quick Links

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
