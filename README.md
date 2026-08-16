# CCWeave

Modular compiler infrastructure implementing CCWeave Specification v0.1.
Five layers plus one auxiliary engine:

| Layer | Directory | Role |
| --- | --- | --- |
| Swaff | `swaff/` | Frontend orchestration; Tree-sitter grammars + lowering adapters |
| Kliche | `kliche/` | Paradigm stereotypes: functional, imperative, oop |
| Weave IR | `ir/` | One IR core; `tilly/` and `on1x/` profiles |
| Glue | `glue/` | C ABI bridging kernels to the host (`GlueSTD.h`, ABI v1) |
| Kernels | `kernels/` | Engine-agnostic R7RS libraries describing compilation logic |
| Oeuph | `oeuph/` | Equality-saturation rewrite engine over the in-memory IR |

## Build

```sh
cmake -S . -B build                       # Swaff frontends off
cmake -S . -B build -DCCWEAVE_ENABLE_TREESITTER=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Sanitizer build: `cmake -S . -B build-asan -DCCWEAVE_ENABLE_ASAN=ON`.

All dependencies are vendored under `third_party/` and pinned in
`third_party/VERSIONS.lock`. Nothing is fetched at build time.

## Manifests are generated

`manifests/Kernel.yaml` and `manifests/Capabilities.yaml` are produced by
`tools/ccw-manifest`, which loads each kernel through the executor and
calls its live `kernel-capabilities`. Never edit them by hand.

```sh
./build/tools/ccw-manifest/ccw-manifest           # regenerate
./build/tools/ccw-manifest/ccw-manifest --check   # CI gate; drift fails
```

## Writing a kernel

A kernel is an R7RS library exporting exactly three procedures, and it
touches IR only through Glue accessors:

```scheme
(define-library (ccweave kernel my-pass)
  (import (scheme base) (ccweave glue))
  (export kernel-info kernel-capabilities kernel-apply)
  (begin
    (define (kernel-info)
      '((name . my-pass) (version . "1.0.0") (description . "...")))
    (define (kernel-capabilities) '(opt.my-pass))
    (define (kernel-apply capability ir options) ir)))
```

Capability ids match `[a-z0-9-]+(\.[a-z0-9-]+)+`; `ccw-manifest` enforces
this. Structural edits go through `instr-build` plus `instr-replace!`,
`instr-insert-before!`, or `instr-delete!` — the host sees every edit and
may log or reject it.

Kernels must stay engine-agnostic: no S7-specific functions and no
engine detection. Anything beyond `(scheme base)` and `(ccweave glue)`
should be feature-tested with `(glue-has? 'name)`.
