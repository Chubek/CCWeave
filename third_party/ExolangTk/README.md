# ExolangTk

ExolangTk is a C99, header-only toolkit for compiler, interpreter, debugger,
and language-runtime authors. It collects the low-level pieces needed to
interoperate with C programs without imposing a runtime, allocator, or build
system on the host application.

## What is included?

| Subsystem | Prefix | Purpose |
| --- | --- | --- |
| InteropTk | `itk_` / `ITK_` | Target and ABI introspection, C types, record layout, calling-convention metadata, mangling, declaration parsing, marshalling, strings, errors, allocators, and export macros |
| FFItk | `ffi_` / `FFI_` | Dynamic library loading, call-interface (CIF) preparation, argument frames, foreign calls, trampolines, closures, and cached bindings |
| DebugTk | `dtk_` / `DTK_` | Symbol lookup, stack unwinding, backtraces, and software breakpoints |
| ExtensionTk | `etk_` / `ETK_` | Dynamic extension loading, semantic versions, dependency-ordered registries, and host-service tables |

Dependencies are deliberately one-way:

```text
FFItk  ──────────────────────────────┐
DebugTk  ──────────────────────────► InteropTk
ExtensionTk  (+ FFItk) ─────────────┘
```

InteropTk is the foundation. ExtensionTk does not depend on DebugTk.

## Quick start

Each module is a single header. Define its implementation macro in exactly one
translation unit, then include the header normally everywhere else.

```c
/* main.c */
#define ITK_PLATFORM_IMPLEMENTATION
#include "InteropTk/itk_platform.h"

int main(void)
{
    itk_target_info target;
    return itk_target_query(&target) != 0 ? 0 : 1;
}
```

Compile directly:

```sh
cc -std=c99 -Iinclude main.c -o example
```

For a complete smoke-test translation unit, see [`tests/smoke.c`](tests/smoke.c).

## CMake

ExolangTk exports an `INTERFACE` target named `ExolangTk`:

```cmake
add_subdirectory(path/to/ExolangTk)
target_link_libraries(my_runtime PRIVATE ExolangTk)
```

Useful options are:

```sh
cmake -S . -B build \
  -DEXOLANGTK_BUILD_TESTS=ON \
  -DEXOLANGTK_BUILD_DOCS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

See [`INSTALL.md`](INSTALL.md) for installation and [`GUIDE.md`](GUIDE.md) for
integration patterns.

## Documentation

The long-form manual is in [`docs/manual`](docs/manual), beginning with
[`01_introduction.md`](docs/manual/01_introduction.md) and
[`02_build.md`](docs/manual/02_build.md). With Doxygen enabled, HTML output is
written to `build/docs/doxygen/html/`.

The YAML files in [`manifests`](manifests) are the source of truth for module
names, dependencies, stability, and exported symbols.

## Requirements and design constraints

- ISO C99; no VLAs or compiler extensions are required.
- Header-only distribution; no ExolangTk library binary is produced.
- Mutable state is caller-owned and passed through structs.
- Cross-boundary allocation uses caller-supplied allocator interfaces.
- Fallible operations return subsystem status values instead of aborting.

The project is licensed under the MIT License.
