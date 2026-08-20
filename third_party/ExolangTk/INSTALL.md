# Installing ExolangTk

ExolangTk is distributed as C99 headers plus a CMake interface target. There is
no compiled ExolangTk library to link.

## Requirements

- A C99 compiler and standard C library.
- CMake 3.16 or newer for the CMake workflow.
- Doxygen only if you want to build the API manual.

## Use from a checkout

The simplest option is to add the repository to your project:

```cmake
add_subdirectory(path/to/ExolangTk)
target_link_libraries(my_runtime PRIVATE ExolangTk)
```

Or compile directly with the include directory:

```sh
cc -std=c99 -I/path/to/ExolangTk/include my_runtime.c -o my_runtime
```

## Configure and install

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build
cmake --install build
```

Installation copies the `include/` tree and exports the `ExolangTk` CMake
target files under `${CMAKE_INSTALL_LIBDIR}/cmake/ExolangTk`. The project
currently does not generate a package-config file, so consumers may either
use the installed export with their own CMake package glue or add the source
tree with `add_subdirectory`.

For a user-local install:

```sh
cmake -S . -B build -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build
cmake --install build
```

Then add the installed include directory to your compiler, for example:

```sh
cc -std=c99 -I"$HOME/.local/include" my_runtime.c -o my_runtime
```

## Tests and documentation

Enable the repository smoke test during configuration:

```sh
cmake -S . -B build -DEXOLANGTK_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Enable Doxygen output when Doxygen is available:

```sh
cmake -S . -B build -DEXOLANGTK_BUILD_DOCS=ON
cmake --build build --target exolangtk-docs
```

Generated HTML is placed in `build/docs/doxygen/html/`.

## Consuming headers correctly

Define implementation guards in exactly one translation unit. For example:

```c
#define ITK_PLATFORM_IMPLEMENTATION
#define ITK_CTYPES_IMPLEMENTATION
#include "InteropTk.h"
```

All other source files should include `InteropTk.h` without those definitions.
See [`GUIDE.md`](GUIDE.md) for the full guard and subsystem workflow.
