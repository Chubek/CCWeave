# stdlib-salvo

Stage 10 of the CCWeave build order: standard libraries for the
supported compilers and interpreters.

## libc

`salvo-libc` (`salvoc`) is the C standard library that accompanies
`compilers/cephyr`: freestanding, Linux-targeted, LP64
(x86-64 / aarch64 / riscv64). See `libc/Libc.yaml` for the stdlib
manifest Cephyr consumes.

Layout:

- `include/` — public headers (freestanding + hosted subset)
- `src/` — implementation, grouped by header family
- `src/crt/` — per-architecture `crt0` (`_start`)
- `libc/Libc.yaml` — stdlib manifest (schema version 1)
- `libc/tests/selftest.c` — freestanding self-test (ctest target
  `salvo_libc_selftest`), linked against `salvoc` alone with `-nostdlib`

Cephyr wiring: the driver loads `$CEPHYR_STDLIB_MANIFEST` when set and
falls back to discovering `stdlib-salvo/libc/Libc.yaml` from the working
directory. The build tree also receives a configured
`stdlib-salvo/libc/Libc.yaml` whose include path resolves absolutely, so
`CEPHYR_STDLIB_MANIFEST=<build>/stdlib-salvo/libc/Libc.yaml` consumes
the freshly built archive.
